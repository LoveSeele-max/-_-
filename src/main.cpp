#include "mainwindow.h"

#include <QApplication>
#include <QMetaType>
#include <QNetworkProxy>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    QNetworkProxy::setApplicationProxy(QNetworkProxy::NoProxy);
    qRegisterMetaType<qint64>("qint64");

    MainWindow window;
    window.show();

    return app.exec();
}
