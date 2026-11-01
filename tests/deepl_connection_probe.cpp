// P-31 measurement: how long a DeepL request takes through the app's TranslationService on a new
// connection, on a connection opened ahead with warmUp(), and on a connection just used.
// Sends a made-up key to /v2/usage: DeepL answers 403, so no account, key or character is involved.
#include "translation_service.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QTextStream>
#include <QTimer>
#include <algorithm>

namespace {
const QString FakeKey = QStringLiteral("00000000-0000-0000-0000-000000000000:fx");
// One /v2/usage round trip on `service`; milliseconds until it answered (failed with 403).
double roundTrip(TranslationService& service) {
    QEventLoop loop;
    QObject::connect(&service, &TranslationService::failed, &loop, &QEventLoop::quit);
    QObject::connect(&service, &TranslationService::usageChecked, &loop, &QEventLoop::quit);
    QElapsedTimer timer; timer.start();
    service.checkUsage(FakeKey, true);
    loop.exec();
    QObject::disconnect(&service, nullptr, &loop, nullptr);
    return timer.nsecsElapsed() / 1e6;
}
void pause(int ms) { QEventLoop loop; QTimer::singleShot(ms, &loop, &QEventLoop::quit); loop.exec(); }
double median(QList<double> v) { std::sort(v.begin(), v.end()); return v[v.size() / 2]; }
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    const int rounds = 5;
    QList<double> fresh, warmed, reused;
    for (int i = 0; i < rounds; ++i) {
        {
            TranslationService service;            // a new manager: no connection yet
            fresh << roundTrip(service);
            pause(1000);                            // the next line comes a moment later
            reused << roundTrip(service);           // on the connection just used
        }
        {
            TranslationService service;
            service.warmUp(true);                   // what the app does when a line starts to appear
            pause(1000);                            // the line settles and is read meanwhile
            warmed << roundTrip(service);
        }
    }
    QTextStream out(stdout);
    out << "new connection    median " << median(fresh) << " ms\n"
        << "opened by warmUp  median " << median(warmed) << " ms\n"
        << "reused connection median " << median(reused) << " ms\n";
    return 0;
}
