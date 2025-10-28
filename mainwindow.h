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

  // pages
  QWidget*    m_pageDashboard;
  BatchPage*  m_pageBatch;
  QWidget*    m_pageSettings;

  // core
  SettingsStore*  m_store;
  LicenseManager* m_lic;

  // nav & stack
  class QListWidget*   m_nav;
  class QStackedWidget* m_stack;

  // dashboard widgets
  class QLabel*    m_lblStatus;
  class QLabel*    m_lblMachine;
  class QPushButton* m_btnActivate;
};
