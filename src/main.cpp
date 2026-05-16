#include "mainwindow.h"

#include <QApplication>
#include <QMetaType>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    qRegisterMetaType<qint64>("qint64");

    MainWindow window;
    window.show();

    return app.exec();
}
