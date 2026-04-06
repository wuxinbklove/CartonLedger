#include <QApplication>

#include "app/MainWindow.h"

int main(int argc, char *argv[])
{
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("PrivateDev"));
    QCoreApplication::setApplicationName(QStringLiteral("CartonLedger"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    app.setApplicationDisplayName(QStringLiteral("Carton Ledger"));

    cartonledger::MainWindow window;
    window.show();
    return app.exec();
}
