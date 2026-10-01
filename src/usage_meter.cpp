#include "usage_meter.h"
#include <QDate>
#include <QLocale>
#include <QSettings>
namespace {
QString monthKey() { return QStringLiteral("usage/month/") + QDate::currentDate().toString(QStringLiteral("yyyy-MM")); }
}
UsageMeter::UsageMeter(QObject* parent) : QObject(parent) {}
qint64 UsageMeter::characters(const QString& text) {
    qint64 count = 0;
    for (qsizetype i = 0; i < text.size(); ++i) {
        if (text.at(i).isHighSurrogate() && i + 1 < text.size() && text.at(i + 1).isLowSurrogate()) ++i;
        ++count;
    }
    return count;
}
qint64 UsageMeter::characters(const QStringList& texts) {
    qint64 total = 0;
    for (const QString& text : texts) total += characters(text);
    return total;
}
qint64 UsageMeter::month() const { return QSettings().value(monthKey(), 0).toLongLong(); }
bool UsageMeter::capEnabled() const { return QSettings().value(QStringLiteral("usage/capEnabled"), false).toBool(); }
qint64 UsageMeter::cap() const {
    const qint64 value = QSettings().value(QStringLiteral("usage/sessionCap"), DefaultCap).toLongLong();
    return value > 0 ? value : DefaultCap;
}
void UsageMeter::setCap(bool enabled, qint64 limit) {
    QSettings settings;
    settings.setValue(QStringLiteral("usage/capEnabled"), enabled);
    settings.setValue(QStringLiteral("usage/sessionCap"), limit > 0 ? limit : DefaultCap);
    emit changed();
}
bool UsageMeter::allows(qint64 more) const { return !capEnabled() || session_ + more <= cap(); }
void UsageMeter::record(qint64 count) {
    if (count <= 0) return;
    session_ += count;
    QSettings settings;
    settings.setValue(monthKey(), settings.value(monthKey(), 0).toLongLong() + count);
    emit changed();
}
QString UsageMeter::summary() const {
    const QLocale locale;
    QString text = QStringLiteral("이번 세션 %1자 · 이번 달 %2자").arg(locale.toString(session_), locale.toString(month()));
    if (capEnabled()) text += QStringLiteral(" · 상한 %1자").arg(locale.toString(cap()));
    return text;
}
QString UsageMeter::capReachedMessage() const {
    const QLocale locale;
    return QStringLiteral("번역을 멈췄습니다 · 이번 세션 사용량 상한(%1자)에 닿았습니다 · 설정 → DeepL·사용량에서 상한을 바꿀 수 있습니다.")
        .arg(locale.toString(cap()));
}
