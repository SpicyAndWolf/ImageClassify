#include "MainWindow.h"
#include "SettingsStore.h"
#include "LicenseManager.h"
#include "ActivationDialog.h"
#include "BatchPage.h"

#include <QListWidget>
#include <QStackedWidget>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QFileDialog>
#include <QDesktopServices>
#include <QUrl>
#include <QApplication>

static void loadAppTheme() {
  // Step 5 里也给出完整 QSS，这里可直接调用；为了可独立编译，这里留空函数占位
}

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
  loadAppTheme();
  m_store = new SettingsStore(this);
  m_lic   = new LicenseManager(m_store, this);

  buildUi();

  // 配置脚本路径
  BatchPage::RunnerConfig cfg;
  const QString app = QCoreApplication::applicationDirPath();
  cfg.pythonExe = app + "/python-embed/python.exe";  // embed python
  cfg.embedDir  = app + "/python-embed";             // 非空 => 按 embed 方式注入环境
  cfg.scriptPath= app + "/TitleOcr/ocr.py";
  cfg.outputDir = app + "/res"; // 默认是Settings Store读入。
  // cfg.extraArgs << "--any-other-flags";
  m_pageBatch->setRunnerConfig(cfg);

  ensureActivatedOnStart();
  updateActivationUi();
}

void MainWindow::buildUi() {
  auto* central = new QWidget(this);
  auto* root = new QHBoxLayout(central);
  setCentralWidget(central);

  // 左侧导航
  m_nav = new QListWidget(this);
  m_nav->setFixedWidth(160);
  m_nav->addItem(tr("总览"));
  m_nav->addItem(tr("批处理"));
  m_nav->addItem(tr("设置"));

  // 右侧堆栈
  m_stack = new QStackedWidget(this);

  // PageDashboard
  m_pageDashboard = new QWidget(this);
  {
    auto* v = new QVBoxLayout(m_pageDashboard);
    auto* title = new QLabel(tr("仪表盘"), this);
    title->setObjectName("pageTitle");
    m_lblStatus  = new QLabel(this);
    m_lblMachine = new QLabel(this);
    m_btnActivate= new QPushButton(tr("激活"), this);
    auto* btnContact = new QPushButton(tr("联系管理员"), this);

    // 最近任务（占位）
    auto* recent = new QLabel(tr("最近任务：暂无"), this);
    recent->setObjectName("subtleNote");

    v->addWidget(title);
    v->addSpacing(8);
    v->addWidget(m_lblStatus);
    v->addWidget(m_lblMachine);
    v->addSpacing(8);
    v->addWidget(m_btnActivate);
    v->addSpacing(12);
    v->addWidget(recent);
    v->addStretch();
    v->addWidget(btnContact);

    connect(m_btnActivate, &QPushButton::clicked, [this]{
      ActivationDialog dlg(m_lic, this);
      if (dlg.exec()==QDialog::Accepted) updateActivationUi();
    });
    connect(btnContact, &QPushButton::clicked, []{
      // 你可以替换为钉钉/企业微信/网页
      QDesktopServices::openUrl(QUrl("mailto:admin@example.com?subject=CDK%20申请/问题"));
    });
  }

  // PageBatch（骨架）
  m_pageBatch = new BatchPage(this);

  // PageSettings（仅结果目录）
  m_pageSettings = new QWidget(this);
  {
    auto* v = new QVBoxLayout(m_pageSettings);
    auto* title = new QLabel(tr("设置"), this);
    title->setObjectName("pageTitle");
    auto* lbl = new QLabel(tr("结果目录：%1").arg(m_store->resPath()), this);
    auto* btn = new QPushButton(tr("选择结果目录"), this);
    v->addWidget(title);
    v->addSpacing(8);
    v->addWidget(lbl);
    v->addWidget(btn);
    v->addStretch();

    connect(btn, &QPushButton::clicked, [this, lbl]{
      const auto dir = QFileDialog::getExistingDirectory(this, tr("选择结果目录"));
      if (!dir.isEmpty()) {
        m_store->setResPath(dir);
        lbl->setText(tr("结果目录：%1").arg(dir));
      }
    });
  }

  m_stack->addWidget(m_pageDashboard);
  m_stack->addWidget(m_pageBatch);
  m_stack->addWidget(m_pageSettings);

  root->addWidget(m_nav);
  root->addWidget(m_stack, 1);

  connect(m_nav, &QListWidget::currentRowChanged, m_stack, &QStackedWidget::setCurrentIndex);
  m_nav->setCurrentRow(0);
}

void MainWindow::ensureActivatedOnStart() {
  if (!m_lic->isActivated()) {
    ActivationDialog dlg(m_lic, this);
    dlg.exec();
  }
}

void MainWindow::updateActivationUi() {
  const bool ok = m_lic->isActivated();
  m_lblStatus->setText(ok ? tr("激活状态：已激活") : tr("激活状态：未激活"));
  m_lblMachine->setText(ok ? tr("设备指纹：%1").arg(m_lic->machineId())
                           : tr("设备指纹：未激活"));
  m_btnActivate->setEnabled(!ok);

  // 页面可用性联动
  m_pageBatch->setActivated(ok);
  for (auto* w : m_pageSettings->findChildren<QWidget*>()) w->setEnabled(ok);

  // 未激活时强制回到 Dashboard
  if (!ok) m_nav->setCurrentRow(0);
}
