#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QStringListModel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QProcess>
#include <QSettings>
#include "folderlistitem.h"

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void onSelectFoldersBtnClicked();
    void onDeleteFolderItem(const QString &folderPath);
    void onStartClassifyBtnClicked();
    void onStopClassifyBtnClicked();
    void onSelectResFolderBtnClicked();

private:
    Ui::MainWindow *ui;
    QStringListModel *folderListModel;
    QStringList selectedFolderPaths;
    QProcess *currentProcess; // 保存当前正在运行的进程，以便中途终止
    bool isUserTerminated;
    QString resFolderPath;  // 存储结果文件夹路径
    QSettings *settings;

    // 添加带删除按钮的列表项
    void addFolderItemWithDeleteButton(const QString &folderPath);
    void resetButtonStates();
    void loadSettings();
    void saveSettings();
};
#endif // MAINWINDOW_H
