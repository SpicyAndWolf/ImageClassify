// SettingsStore.h
#pragma once
#include <QObject>
#include <QSettings>

class SettingsStore : public QObject {
  Q_OBJECT
public:
  explicit SettingsStore(QObject* parent=nullptr);

  // 结果目录
  QString resPath() const;
  void setResPath(const QString& path);

  // 许可
  bool isActivated() const;
  void setActivated(bool on);
  QString licenseToken() const;     // 后端返回的 token（JSON 或自定义格式）
  void setLicenseToken(const QString& t);
  QString storedMachineId() const;  // 首次激活时落盘的 machineId（用于比对）
  void setStoredMachineId(const QString& id);

private:
  QSettings m_settings;
};
