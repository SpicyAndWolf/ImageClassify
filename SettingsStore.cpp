// SettingsStore.cpp
#include "SettingsStore.h"
#include <QStandardPaths>
#include <QDir>

static QString configFilePath() {
  const QString base = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
  QDir().mkpath(base);

  // 统一到独立 ini 文件，避免随程序目录走
  return base + "/config.ini";
}

SettingsStore::SettingsStore(QObject* p)
  : QObject(p)
  , m_settings(configFilePath(), QSettings::IniFormat)
{}

QString SettingsStore::resPath() const { return m_settings.value("app/resPath").toString(); }
void SettingsStore::setResPath(const QString& path) { m_settings.setValue("app/resPath", path); }

bool SettingsStore::isActivated() const { return m_settings.value("license/activated", false).toBool(); }
void SettingsStore::setActivated(bool on) { m_settings.setValue("license/activated", on); }

QString SettingsStore::licenseToken() const { return m_settings.value("license/token").toString(); }
void SettingsStore::setLicenseToken(const QString& t) { m_settings.setValue("license/token", t); }

QString SettingsStore::storedMachineId() const { return m_settings.value("license/machineId").toString(); }
void SettingsStore::setStoredMachineId(const QString& id) { m_settings.setValue("license/machineId", id); }
