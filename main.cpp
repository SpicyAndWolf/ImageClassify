#include "mainwindow.h"
#include <QApplication>
#include <QFile>
#include <QDebug>
#include <QIcon>
#include <QSslSocket>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    qDebug() << "SSL supported:" << QSslSocket::supportsSsl();
    qDebug() << "build:" << QSslSocket::sslLibraryBuildVersionString();
    qDebug() << "runtime:" << QSslSocket::sslLibraryVersionString();



    // 设置样式
    QFile file(":/style/theme.qss");
    if (!file.open(QFile::ReadOnly)) {
        qDebug() << "Failed to load QSS file.";
    } else {
        QString style = QLatin1String(file.readAll());
        qApp->setStyleSheet(style);
    }

    MainWindow w;
    w.setWindowIcon(QIcon(":/icons/icon.svg"));
    w.show();
    return a.exec();
}
