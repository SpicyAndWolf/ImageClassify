#pragma once
#include <QWidget>
#include <QProcess>
#include <QQueue>
#include <QRegularExpression>
#include <QProcessEnvironment>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QTextStream>

class BatchPage : public QWidget {
  Q_OBJECT
public:
  explicit BatchPage(QWidget* parent=nullptr);

  void setActivated(bool on);

  struct RunnerConfig {
    // 通用
    QString pythonExe;     // 必填：python.exe 绝对路径（embed 或 venv）
    QString scriptPath;    // 必填：ocr.py 绝对路径
    QString outputDir;     // 必填：结果目录
    QStringList extraArgs; // 可选：其他参数

    // embed 专属（Windows 便携版 Python）
    // 若设置了 embedDir，则会额外注入 PATH/PYTHONHOME/PYTHONPATH，并将工作目录切到脚本目录
    QString embedDir;      // 形如：<appDir>/python-embed
  };
  void setRunnerConfig(const RunnerConfig& cfg);
  RunnerConfig getRunnerConfig() const;

  QStringList selectedFolders() const;

public slots:
  void startRun();
  void stopRun();

signals:
  void startRequested(const QStringList& folders);
  void stopRequested();
  void jobStarted(const QString& folder);                        // 新增：任务开始（实时给 Dashboard）
  void jobFinished(const QString& folder, int code, int status); // 新增：任务结束（status=QProcess::ExitStatus）

private:
  void buildUi();
  void writeHistory(const QString& event, const QString& folder, int code = 0, QProcess::ExitStatus status = QProcess::NormalExit);
  void addFolderItem(const QString& dir);   // 插入一行（含删除按钮）
  bool hasFolder(const QString& dir) const; // 避免重复添加
  void enqueueJobs(const QStringList& folders);
  void startNextJob();
  void resetUiForRun(bool running);
  void parseStdoutLines(const QString& chunk);
  void updateStatsLabel();
  QProcessEnvironment buildEnvForEmbed(const QString& embedDir, const QProcessEnvironment& baseEnv) const;

  // UI
  class QListWidget*   m_list;
  class QPushButton*   m_btnAdd;
  class QPushButton*   m_btnClear;
  class QPushButton*   m_btnStart;
  class QPushButton*   m_btnStop;
  class QProgressBar*  m_progress;
  class QTextEdit*     m_log;
  class QLabel*        m_stat;

  // 进程与队列
  QProcess*       m_proc;
  QQueue<QString> m_queue;
  QString         m_current;
  RunnerConfig    m_cfg;

  // 日志缓冲与正则
  QString m_outBuf;
  QRegularExpression m_rxProgress; // ^PROGRESS\s+(\d+)\%
  QRegularExpression m_rxStat;     // ^STAT\b(.*)$
  QRegularExpression m_rxFinal;    // ^FINAL_STATISTICS:\s*TOTAL=(\d+),\s*PROCESSED=(\d+),\s*CATEGORIES=(\d+)
  bool m_verbose = false; //是否开启调试
  void appendLogLine(const QString& line);

  struct Stats { qint64 total=0, ok=0, fail=0; } m_sum;
};
