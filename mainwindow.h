#pragma once
#include <QMainWindow>

class SettingsStore;
class LicenseManager;
class BatchPage;

class MainWindow : public QMainWindow {
  Q_OBJECT
public:
  explicit MainWindow(QWidget* parent=nullptr);

private:
  void buildUi();
  void ensureActivatedOnStart();
  void updateActivationUi();

  // 触发函数
  void onContactAdminClicked();

  // pages
  QWidget*    m_pageDashboard{nullptr};
  BatchPage*  m_pageBatch{nullptr};
  QWidget*    m_pageSettings{nullptr};

  // core
  SettingsStore*  m_store;
  LicenseManager* m_lic;

  // nav & stack
  class QListWidget*   m_nav{nullptr};
  class QStackedWidget* m_stack{nullptr};

  // dashboard widgets
  class QLabel*    m_lblStatus;
  class QLabel*    m_lblMachine;
  class QPushButton* m_btnActivate;

  // 最近任务
  QLabel*      m_lblRecent {nullptr};
  QStringList  m_recent;                // 新增：最多保留 N 条
  void appendRecent(const QString& line);
  void loadRecentHistory(int maxItems = 5); // 新增：从 history.log 初始化
};
