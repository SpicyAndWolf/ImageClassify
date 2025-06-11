#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QFileDialog>
#include <QStringListModel>
#include <QMessageBox>
#include <QTreeView>
#include <QProcess>
#include <QCoreApplication>
#include <QFile>
#include <QDebug>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , folderListModel(new QStringListModel(this))
    , currentProcess(nullptr)
    , isUserTerminated(false)
{
    ui->setupUi(this);
    this->setWindowIcon(QIcon(":/res/icon.png"));

    // 连接按钮点击信号到槽函数
    connect(ui->selectFoldersBtn, &QPushButton::clicked, this, &MainWindow::onSelectFoldersBtnClicked);
    connect(ui->startClassifyBtn, &QPushButton::clicked, this, &MainWindow::onStartClassifyBtnClicked);
    connect(ui->stopClassifyBtn, &QPushButton::clicked, this, &MainWindow::onStopClassifyBtnClicked);

    // 初始状态下隐藏终止按钮
    ui->stopClassifyBtn->setVisible(false);
}

MainWindow::~MainWindow()
{
    delete ui;
}


void MainWindow::onSelectFoldersBtnClicked()
{
    // 使用QFileDialog以支持同时选择多个文件夹
    QFileDialog dialog(this);
    dialog.setFileMode(QFileDialog::Directory);
    dialog.setOption(QFileDialog::DontUseNativeDialog, true);
    dialog.setOption(QFileDialog::ShowDirsOnly, true);

    // 允许多选
    QListView *listView = dialog.findChild<QListView*>("listView");
    if (listView) {
        listView->setSelectionMode(QAbstractItemView::MultiSelection);
    }
    QTreeView *treeView = dialog.findChild<QTreeView*>();
    if (treeView) {
        treeView->setSelectionMode(QAbstractItemView::MultiSelection);
    }

    // 处理选中结果
    if (dialog.exec()) {
        QStringList selectedDirs = dialog.selectedFiles();
        for (const QString &dir : selectedDirs) {
            if (!selectedFolderPaths.contains(dir)) {
                selectedFolderPaths.append(dir);
                addFolderItemWithDeleteButton(dir);
            }
        }
        folderListModel->setStringList(selectedFolderPaths);

        if (selectedDirs.isEmpty()) {
            QMessageBox::information(this, tr("失败"), tr("未成功添加文件夹"));
        }
    }
}

void MainWindow::addFolderItemWithDeleteButton(const QString &folderPath)
{
    // 创建自定义列表项
    FolderListItem *folderItem = new FolderListItem(folderPath);

    // 连接删除信号
    connect(folderItem, &FolderListItem::deleteRequested, this, &MainWindow::onDeleteFolderItem);

    // 创建列表项并设置自定义widget
    QListWidgetItem *listItem = new QListWidgetItem();
    listItem->setSizeHint(folderItem->sizeHint());

    // 将列表项添加到列表中
    ui->selectedFoldersList->addItem(listItem);
    ui->selectedFoldersList->setItemWidget(listItem, folderItem);
}

void MainWindow::onDeleteFolderItem(const QString &folderPath)
{
    // 从selectedFolderPaths中移除
    selectedFolderPaths.removeAll(folderPath);

    // 从列表视图中移除对应的项
    for (int i = 0; i < ui->selectedFoldersList->count(); ++i) {
        QListWidgetItem *item = ui->selectedFoldersList->item(i);
        FolderListItem *widget = qobject_cast<FolderListItem*>(
            ui->selectedFoldersList->itemWidget(item));
        if (widget && widget->getFolderPath() == folderPath) {
            delete ui->selectedFoldersList->takeItem(i);
            break;
        }
    }
}

void MainWindow::onStartClassifyBtnClicked()
{
    // 如果当前有进程在运行，则终止它
    if (currentProcess && currentProcess->state() == QProcess::Running) {
        onStopClassifyBtnClicked();
        return;
    }

    // 检查是否选择了文件夹
    if (selectedFolderPaths.isEmpty()) {
        QMessageBox::warning(this, tr("Warning"), tr("请先选择要处理的文件夹"));
        return;
    }

    // 重置终止标志
    isUserTerminated = false;

    // 禁用按钮防止重复点击
    ui->startClassifyBtn->setEnabled(false);
    ui->startClassifyBtn->setText("处理中...");
    ui->stopClassifyBtn->setVisible(true);

    // 构建Python脚本路径
    QString scriptPath = QCoreApplication::applicationDirPath() + "/TitleOcr/ocr.py";

    // 构建venv中的Python解释器路径
    QString venvPythonPath = QCoreApplication::applicationDirPath() + "/venv/Scripts/python.exe";

    // 检查脚本文件是否存在
    if (!QFile::exists(scriptPath)) {
        QMessageBox::critical(this, tr("错误"), tr("找不到OCR脚本文件：") + scriptPath);
        ui->startClassifyBtn->setEnabled(true);
        ui->startClassifyBtn->setText("开始处理");
        return;
    }

    // 检查venv Python解释器是否存在
    if (!QFile::exists(venvPythonPath)) {
        QMessageBox::critical(this, tr("错误"), tr("找不到venv Python解释器：") + venvPythonPath);
        ui->startClassifyBtn->setEnabled(true);
        ui->startClassifyBtn->setText("开始处理");
        return;
    }

    // 创建QProcess来执行Python脚本
    currentProcess = new QProcess(this);

    // 构建命令参数
    QStringList arguments;
    arguments << scriptPath;
    arguments << selectedFolderPaths;  // 将所有选中的文件夹路径作为参数

    // 连接进程完成信号
    connect(currentProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            [this](int exitCode, QProcess::ExitStatus exitStatus) {
                // 恢复按钮状态
                resetButtonStates();

                if (exitStatus == QProcess::NormalExit && exitCode == 0) {
                    QMessageBox::information(this, tr("完成"), tr("图片分类处理完成！"));
                }
                else if (exitStatus == QProcess::CrashExit) {
                    QMessageBox::information(this, tr("已终止"), tr("处理已被用户终止"));
                }
                else {
                    QString errorMsg = tr("处理过程中出现错误：\n") + currentProcess->readAllStandardError();
                    QMessageBox::critical(this, tr("错误"), errorMsg);
                    qDebug()<<errorMsg;
                }

                // 清理进程对象
                currentProcess->deleteLater();
                currentProcess = nullptr;
            });

    // 连接错误信号
    connect(currentProcess, &QProcess::errorOccurred,
            [this](QProcess::ProcessError error) {
                // 重置按钮状态
                resetButtonStates();

                // 用户主动终止，不显示错误
                if (isUserTerminated) {
                    return;
                }

                QString errorMsg;
                switch (error) {
                case QProcess::FailedToStart:
                    errorMsg = tr("无法启动Python进程，请确保已安装Python并配置了环境变量");
                    break;
                default:
                    errorMsg = tr("进程执行出错");
                    break;
                }

                QMessageBox::critical(this, tr("错误"), errorMsg);
                qDebug()<<errorMsg;
                currentProcess->deleteLater();
                currentProcess = nullptr;
            });

    // 启动Python脚本
    currentProcess->start(venvPythonPath, arguments);
}

void MainWindow::onStopClassifyBtnClicked()
{
    if (currentProcess && currentProcess->state() == QProcess::Running) {
        // 询问用户是否确认终止
        int ret = QMessageBox::question(this, tr("确认终止"),
                                       tr("确定要终止当前的处理任务吗？"),
                                       QMessageBox::Yes | QMessageBox::No,
                                       QMessageBox::No);

        if (ret == QMessageBox::Yes) {
            // 设置用户终止标志
            isUserTerminated = true;

            // 首先尝试正常终止
            currentProcess->terminate();

            // 等待进程结束（最多等待2秒）
            if (!currentProcess->waitForFinished(2000)) {
                // 如果进程没有正常结束，强制终止
                currentProcess->kill();
            }


            // 重置按钮状态
            resetButtonStates();
        }
    }
}

void MainWindow::resetButtonStates()
{
    ui->startClassifyBtn->setEnabled(true);
    ui->startClassifyBtn->setText("开始处理");
    ui->stopClassifyBtn->setVisible(false);
}
