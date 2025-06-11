#include "folderlistitem.h"
#include "ui_folderlistitem.h"
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QFileInfo>

FolderListItem::FolderListItem(const QString &folderPath, QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::FolderListItem)
    , folderPath(folderPath)
{
    ui->setupUi(this);

    // 设置标签内容
    ui->folderPathLabel->setText(folderPath);
    ui->folderPathLabel->setToolTip(folderPath);

    setupConnections();
}

FolderListItem::~FolderListItem(){
    delete ui;
}

QString FolderListItem::getFolderPath() const
{
    return folderPath;
}

void FolderListItem::setupConnections()
{
    // 连接删除按钮的点击信号
    connect(ui->deleteBtn, &QPushButton::clicked, this, &FolderListItem::onDeleteButtonClicked);
}


void FolderListItem::onDeleteButtonClicked()
{
    // 发射删除信号
    emit deleteRequested(folderPath);
}
