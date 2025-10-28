#include "mainwindow.h"
#include <QApplication>
#include <QFile>
#include <QDebug>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    // 设置样式
    QFile file(":/style/theme.qss");
    if (!file.open(QFile::ReadOnly)) {
        qDebug() << "Failed to load QSS file.";
    } else {
        QString style = QLatin1String(file.readAll());
        qApp->setStyleSheet(style);
    }

    MainWindow w;
    w.show();
    return a.exec();
}
