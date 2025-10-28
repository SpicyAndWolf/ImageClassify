// DeviceFingerprint.cpp
#include "DeviceFingerprint.h"
#include <QCryptographicHash>
#include <QNetworkInterface>
#include <QSysInfo>

static QString firstPhysicalMac() {
  const auto ifaces = QNetworkInterface::allInterfaces();
  for (const auto& nic : ifaces) {
    const bool isUp    = nic.flags().testFlag(QNetworkInterface::IsUp);
    const bool notLoop = !nic.flags().testFlag(QNetworkInterface::IsLoopBack);
    const bool notVirt = !nic.humanReadableName().contains("Virtual", Qt::CaseInsensitive)
                      && !nic.humanReadableName().contains("VM", Qt::CaseInsensitive);
    if (isUp && notLoop && notVirt && !nic.hardwareAddress().isEmpty())
      return nic.hardwareAddress().toUpper(); // AA:BB:CC:DD:EE:FF
  }
  return QString();
}

QString DeviceFingerprint::machineId() {
  const QString host = QSysInfo::machineHostName();
  QString mac = firstPhysicalMac();
  if (mac.isEmpty()) mac = QStringLiteral("NO-MAC");
  const QByteArray raw  = (host + "|" + mac).toUtf8();
  const QByteArray hash = QCryptographicHash::hash(raw, QCryptographicHash::Sha256).toHex();
  return QString::fromLatin1(hash);
}
