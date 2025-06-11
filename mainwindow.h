#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QStringListModel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QProcess>
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

private:
    Ui::MainWindow *ui;
    QStringListModel *folderListModel;
    QStringList selectedFolderPaths;
    QProcess *currentProcess; // 保存当前正在运行的进程，以便中途终止
    bool isUserTerminated;

    // 添加带删除按钮的列表项
    void addFolderItemWithDeleteButton(const QString &folderPath);
    void resetButtonStates();
};
#endif // MAINWINDOW_H
