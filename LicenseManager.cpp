#include "LicenseManager.h"
#include "SettingsStore.h"
#include "ApiClient.h"
#include "DeviceFingerprint.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QDateTime>

static bool parseTokenMachineId(const QString& token, QString* outMid, qint64* outExpire=nullptr) {
  // token = base64url(JSON). JSON 至少包含 machineId，选填 expireAt(Unix)
  const auto raw = QByteArray::fromBase64(token.toUtf8(),
      QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
  const auto doc = QJsonDocument::fromJson(raw);
  if (!doc.isObject()) return false;
  const auto obj = doc.object();
  const auto mid = obj.value("machineId").toString();
  if (mid.isEmpty()) return false;
  *outMid = mid;
  if (outExpire) *outExpire = static_cast<qint64>(obj.value("expireAt").toDouble(0));
  return true;
}

LicenseManager::LicenseManager(SettingsStore* store, QObject* parent)
  : QObject(parent), m_store(store), m_api(new ApiClient(this)), m_mid(DeviceFingerprint::machineId()) {
  connect(m_api, &ApiClient::verifyFinished, this, &LicenseManager::onVerifyFinished);
}

void LicenseManager::setApiBaseUrl(const QUrl& baseUrl) {
  m_api->setBaseUrl(baseUrl);
}

bool LicenseManager::isActivated() const {
  const auto token    = m_store->licenseToken();
  const auto storedId = m_store->storedMachineId();
  if (token.isEmpty() || storedId.isEmpty()) return false;
  if (storedId != m_mid) return false;            // 防拷贝：AppData中记录的机器码必须与当前一致
  return isTokenValidForThisMachine(token);       // 解析 token 并比对 machineId（可同时检查过期）
}

QString LicenseManager::machineId() const { return m_mid; }

void LicenseManager::activate(const QString& cdk) {
  if (cdk.trimmed().isEmpty()) { emit activationError(tr("请输入有效的激活码")); return; }
  m_api->verifyAndBind(cdk.trimmed(), m_mid);
}

void LicenseManager::onVerifyFinished(bool ok, const QString& token, const QString& serverMid, const QString& msg) {
  if (!ok) { emit activationError(!msg.isEmpty()? msg : tr("激活失败：网络或服务器错误")); return; }
  if (serverMid != m_mid) { emit activationError(tr("服务器绑定的设备指纹与本机不一致")); return; }

  // 解析token，确保内含machineId一致（保险双重比对）
  QString midInToken; qint64 exp = 0;
  if (!parseTokenMachineId(token, &midInToken, &exp) || midInToken != m_mid) {
    emit activationError(tr("无效的授权令牌"));
    return;
  }
  // 可选：过期检查
  if (exp > 0 && QDateTime::currentSecsSinceEpoch() > exp) {
    emit activationError(tr("授权已过期"));
    return;
  }

  // 落盘（存到 AppConfig 目录，避免随压缩包走）
  m_store->setLicenseToken(token);
  m_store->setStoredMachineId(m_mid);
  m_store->setActivated(true);

  emit activated();
}

bool LicenseManager::isTokenValidForThisMachine(const QString& token) const {
  QString mid; qint64 exp = 0;
  if (!parseTokenMachineId(token, &mid, &exp)) return false;
  if (mid != m_mid) return false;
  if (exp > 0 && QDateTime::currentSecsSinceEpoch() > exp) return false; // 过期则视为未激活
  return true;
}
