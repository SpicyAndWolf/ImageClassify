#include "MainWindow.h"
#include "SettingsStore.h"
#include "LicenseManager.h"
#include "ActivationDialog.h"
#include "BatchPage.h"
#include "ContactDialog.h"

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
#include <QCoreApplication>
#include <QFile>
#include <QTextStream>
#include <QRegularExpression>

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
  auto dir = m_store->resPath();
  cfg.outputDir = dir.isEmpty() ? (app + "/res") : dir;
  m_pageBatch->setRunnerConfig(cfg);

  ensureActivatedOnStart();
  updateActivationUi();
  loadRecentHistory(5);
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

    // 最近任务
    m_lblRecent = new QLabel(tr("最近任务：暂无"), this);
    m_lblRecent->setObjectName("subtleNote");
    m_lblRecent->setTextInteractionFlags(Qt::TextSelectableByMouse);

    v->addWidget(title);
    v->addSpacing(8);
    v->addWidget(m_lblStatus);
    v->addWidget(m_lblMachine);
    v->addSpacing(8);
    v->addWidget(m_btnActivate);
    v->addSpacing(12);
    v->addWidget(m_lblRecent);
    v->addStretch();
    v->addWidget(btnContact);

    connect(m_btnActivate, &QPushButton::clicked, [this]{
      ActivationDialog dlg(m_lic, this);
      if (dlg.exec()==QDialog::Accepted) updateActivationUi();
    });
    connect(btnContact, &QPushButton::clicked,  this, &MainWindow::onContactAdminClicked);
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

        // 同步更新配置
        BatchPage::RunnerConfig cfg = m_pageBatch->getRunnerConfig();  // 假设你在 BatchPage 已有 getter（下面附带实现）
        cfg.outputDir = dir;
        m_pageBatch->setRunnerConfig(cfg);
      }
    });
  }

  m_stack->addWidget(m_pageDashboard);
  m_stack->addWidget(m_pageBatch);
  m_stack->addWidget(m_pageSettings);

  root->addWidget(m_nav);
  root->addWidget(m_stack, 1);

  // 运行期实时刷新最近任务
  connect(m_pageBatch, &BatchPage::jobStarted, this, [this](const QString& dir){
      appendRecent(QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss ")
                   + "开始处理：" + dir);
  });
  connect(m_pageBatch, &BatchPage::jobFinished, this, [this](const QString& dir, int code, int st){
      const char* stTxt = (st==0 ? "normal" : "crash");
      appendRecent(QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss ")
                   + QString("结束：%1（%2,%3）").arg(dir).arg(code).arg(stTxt));
  });

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

void MainWindow::onContactAdminClicked()
{
    // 优先从 <app>/contact.txt 读取，便于后续不改代码直接替换
    auto readContactFromTxt = []() -> std::tuple<QString,QString,QString> {
        const QString path = QCoreApplication::applicationDirPath() + "/contact.txt";
        QFile f(path);
        QString name, email, phone;
        if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream in(&f);
            in.setCodec("UTF-8");
            QRegularExpression kvRe(R"(^\s*([A-Za-z_]+)\s*=\s*(.+?)\s*$)");
            while (!in.atEnd()) {
                const QString line = in.readLine();
                if (line.trimmed().startsWith('#') || line.trimmed().isEmpty()) continue;
                auto m = kvRe.match(line);
                if (m.hasMatch()) {
                    const QString key = m.captured(1).toLower();
                    const QString val = m.captured(2).trimmed();
                    if (key == "name")  name  = val;
                    else if (key == "email") email = val;
                    else if (key == "phone") phone = val;
                }
            }
        }
        return {name, email, phone};
    };

    auto [name, email, phone] = readContactFromTxt();
    if (name.isEmpty())  name  = tr("无");
    if (email.isEmpty()) email = "无";
    if (phone.isEmpty()) phone = "无";

    ContactDialog dlg(name, email, phone, this);
    dlg.exec(); // 模态展示
}

void MainWindow::appendRecent(const QString& line) {
    const int MAX = 5;
    if (line.isEmpty()) {
        // 保持现状
    } else {
        m_recent.prepend(line);
        while (m_recent.size() > MAX) m_recent.removeLast();
    }
    if (m_recent.isEmpty()) {
        m_lblRecent->setText(tr("最近任务：暂无"));
    } else {
        m_lblRecent->setText(tr("最近任务：\n• ") + m_recent.join("\n• "));
    }
}

void MainWindow::loadRecentHistory(int maxItems) {
    const QString path = QCoreApplication::applicationDirPath() + "/logs/history.log";
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        appendRecent(QString());
        return;
    }
    const QString all = QString::fromUtf8(f.readAll());
    const auto lines = all.split('\n', Qt::SkipEmptyParts);
    // 只挑关键行（START/END），从后往前取最近 maxItems 条
    QStringList picked;
    for (int i = lines.size()-1; i >= 0 && picked.size() < maxItems; --i) {
        const QString &ln = lines[i].trimmed();
        if (ln.contains(" START ") || ln.contains(" END   ")) {
            picked.prepend(ln);
        }
    }
    if (picked.isEmpty()) {
        appendRecent(QString());
        return;
    }
    // 为了跟实时显示保持一致，可以做个轻量映射（可选）
    for (const auto& ln : picked) {
        // 直接显示原文也可以：appendRecent(ln);
        // 简单美化：把 " START " / " END   " 替换为“开始处理：/结束：”
        if (ln.contains(" START ")) {
            appendRecent(ln.left(19) + " 开始处理：" + ln.section(' ', 3)); // 19 是时间戳长度
        } else if (ln.contains(" END   ")) {
            appendRecent(ln.left(19) + " 结束：" + ln.section(' ', 3));
        }
    }
}

