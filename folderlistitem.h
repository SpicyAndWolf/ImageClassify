#ifndef FOLDERLISTITEM_H
#define FOLDERLISTITEM_H

#include <QWidget>

QT_BEGIN_NAMESPACE
namespace Ui { class FolderListItem; }
QT_END_NAMESPACE

class FolderListItem : public QWidget
{
    Q_OBJECT

public:
    explicit FolderListItem(const QString &folderPath, QWidget *parent = nullptr);
    ~FolderListItem();

    // 获取文件夹路径
    QString getFolderPath() const;

signals:
    // 删除信号，传递文件夹路径
    void deleteRequested(const QString &folderPath);

private slots:
    void onDeleteButtonClicked();

private:
    Ui::FolderListItem *ui;
    QString folderPath;
    void setupConnections();
};

#endif // FOLDERLISTITEM_H
