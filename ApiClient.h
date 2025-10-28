#pragma once
#include <QObject>
#include <QNetworkAccessManager>

class ApiClient : public QObject {
  Q_OBJECT
public:
  explicit ApiClient(QObject* parent=nullptr);
  void setBaseUrl(const QUrl& base);               // e.g. https://api.example.com
  void verifyAndBind(const QString& cdk, const QString& machineId);
signals:
  // ok, token(base64url json), serverMachineId, message
  void verifyFinished(bool ok, const QString& token, const QString& serverMachineId, const QString& message);
private:
  QNetworkAccessManager m_nam;
  QUrl m_base;
};
