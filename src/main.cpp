#include "main_window.h"
#include <QApplication>
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
    MainWindow window;
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
