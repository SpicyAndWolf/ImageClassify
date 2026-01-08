#include "ApiClient.h"
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>

ApiClient::ApiClient(QObject* p) : QObject(p) {}

void ApiClient::setBaseUrl(const QUrl& base) { m_base = base; }

void ApiClient::verifyAndBind(const QString& cdk, const QString& machineId) {
  // TODO: 替换为你的阿里云函数网关基础地址
  const QUrl url = m_base.isEmpty()
      ? QUrl("https://image-cify-auth-zvtflexxfj.cn-hangzhou.fcapp.run/license/verify-and-bind")
      : m_base.resolved(QUrl("/license/verify-and-bind"));

  QNetworkRequest req(url);
  req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

  QJsonObject body { {"cdk", cdk}, {"machineId", machineId} };
  auto* reply = m_nam.post(req, QJsonDocument(body).toJson());

  connect(reply, &QNetworkReply::finished, this, [this, reply]() {
    const auto bytes = reply->readAll();
    const auto err   = reply->error();
    reply->deleteLater();

    if (err != QNetworkReply::NoError) {
      // 把后端返回的错误体透传一下，便于定位
      const QString msg = bytes.isEmpty() ? reply->errorString() : QString::fromUtf8(bytes);
      emit verifyFinished(false, {}, {}, msg);
      return;
    }
    const auto doc = QJsonDocument::fromJson(bytes);
    if (!doc.isObject()) { emit verifyFinished(false, {}, {}, "Bad JSON"); return; }

    const auto obj = doc.object();
    const bool ok        = obj.value("ok").toBool();
    const QString token  = obj.value("token").toString();
    const QString mid    = obj.value("machineId").toString();
    const QString message= obj.value("message").toString();
    emit verifyFinished(ok, token, mid, message);
  });
}
