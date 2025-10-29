#include "BatchPage.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QListWidget>
#include <QPushButton>
#include <QFileDialog>
#include <QLabel>
#include <QProgressBar>
#include <QTextEdit>
#include <QDir>
#include <QFileInfo>
#include <QApplication>
#include <QDateTime>
#include <QFile>
#include <QTextStream>

BatchPage::BatchPage(QWidget* parent)
  : QWidget(parent),
    m_proc(new QProcess(this)),
    m_rxProgress(QRegularExpression(QStringLiteral("^\\s*PROGRESS\\s+(\\d{1,3})\\s*%\\s*$"))),
    m_rxStat(QRegularExpression(QStringLiteral("^\\s*STAT\\b\\s*(.*)$"), QRegularExpression::CaseInsensitiveOption)),
    m_rxFinal(QRegularExpression(QStringLiteral("^\\s*FINAL_STATISTICS:\\s*TOTAL=(\\d+),\\s*PROCESSED=(\\d+),\\s*CATEGORIES=(\\d+)\\s*$"),
                                QRegularExpression::CaseInsensitiveOption))
{
  buildUi();

  connect(m_proc, &QProcess::readyReadStandardOutput, [this]{
    const QString chunk = QString::fromLocal8Bit(m_proc->readAllStandardOutput());
    parseStdoutLines(chunk);
  });
  connect(m_proc, &QProcess::readyReadStandardError, [this]{
    const QString chunk = QString::fromLocal8Bit(m_proc->readAllStandardError());
    appendLogLine(QStringLiteral("[stderr] %1").arg(chunk.trimmed()));
  });
  connect(m_proc, QOverload<int,QProcess::ExitStatus>::of(&QProcess::finished),
          [this](int code, QProcess::ExitStatus st){
    appendLogLine(QStringLiteral("进程结束：%1（%2）").arg(code).arg(st==QProcess::NormalExit?"normal":"crash"));
    writeHistory("END", m_current, code, st);
    emit jobFinished(m_current, code, static_cast<int>(st));
    startNextJob();
  });
  connect(m_proc, &QProcess::errorOccurred, [this](QProcess::ProcessError e){
    appendLogLine(QStringLiteral("[错误] QProcess error=%1, errStr=%2")
                  .arg(static_cast<int>(e)).arg(m_proc->errorString()));
  });
}

void BatchPage::buildUi() {
  auto* v = new QVBoxLayout(this);
  auto* title = new QLabel(tr("批处理"), this);
  title->setObjectName("pageTitle");
  v->addWidget(title);

  auto* bar = new QHBoxLayout();
  m_btnAdd   = new QPushButton(tr("添加文件夹"), this);
  m_btnClear = new QPushButton(tr("清空"), this);
  m_btnStart = new QPushButton(tr("开始"), this);
  m_btnStop  = new QPushButton(tr("停止"), this);
  m_btnStop->setEnabled(false);
  bar->addWidget(m_btnAdd);
  bar->addWidget(m_btnClear);
  bar->addStretch();
  bar->addWidget(m_btnStart);
  bar->addWidget(m_btnStop);
  v->addLayout(bar);

  m_list = new QListWidget(this);
  m_list->setObjectName("batchList");       // 供 QSS 精准定制行高
  m_list->setUniformItemSizes(false);       // 让 sizeHint 生效
  m_list->setSpacing(4);
  m_list->setSelectionMode(QAbstractItemView::ExtendedSelection);
  v->addWidget(m_list, 1);

  auto* bottom = new QHBoxLayout();
  m_progress = new QProgressBar(this);
  m_progress->setRange(0, 100);
  m_progress->setValue(0);
  m_stat = new QLabel(tr("统计：total=0  ok=0  fail=0"), this);
  bottom->addWidget(m_progress, 2);
  bottom->addWidget(m_stat, 1);
  v->addLayout(bottom);

  m_log = new QTextEdit(this);
  m_log->setReadOnly(true);
  m_log->setMinimumHeight(140);
  v->addWidget(m_log);

  connect(m_btnAdd,  &QPushButton::clicked, [this]{
    const auto dir = QFileDialog::getExistingDirectory(this, tr("选择文件夹"));
    if (!dir.isEmpty() && !hasFolder(dir)) {
          addFolderItem(dir);
    }
  });
  connect(m_btnClear,&QPushButton::clicked, [this]{ m_list->clear(); });
  connect(m_btnStart,&QPushButton::clicked, this, &BatchPage::startRun);
  connect(m_btnStop, &QPushButton::clicked,  this, &BatchPage::stopRun);
}

// 判断列表里是否已存在该目录（用绝对路径对比）
bool BatchPage::hasFolder(const QString& dir) const {
  const QString canon = QDir(dir).absolutePath();
  for (int i=0; i<m_list->count(); ++i) {
    const auto* it = m_list->item(i);
    const QString saved = it->data(Qt::UserRole).toString();
    if (!saved.isEmpty() && QDir(saved).absolutePath() == canon) return true;
    if (saved.isEmpty() && QDir(it->text()).absolutePath() == canon) return true; // 向后兼容旧数据
  }
  return false;
}

// 创建“路径 + 删除按钮”的一行
void BatchPage::addFolderItem(const QString& dir) {
  auto* item = new QListWidgetItem(m_list);
  item->setData(Qt::UserRole, dir); // 把真实路径放在 UserRole，便于 selectedFolders() 读取
  item->setSizeHint(QSize(item->sizeHint().width(), 34));

  auto* row = new QWidget(m_list);
  row->setAttribute(Qt::WA_StyledBackground, true);
  auto* hl  = new QHBoxLayout(row);
  hl->setContentsMargins(8, 2, 8, 2);
  hl->setSpacing(8);

  auto* lbl = new QLabel(dir, row);
  lbl->setTextInteractionFlags(Qt::TextSelectableByMouse); // 允许鼠标选择复制
  lbl->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

  auto* btn = new QPushButton(tr("Del"), row);
  btn->setObjectName("btnDelete");                  // 供 QSS 定制成红色
  btn->setProperty("danger", true);                 // 另一种匹配方式
  btn->setFixedHeight(32);
  btn->setCursor(Qt::PointingHandCursor);

  hl->addWidget(lbl, /*stretch*/1);
  hl->addWidget(btn, /*stretch*/0);
  row->setLayout(hl);

  // 关键：让行高跟随我们自定义控件的 sizeHint（绕开全局 item 高度的限制）
  item->setSizeHint(QSize(item->sizeHint().width(), row->sizeHint().height()+4));
  m_list->addItem(item);
  m_list->setItemWidget(item, row);

  // 点击删除：把对应 item 移除并释放
  connect(btn, &QPushButton::clicked, this, [this, item]{
    const int r = m_list->row(item);
    if (r >= 0) {
      delete m_list->takeItem(r); // takeItem 后需要手动 delete，否则泄漏
    }
  });
}

void BatchPage::setActivated(bool on) {
  for (auto* w : findChildren<QWidget*>()) w->setEnabled(on);
}

void BatchPage::setRunnerConfig(const RunnerConfig& cfg) { m_cfg = cfg; }

BatchPage::RunnerConfig BatchPage::getRunnerConfig() const{ return m_cfg; }

QStringList BatchPage::selectedFolders() const {
  QStringList dirs;
  for (int i=0;i<m_list->count();++i) {
    const auto* it = m_list->item(i);
    const QString d = it->data(Qt::UserRole).toString();
    dirs << (d.isEmpty() ? it->text() : d); // 兼容旧存储方式
  }
  return dirs;
}

void BatchPage::startRun() {
  if (m_cfg.pythonExe.isEmpty() || m_cfg.scriptPath.isEmpty() || m_cfg.outputDir.isEmpty()) {
    appendLogLine(tr("[错误] 运行配置不完整（pythonExe/scriptPath/outputDir）"));
    return;
  }
  if (!QFile::exists(m_cfg.pythonExe)) {
    appendLogLine(tr("[错误] 找不到 python 可执行文件：%1").arg(m_cfg.pythonExe));
    return;
  }
  if (!QFile::exists(m_cfg.scriptPath)) {
    appendLogLine(tr("[错误] 找不到脚本：%1").arg(m_cfg.scriptPath));
    return;
  }

  const auto dirs = selectedFolders();
  if (dirs.isEmpty()) { appendLogLine(tr("请先添加至少一个文件夹")); return; }

  m_sum = {};
  m_progress->setValue(0);
  m_log->clear();
  updateStatsLabel();
  resetUiForRun(true);

  enqueueJobs(dirs);
  startNextJob();
}

void BatchPage::stopRun() {
  if (m_proc->state() != QProcess::NotRunning) {
    m_proc->terminate();
    if (!m_proc->waitForFinished(1500)) m_proc->kill();
  }
  m_queue.clear();
  appendLogLine(tr("已停止"));
  resetUiForRun(false);
}

void BatchPage::enqueueJobs(const QStringList& folders) {
  m_queue.clear();
  QSet<QString> seen;
  for (const auto& d : folders) {
    const QString canon = QDir(d).absolutePath();
    if (!seen.contains(canon)) { seen.insert(canon); m_queue.enqueue(canon); }
  }
}

QProcessEnvironment BatchPage::buildEnvForEmbed(const QString& embedDir, const QProcessEnvironment& baseEnv) const {
  auto env = baseEnv;

  // 1) PATH 头部加入 embedDir
  QString path = env.value("PATH");
#ifdef Q_OS_WIN
  const QString sep = ";";
#else
  const QString sep = ":";
#endif
  if (!path.startsWith(embedDir, Qt::CaseInsensitive)) {
    path = embedDir + sep + path;
    env.insert("PATH", path);
  }

  // 2) PYTHONHOME
  env.insert("PYTHONHOME", embedDir);

  // 3) PYTHONPATH（按常见 embed 布局）
  QString pyPath = env.value("PYTHONPATH");
  QStringList candidates{
    embedDir,
    embedDir + "/Lib",
    embedDir + "/site-packages"
  };
  for (const auto& p : candidates) {
    if (!pyPath.contains(p, Qt::CaseInsensitive)) {
      pyPath = pyPath.isEmpty() ? p : (p + sep + pyPath);
    }
  }
  env.insert("PYTHONPATH", pyPath);

  return env;
}

void BatchPage::appendLogLine(const QString &line)
{
    // 写入ui
    m_log->append(line);

    // 持久化到 <app>/logs/run.log
    const QString appDir = QCoreApplication::applicationDirPath();
    const QString logDir = appDir + "/logs";
    QDir().mkpath(logDir); // 若不存在则创建

    QFile f(logDir + "/run.log");
    if (f.open(QIODevice::Append | QIODevice::Text)) {
        QTextStream out(&f);
        out << QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss ")
            << line << '\n';
        out.flush(); // 及时落盘
    }
}

void BatchPage::startNextJob() {
  if (m_queue.isEmpty()) {
    appendLogLine(tr("全部任务完成"));
    resetUiForRun(false);
    m_progress->setValue(100);
    return;
  }

  m_current = m_queue.dequeue();
  appendLogLine(QStringLiteral("开始处理：%1").arg(m_current));
  m_progress->setValue(0);
  writeHistory("START", m_current);
  emit jobStarted(m_current);

  // 组装参数
  QStringList args;
  args << "-u" << m_cfg.scriptPath
       << "--resPath" << m_cfg.outputDir
       << m_current;                  // 兼容你旧方案：把每个文件夹作为位置参数
  args << m_cfg.extraArgs;

  // 进程环境与工作目录（embed 关键）
  QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
#ifdef Q_OS_WIN
  const bool useEmbed = !m_cfg.embedDir.isEmpty();
  if (useEmbed) env = buildEnvForEmbed(m_cfg.embedDir, env);
#endif

  // 设置 program/args/cwd/env
  QFileInfo scriptFi(m_cfg.scriptPath);
  const QString cwd = scriptFi.absolutePath();

  m_proc->setWorkingDirectory(cwd);
  m_proc->setProgram(m_cfg.pythonExe);
  m_proc->setArguments(args);
  m_proc->setProcessEnvironment(env);
  m_proc->setProcessChannelMode(QProcess::SeparateChannels);

  // 调试输出：若启动失败，可据此排查
  auto logDbg = [&](const QString& s){ if (m_verbose) appendLogLine(s); };
  logDbg(QStringLiteral("[debug] program=%1").arg(m_cfg.pythonExe));
  logDbg(QStringLiteral("[debug] args=%1").arg(args.join(" ")));
  logDbg(QStringLiteral("[debug] cwd=%1").arg(cwd));
#ifdef Q_OS_WIN
  if (!m_cfg.embedDir.isEmpty()) {
     logDbg(QStringLiteral("[debug] embedDir=%1").arg(m_cfg.embedDir));
     logDbg(QStringLiteral("[debug] PATH(head)=%1").arg(env.value("PATH").left(200)));
     logDbg(QStringLiteral("[debug] PYTHONHOME=%1").arg(env.value("PYTHONHOME")));
     logDbg(QStringLiteral("[debug] PYTHONPATH=%1").arg(env.value("PYTHONPATH")));
  }
#endif

  m_proc->start();
  if (!m_proc->waitForStarted(3000)) {
    appendLogLine(tr("[错误] 无法启动进程：%1").arg(m_proc->errorString()));
    // 继续尝试下一个，避免队列卡死
    startNextJob();
  }
}

void BatchPage::resetUiForRun(bool running) {
  m_btnStart->setEnabled(!running);
  m_btnStop->setEnabled(running);
  m_btnAdd->setEnabled(!running);
  m_btnClear->setEnabled(!running);
  m_list->setEnabled(!running);
}

void BatchPage::parseStdoutLines(const QString& chunk) {
  m_outBuf += chunk;
  int idx;
  while ((idx = m_outBuf.indexOf('\n')) != -1) {
    QString line = m_outBuf.left(idx);
    m_outBuf.remove(0, idx+1);
    line = line.trimmed();
    if (line.isEmpty()) continue;

    // 1) PROGRESS x%
    auto m = m_rxProgress.match(line);
    if (m.hasMatch()) {
      int p = m.captured(1).toInt();
      m_progress->setValue(qBound(0, p, 100));
      continue;
    }

    // 2) 统计行（覆盖式快照）：STAT total=<x> ok=<y>|processed=<y> fail=<z>
    if (auto mm = m_rxStat.match(line); mm.hasMatch()) {
      const QString payload = mm.captured(1);
      qint64 total=-1, ok=-1, fail=-1;
      const auto parts = payload.split(QRegularExpression("[,\\s]+"), Qt::SkipEmptyParts);
      for (const auto& kv : parts) {
        const auto pair = kv.split('=');
        if (pair.size()!=2) continue;
        const QString k = pair[0].trimmed().toLower();
        const qint64  v = pair[1].trimmed().toLongLong();
        if (k=="total") total=v;
        else if (k=="ok" || k=="processed") ok=v;
        else if (k=="fail") fail=v;
      }
      if (total>=0) m_sum.total = total;
      if (ok>=0)    m_sum.ok    = ok;
      if (fail>=0)  m_sum.fail  = fail;
      updateStatsLabel();
      continue;
    }

    // 3) FINAL_STATISTICS
    m = m_rxFinal.match(line);
    if (m.hasMatch()) {
      appendLogLine(QStringLiteral("分类完成：total=%1 processed=%2 categories=%3")
                    .arg(m.captured(1))
                    .arg(m.captured(2))
                    .arg(m.captured(3)));
      continue;
    }

    // 4) 普通日志
    appendLogLine(line);
  }
}

void BatchPage::updateStatsLabel() {
  m_stat->setText(tr("统计：total=%1  ok=%2  fail=%3")
                  .arg(m_sum.total).arg(m_sum.ok).arg(m_sum.fail));
}


void BatchPage::writeHistory(const QString& event,
                             const QString& folder,
                             int code,
                             QProcess::ExitStatus status)
{
    // 目录：<app>/logs/history.log
    const QString logDir = QCoreApplication::applicationDirPath() + "/logs";
    QDir().mkpath(logDir);
    const QString path = logDir + "/history.log";

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
        return;

    // 统一格式（易读、易解析）：时间 事件 路径 [可选状态]
    // START:  2025-10-29 10:20:11 START /data/folderA
    // END:    2025-10-29 10:25:02 END   /data/folderA code=0 status=normal
    const QString ts = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
    QTextStream out(&f);
    if (event == "START") {
        out << ts << " START " << folder << "\n";
    } else if (event == "END") {
        const char* stTxt = (status == QProcess::NormalExit ? "normal" : "crash");
        out << ts << " END   " << folder << " code=" << code << " status=" << stTxt << "\n";
    } else {
        out << ts << ' ' << event << ' ' << folder << "\n";
    }
}
