#pragma once
#include <QObject>

class SettingsStore;
class ApiClient;

class LicenseManager : public QObject {
  Q_OBJECT
public:
  explicit LicenseManager(SettingsStore* store, QObject* parent=nullptr);

  bool isActivated() const;         // 纯本地：token存在 && storedMid==currentMid && token.mid==currentMid
  QString machineId() const;        // 当前设备指纹
  void activate(const QString& cdk);// 异步联网验证并绑定，成功后落盘

  void setApiBaseUrl(const QUrl& baseUrl); // 便于测试/切换环境

signals:
  void activated();
  void activationError(const QString& reason);

private slots:
  void onVerifyFinished(bool ok, const QString& token, const QString& serverMachineId, const QString& message);

private:
  bool isTokenValidForThisMachine(const QString& token) const;

  SettingsStore* m_store;
  ApiClient*     m_api;
  QString        m_mid; // current machine id
};
