#include "translation_service.h"
#include <QSslConfiguration>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
namespace {
bool validKey(const QString& key) {
    return !key.isEmpty() && key.size() <= 512 && !key.contains('\r') && !key.contains('\n');
}
bool validLanguage(const QString& code) {
    static const QRegularExpression pattern(QStringLiteral("^[A-Z]{2}(-[A-Z]{2,4})?$"));
    return pattern.match(code).hasMatch();
}
QString statusMessage(int status) {
    switch(status) {
    case 401: case 403: return QStringLiteral("DeepL 인증 실패. API 키와 Free/Pro 설정을 확인하세요.");
    case 429: return QStringLiteral("DeepL 요청이 너무 많아요.");
    case 456: return QStringLiteral("DeepL 사용 한도에 도달했어요. 계정 한도를 확인하세요.");
    case 413: return QStringLiteral("요청 크기가 너무 커요. 자막 영역을 줄이세요.");
    default:
        if(status >= 300 && status < 400) return QStringLiteral("번역 서버의 주소 이동 응답을 거부했어요.");
        if(status >= 500) return QStringLiteral("번역 서버 오류이에요. 잠시 후 재시도하세요.");
        return QStringLiteral("예상하지 못한 번역 HTTP 응답이에요 (%1).").arg(status);
    }
}
}
TranslationService::TranslationService(QObject* parent) : QObject(parent), network_(this) {
    deadline_.setSingleShot(true);
    connect(&deadline_, &QTimer::timeout, this, [this] {
        failCurrent(QStringLiteral("번역 시간이 %1초를 초과했어요. 네트워크를 확인하세요.").arg(timeoutMs_ / 1000));
    });
}
QNetworkRequest TranslationService::request(bool freeApi, const char* path, const QString& key) const {
    const QString base = testEndpoint_.isValid() ? testEndpoint_.toString()
                       : QString::fromLatin1(freeApi ? "https://api-free.deepl.com" : "https://api.deepl.com");
    QNetworkRequest request(QUrl(base + QLatin1String(path)));
    request.setRawHeader("Authorization", "DeepL-Auth-Key " + key.toUtf8());
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    // The transfer limit is a little longer, so the clearer deadline message wins.
    request.setTransferTimeout(timeoutMs_ + 1000);
    return request;
}
TranslationService::~TranslationService() { cancel(); }
void TranslationService::cancel() {
    deadline_.stop();
    auto* old = reply_; reply_ = nullptr; response_.clear();
    if (old) { disconnect(old,nullptr,this,nullptr); old->abort(); old->deleteLater(); }
}
void TranslationService::failCurrent(const QString& error) {
    if (!reply_) return;
    cancel(); emit failed(error);
}
bool TranslationService::readAvailable() {
    if (!reply_) return false;
    response_ += reply_->read(MaxResponseBytes - response_.size() + 1);
    if (response_.size() > MaxResponseBytes) {
        failCurrent(QStringLiteral("번역 응답이 허용 크기를 초과했어요.")); return false;
    }
    return true;
}
QByteArray TranslationService::requestBody(const QStringList& texts, const QString& targetLang, const QString& sourceLang) {
    QJsonObject object{{"text",QJsonArray::fromStringList(texts)},{"target_lang",targetLang}};
    if (!sourceLang.isEmpty()) object.insert("source_lang", sourceLang);
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}
void TranslationService::translate(const QStringList& texts, const QString& apiKey, bool freeApi,
                                   const QString& targetLang, const QString& sourceLang) {
    if (isBusy()) return;
    const QString key = apiKey.trimmed();
    if (!validKey(key)) {
        emit failed(QStringLiteral("올바른 DeepL API 키를 설정하세요.")); return;
    }
    qsizetype bytes = 0;
    for (const QString& text : texts) {
        if (text.trimmed().isEmpty()) { bytes = -1; break; }
        bytes += text.toUtf8().size();
    }
    if (texts.isEmpty() || texts.size() > MaxTexts || bytes < 0 || bytes > 32768) {
        emit failed(QStringLiteral("번역할 텍스트가 비어 있거나 너무 길어요.")); return;
    }
    if (!validLanguage(targetLang) || (!sourceLang.isEmpty() && !validLanguage(sourceLang))) {
        emit failed(QStringLiteral("지원하지 않는 번역 언어 설정이에요.")); return;
    }
    const QByteArray body = requestBody(texts, targetLang, sourceLang);
    if (body.size() >= 128*1024) { emit failed(QStringLiteral("번역 요청 크기가 너무 커요.")); return; }
    QNetworkRequest post = request(freeApi, "/v2/translate", key);
    post.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    expected_ = texts.size();
    start(network_.post(post,body), Kind::Translate);
}
void TranslationService::checkUsage(const QString& apiKey, bool freeApi) {
    if (isBusy()) return;
    const QString key = apiKey.trimmed();
    if (!validKey(key)) {
        emit failed(QStringLiteral("올바른 DeepL API 키를 설정하세요.")); return;
    }
    start(network_.get(request(freeApi, "/v2/usage", key)), Kind::Usage);
}
bool TranslationService::warmUp(bool freeApi) {
    if (lastUse_.isValid() && lastUse_.elapsed() < WarmAfterIdleMs) return false;
    lastUse_.restart();
    if (testEndpoint_.isValid()) {
        network_.connectToHost(testEndpoint_.host(), quint16(testEndpoint_.port(80)));
        return true;
    }
    // ALPN with h2: the server speaks HTTP/2, and requests then share this connection.
    QSslConfiguration ssl = QSslConfiguration::defaultConfiguration();
    ssl.setAllowedNextProtocols({QSslConfiguration::ALPNProtocolHTTP2, QSslConfiguration::NextProtocolHttp1_1});
    network_.connectToHostEncrypted(QString::fromLatin1(freeApi ? "api-free.deepl.com" : "api.deepl.com"), 443, ssl);
    return true;
}
void TranslationService::start(QNetworkReply* reply, Kind kind) {
    lastUse_.restart();
    response_.clear();
    lastStatus_ = 0; lastRetryAfter_ = -1;
    reply_ = reply;
    reply->setReadBufferSize(MaxResponseBytes + 1);
    connect(reply,&QNetworkReply::readyRead,this,[this,reply] { if(reply_ == reply) readAvailable(); });
    connect(reply,&QNetworkReply::metaDataChanged,this,[this,reply] {
        if(reply_ == reply && reply->header(QNetworkRequest::ContentLengthHeader).toLongLong() > MaxResponseBytes)
            failCurrent(QStringLiteral("번역 응답이 허용 크기를 초과했어요."));
    });
    connect(reply,&QNetworkReply::finished,this,[this,reply,kind] {
        if(reply_ != reply || !readAvailable()) return;
        deadline_.stop();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        bool retryAfterOk = false;
        const int retryAfter = reply->rawHeader("Retry-After").trimmed().toInt(&retryAfterOk);
        lastStatus_ = status; lastRetryAfter_ = retryAfterOk && retryAfter >= 0 ? retryAfter : -1;
        const auto networkError = reply->error();
        const QByteArray body = response_;
        reply_ = nullptr; response_.clear(); reply->deleteLater();
        // An HTTP error status is reported as-is; a transport error only matters without one.
        if((status == 0 || status == 200) && networkError != QNetworkReply::NoError) {
            emit failed(networkError == QNetworkReply::TimeoutError
                ? QStringLiteral("번역 연결 시간이 초과되었어요.")
                : QStringLiteral("번역 서버에 안전하게 연결하지 못했어요. 네트워크와 시스템 날짜를 확인하세요."));
            return;
        }
        QString error;
        if(kind == Kind::Usage) {
            qint64 used = 0, limit = -1;
            if(decodeUsage(status,body,used,limit,error)) emit usageChecked(used,limit); else emit failed(error);
            return;
        }
        QString detected;
        const QStringList results = decodeResponses(status,body,expected_,error,&detected);
        if(!error.isEmpty()) emit failed(error); else emit translated(results,detected);
    });
    deadline_.start(timeoutMs_);
}
bool TranslationService::decodeUsage(int status, const QByteArray& body, qint64& used, qint64& limit, QString& error) {
    error.clear(); used = 0; limit = -1;
    if(status != 200) { error = statusMessage(status); return false; }
    if(body.size() > MaxResponseBytes) { error = QStringLiteral("사용량 응답 크기 초과."); return false; }
    QJsonParseError parse;
    const auto document = QJsonDocument::fromJson(body,&parse);
    if(parse.error != QJsonParseError::NoError || !document.isObject()) {
        error = QStringLiteral("사용량 응답 JSON 형식 오류."); return false;
    }
    const auto object = document.object();
    const auto count = object.value("character_count");
    if(!count.isDouble() || count.toInteger(-1) < 0) {
        error = QStringLiteral("사용량 응답에 필수 필드가 없어요."); return false;
    }
    used = count.toInteger();
    const auto max = object.value("character_limit");
    if(max.isDouble() && max.toInteger(-1) >= 0) limit = max.toInteger();
    return true;
}
QStringList TranslationService::decodeResponses(int status, const QByteArray& body, qsizetype expected, QString& error,
                                                QString* detectedLanguage) {
    error.clear();
    if(detectedLanguage) detectedLanguage->clear();
    if(status != 200) { error = statusMessage(status); return {}; }
    if(body.size() > MaxResponseBytes) { error = QStringLiteral("번역 응답 크기 초과."); return {}; }
    QJsonParseError parse;
    const auto document = QJsonDocument::fromJson(body,&parse);
    if(parse.error != QJsonParseError::NoError || !document.isObject()) {
        error = QStringLiteral("번역 응답 JSON 형식 오류."); return {};
    }
    // One translation per requested piece, in the same order.
    const auto array = document.object().value("translations").toArray();
    if(expected < 1 || array.size() != expected) { error = QStringLiteral("번역 응답에 필수 필드가 없어요."); return {}; }
    QStringList results;
    for(const auto& item : array) {
        if(!item.isObject() || !item.toObject().value("text").isString()) {
            error = QStringLiteral("번역 응답에 필수 필드가 없어요."); return {};
        }
        const QString text = item.toObject().value("text").toString();
        if(text.trimmed().isEmpty()) { error = QStringLiteral("서버가 빈 번역 결과를 반환했어요."); return {}; }
        results << text;
    }
    // Optional; only a well-formed language code is passed on to the UI.
    const QString detected = array.at(0).toObject().value("detected_source_language").toString();
    if(detectedLanguage && validLanguage(detected)) *detectedLanguage = detected;
    return results;
}
QString TranslationService::decodeResponse(int status, const QByteArray& body, QString& error, QString* detectedLanguage) {
    const QStringList results = decodeResponses(status, body, 1, error, detectedLanguage);
    return results.isEmpty() ? QString() : results.first();
}
