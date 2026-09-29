#include "main_window.h"
#include <QApplication>
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
    MainWindow window;
    window.show();
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
