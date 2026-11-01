#include "main_window.h"
#include <QApplication>
#include <QLocalServer>
#include <QLocalSocket>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
int main(int argc, char** argv) {
    QApplication app(argc, argv);
    app.setOrganizationName("PersonalTools");
    app.setApplicationName("SubtitleTranslator");
    app.setApplicationVersion("0.1.0");
    app.setFont(QFont("Malgun Gothic", 10));
    app.setWindowIcon(MainWindow::appIcon());
    // The app lives in the tray: closing the window, or the result window while it is hidden, must not end it.
    app.setQuitOnLastWindowClosed(false);
    // One app per Windows user: a second launch (a shortcut pressed twice, start-up plus a click) asks
    // the running one to show its window and exits, instead of a second tray icon fighting for the
    // same shortcuts.
    const QString instanceName = QStringLiteral("SubtitleTranslator-") + qEnvironmentVariable("USERNAME");
    const bool smoke = app.arguments().contains("--smoke-test");
    if (!smoke) {
        QLocalSocket running;
        running.connectToServer(instanceName);
        if (running.waitForConnected(500)) {
            running.write("show");
            running.waitForBytesWritten(500);
            return 0;
        }
        QLocalServer::removeServer(instanceName); // left behind by a crash
    }
    MainWindow window;
    QLocalServer instance;
    if (!smoke && instance.listen(instanceName))
        QObject::connect(&instance, &QLocalServer::newConnection, &window, [&] {
            while (QLocalSocket* other = instance.nextPendingConnection()) other->deleteLater();
            window.showFromTray();
        });
    // Started with --auto and a saved region: stay in the tray (or minimized) so the game keeps the screen.
    if (window.autoTranslating()) {
        if (!QSystemTrayIcon::isSystemTrayAvailable()) window.showMinimized();
    } else window.show();
    // Explicit smoke mode exercises the real UI event loop. It performs no OCR/network request.
    if (app.arguments().contains("--smoke-test")) {
        QTimer::singleShot(900, &app, [&] {
            const int index = app.arguments().indexOf("--screenshot");
            if (index >= 0 && index + 1 < app.arguments().size())
                if (!window.grab().save(app.arguments().at(index + 1))) { app.exit(7); return; }
            app.exit(0);
        });
    }
    return app.exec();
}
