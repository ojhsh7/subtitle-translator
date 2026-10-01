#include "diagnostics.h"
#include "main_window.h"
#include "single_instance.h"
#include <QApplication>
#include <QElapsedTimer>
#include <QSettings>
#include <QTimer>
#include <QRegularExpression>
int main(int argc, char** argv) {
    QElapsedTimer startup;
    startup.start();
    QApplication app(argc, argv);
    app.setOrganizationName("PersonalTools");
    // QA and automated checks run the real executable with their own settings so the user's
    // saved region, languages and consent stay untouched. Only a short plain name is accepted.
    const QString profile = qEnvironmentVariable("SUBTITLE_TRANSLATOR_PROFILE");
    static const QRegularExpression safeProfile(QStringLiteral("^[A-Za-z0-9_-]{1,32}$"));
    app.setApplicationName(safeProfile.match(profile).hasMatch()
        ? QStringLiteral("SubtitleTranslator-") + profile : QStringLiteral("SubtitleTranslator"));
    app.setApplicationVersion("0.2.0");
    app.setFont(QFont("Malgun Gothic", 10));
    const bool smoke = app.arguments().contains("--smoke-test");
    // A second launch (desktop shortcut clicked again) hands its arguments to the running app.
    SingleInstance instance(app.applicationName());
    if (!smoke && !instance.claim(app.arguments().mid(1))) return 0;
    Diagnostics::Options logging;
    logging.directory = Diagnostics::defaultDirectory();
    logging.logText = QSettings().value("diagnostics/logText", false).toBool();
    if (!smoke) Diagnostics::init(logging);
    MainWindow window;
    QObject::connect(&instance, &SingleInstance::activated, &window, &MainWindow::activateFromLaunch);
    // Started with --auto and a saved region: stay minimized so the game keeps the screen.
    if (window.autoTranslating()) window.showMinimized();
    else window.show();
    qCInfo(stPerf, "startup-to-window ms=%lld", static_cast<long long>(startup.elapsed()));
    UiStallMonitor stalls;
    if (app.arguments().contains("--perf")) stalls.start(); // measurement runs only
    // Explicit smoke mode exercises the real UI event loop. It performs no OCR/network request.
    if (smoke) {
        QTimer::singleShot(900, &app, [&] {
            const int index = app.arguments().indexOf("--screenshot");
            if (index >= 0 && index + 1 < app.arguments().size())
                if (!window.grab().save(app.arguments().at(index + 1))) { app.exit(7); return; }
            app.exit(0);
        });
    }
    const int code = app.exec();
    if (stalls.isActive())
        qCInfo(stPerf, "ui-stall summary samples=%lld stalls=%d worst_ms=%lld", static_cast<long long>(stalls.samples()),
               stalls.stalls(), static_cast<long long>(stalls.worstMs()));
    Diagnostics::shutdown();
    return code;
}
