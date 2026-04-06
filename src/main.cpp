#include <QApplication>

#include "app/MainWindow.h"

int main(int argc, char *argv[])
{
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("PrivateDev"));
    QCoreApplication::setApplicationName(QStringLiteral("BoerCartonLedger"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    app.setApplicationDisplayName(QStringLiteral("博尔纸箱记账"));

    cartonledger::MainWindow window;
    window.show();
    return app.exec();
}
