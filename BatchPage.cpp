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
    m_log->append(QStringLiteral("[stderr] %1").arg(chunk.trimmed()));
  });
  connect(m_proc, QOverload<int,QProcess::ExitStatus>::of(&QProcess::finished),
          [this](int code, QProcess::ExitStatus st){
    m_log->append(QStringLiteral("进程结束：%1（%2）").arg(code).arg(st==QProcess::NormalExit?"normal":"crash"));
    startNextJob();
  });
  connect(m_proc, &QProcess::errorOccurred, [this](QProcess::ProcessError e){
    m_log->append(QStringLiteral("[错误] QProcess error=%1, errStr=%2")
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
    if (!dir.isEmpty()) m_list->addItem(new QListWidgetItem(dir, m_list));
  });
  connect(m_btnClear,&QPushButton::clicked, [this]{ m_list->clear(); });
  connect(m_btnStart,&QPushButton::clicked, this, &BatchPage::startRun);
  connect(m_btnStop, &QPushButton::clicked,  this, &BatchPage::stopRun);
}

void BatchPage::setActivated(bool on) {
  for (auto* w : findChildren<QWidget*>()) w->setEnabled(on);
}

void BatchPage::setRunnerConfig(const RunnerConfig& cfg) { m_cfg = cfg; }

QStringList BatchPage::selectedFolders() const {
  QStringList dirs;
  for (int i=0;i<m_list->count();++i) dirs << m_list->item(i)->text();
  return dirs;
}

void BatchPage::startRun() {
  if (m_cfg.pythonExe.isEmpty() || m_cfg.scriptPath.isEmpty() || m_cfg.outputDir.isEmpty()) {
    m_log->append(tr("[错误] 运行配置不完整（pythonExe/scriptPath/outputDir）"));
    return;
  }
  if (!QFile::exists(m_cfg.pythonExe)) {
    m_log->append(tr("[错误] 找不到 python 可执行文件：%1").arg(m_cfg.pythonExe));
    return;
  }
  if (!QFile::exists(m_cfg.scriptPath)) {
    m_log->append(tr("[错误] 找不到脚本：%1").arg(m_cfg.scriptPath));
    return;
  }

  const auto dirs = selectedFolders();
  if (dirs.isEmpty()) { m_log->append(tr("请先添加至少一个文件夹")); return; }

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
  m_log->append(tr("已停止"));
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

void BatchPage::startNextJob() {
  if (m_queue.isEmpty()) {
    m_log->append(tr("全部任务完成"));
    resetUiForRun(false);
    m_progress->setValue(100);
    return;
  }

  m_current = m_queue.dequeue();
  m_log->append(QStringLiteral("开始处理：%1").arg(m_current));
  m_progress->setValue(0);

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
  auto logDbg = [&](const QString& s){ if (m_verbose) m_log->append(s); };
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
    m_log->append(tr("[错误] 无法启动进程：%1").arg(m_proc->errorString()));
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

    // 2) STAT total/ok/fail
    m = m_rxStat.match(line);
    if (m.hasMatch()) {
      const QString s = m.captured(1);
      qint64 total=0, ok=0, fail=0;
      const auto parts = s.split(QRegularExpression("[,\\s]+"), Qt::SkipEmptyParts);
      for (const auto& kv : parts) {
        const auto pair = kv.split('=');
        if (pair.size()!=2) continue;
        const QString k = pair[0].trimmed().toLower();
        const qint64  v = pair[1].trimmed().toLongLong();
        if (k=="total") total=v;
        else if (k=="ok" || k=="processed") ok=v;
        else if (k=="fail") fail=v;
      }
      m_sum.total += total;
      m_sum.ok    += ok;
      m_sum.fail  += fail;
      updateStatsLabel();
      continue;
    }

    // 3) FINAL_STATISTICS（兼容你旧版）
    m = m_rxFinal.match(line);
    if (m.hasMatch()) {
      const qint64 total = m.captured(1).toLongLong();
      const qint64 processed = m.captured(2).toLongLong();
      // categories 不计入 ok/fail，但可打印
      const qint64 categories = m.captured(3).toLongLong();
      m_sum.total += total;
      m_sum.ok    += processed;
      // 若你有失败数，可由 total-processed 推算（按需）
      m_sum.fail  += qMax<qint64>(0, total - processed);
      m_log->append(QStringLiteral("分类完成：total=%1 processed=%2 categories=%3")
                    .arg(total).arg(processed).arg(categories));
      updateStatsLabel();
      continue;
    }

    // 4) 普通日志
    m_log->append(line);
  }
}

void BatchPage::updateStatsLabel() {
  m_stat->setText(tr("统计：total=%1  ok=%2  fail=%3")
                  .arg(m_sum.total).arg(m_sum.ok).arg(m_sum.fail));
}
