#include "mainwindow.h"

#include <QApplication>
#include <QSslSocket>
#include <QStyleFactory>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("BB Installer"));
    QApplication::setApplicationVersion(QStringLiteral(BBINSTALLER_VERSION));
    // Same look on every OS; the stylesheet supplies the colours
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    // The phone needs TLS 1.0, which only the OpenSSL backend can still be talked into
    // (Schannel on current Windows refuses it). Must be picked before the first TLS socket.
    if (QSslSocket::availableBackends().contains(QStringLiteral("openssl")))
        QSslSocket::setActiveBackend(QStringLiteral("openssl"));

    MainWindow w;
    w.show();
    return app.exec();
}
