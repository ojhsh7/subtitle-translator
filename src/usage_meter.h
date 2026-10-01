#pragma once
#include <QObject>
#include <QStringList>

// Characters sent for translation: this app session and this calendar month (saved under
// usage/month/<yyyy-MM>), plus an optional session cap that stops translation before it is passed.
// The count is the app's own estimate (Unicode characters of the sent text); DeepL's bill is the
// authority, and /v2/usage shows the account total.
class UsageMeter final : public QObject {
    Q_OBJECT
public:
    explicit UsageMeter(QObject* parent = nullptr);
    static qint64 characters(const QString& text);
    static qint64 characters(const QStringList& texts);
    qint64 session() const { return session_; }
    qint64 month() const;
    bool capEnabled() const;
    qint64 cap() const;
    void setCap(bool enabled, qint64 characters);
    static constexpr qint64 DefaultCap = 50000;
    // False when sending `more` characters would pass the session cap.
    bool allows(qint64 more) const;
    void record(qint64 characters);
    QString summary() const;         // "이번 세션 1,234자 · 이번 달 12,345자"
    QString capReachedMessage() const;
signals:
    void changed();
private:
    qint64 session_ = 0;
};
