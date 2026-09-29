#include "translation_service.h"
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
QNetworkRequest deeplRequest(bool freeApi, const char* path, const QString& key) {
    QNetworkRequest request(QUrl(QString::fromLatin1(freeApi ? "https://api-free.deepl.com" : "https://api.deepl.com") + path));
    request.setRawHeader("Authorization", "DeepL-Auth-Key " + key.toUtf8());
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(30000);
    return request;
}
QString statusMessage(int status) {
    switch(status) {
    case 401: case 403: return QStringLiteral("DeepL 인증 실패. API 키와 Free/Pro 설정을 확인하세요.");
    case 429: return QStringLiteral("요청이 너무 많습니다. 잠시 후 직접 재시도하세요.");
    case 456: return QStringLiteral("DeepL 사용 한도에 도달했습니다. 계정 한도를 확인하세요.");
    case 413: return QStringLiteral("요청 크기가 너무 큽니다. 자막 영역을 줄이세요.");
    default:
        if(status >= 300 && status < 400) return QStringLiteral("번역 서버의 주소 이동 응답을 거부했습니다.");
        if(status >= 500) return QStringLiteral("번역 서버 오류입니다. 잠시 후 재시도하세요.");
        return QStringLiteral("예상하지 못한 번역 HTTP 응답입니다 (%1).").arg(status);
    }
}
}
TranslationService::TranslationService(QObject* parent) : QObject(parent), network_(this) {
    deadline_.setSingleShot(true);
    connect(&deadline_, &QTimer::timeout, this, [this] {
        failCurrent(QStringLiteral("번역 시간이 30초를 초과했습니다. 네트워크를 확인하세요."));
    });
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
        failCurrent(QStringLiteral("번역 응답이 허용 크기를 초과했습니다.")); return false;
    }
    return true;
}
QByteArray TranslationService::requestBody(const QString& text, const QString& targetLang, const QString& sourceLang) {
    QJsonObject object{{"text",QJsonArray{text}},{"target_lang",targetLang}};
    if (!sourceLang.isEmpty()) object.insert("source_lang", sourceLang);
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}
void TranslationService::translate(const QString& text, const QString& apiKey, bool freeApi,
                                   const QString& targetLang, const QString& sourceLang) {
    if (isBusy()) return;
    const QString key = apiKey.trimmed();
    if (!validKey(key)) {
        emit failed(QStringLiteral("올바른 DeepL API 키를 설정하세요.")); return;
    }
    if (text.trimmed().isEmpty() || text.toUtf8().size() > 32768) {
        emit failed(QStringLiteral("번역할 텍스트가 비어 있거나 너무 깁니다.")); return;
    }
    if (!validLanguage(targetLang) || (!sourceLang.isEmpty() && !validLanguage(sourceLang))) {
        emit failed(QStringLiteral("지원하지 않는 번역 언어 설정입니다.")); return;
    }
    const QByteArray body = requestBody(text, targetLang, sourceLang);
    if (body.size() >= 128*1024) { emit failed(QStringLiteral("번역 요청 크기가 너무 큽니다.")); return; }
    QNetworkRequest request = deeplRequest(freeApi, "/v2/translate", key);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    start(network_.post(request,body), Kind::Translate);
}
void TranslationService::checkUsage(const QString& apiKey, bool freeApi) {
    if (isBusy()) return;
    const QString key = apiKey.trimmed();
    if (!validKey(key)) {
        emit failed(QStringLiteral("올바른 DeepL API 키를 설정하세요.")); return;
    }
    start(network_.get(deeplRequest(freeApi, "/v2/usage", key)), Kind::Usage);
}
void TranslationService::start(QNetworkReply* reply, Kind kind) {
    response_.clear();
    reply_ = reply;
    reply->setReadBufferSize(MaxResponseBytes + 1);
    connect(reply,&QNetworkReply::readyRead,this,[this,reply] { if(reply_ == reply) readAvailable(); });
    connect(reply,&QNetworkReply::metaDataChanged,this,[this,reply] {
        if(reply_ == reply && reply->header(QNetworkRequest::ContentLengthHeader).toLongLong() > MaxResponseBytes)
            failCurrent(QStringLiteral("번역 응답이 허용 크기를 초과했습니다."));
    });
    connect(reply,&QNetworkReply::finished,this,[this,reply,kind] {
        if(reply_ != reply || !readAvailable()) return;
        deadline_.stop();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const auto networkError = reply->error();
        const QByteArray body = response_;
        reply_ = nullptr; response_.clear(); reply->deleteLater();
        // An HTTP error status is reported as-is; a transport error only matters without one.
        if((status == 0 || status == 200) && networkError != QNetworkReply::NoError) {
            emit failed(networkError == QNetworkReply::TimeoutError
                ? QStringLiteral("번역 연결 시간이 초과되었습니다.")
                : QStringLiteral("번역 서버에 안전하게 연결하지 못했습니다. 네트워크와 시스템 날짜를 확인하세요."));
            return;
        }
        QString error;
        if(kind == Kind::Usage) {
            qint64 used = 0, limit = -1;
            if(decodeUsage(status,body,used,limit,error)) emit usageChecked(used,limit); else emit failed(error);
            return;
        }
        QString detected;
        const QString result = decodeResponse(status,body,error,&detected);
        if(!error.isEmpty()) emit failed(error); else emit translated(result,detected);
    });
    deadline_.start(30000);
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
        error = QStringLiteral("사용량 응답에 필수 필드가 없습니다."); return false;
    }
    used = count.toInteger();
    const auto max = object.value("character_limit");
    if(max.isDouble() && max.toInteger(-1) >= 0) limit = max.toInteger();
    return true;
}
QString TranslationService::decodeResponse(int status, const QByteArray& body, QString& error, QString* detectedLanguage) {
    error.clear();
    if(detectedLanguage) detectedLanguage->clear();
    if(status != 200) { error = statusMessage(status); return {}; }
    if(body.size() > MaxResponseBytes) { error = QStringLiteral("번역 응답 크기 초과."); return {}; }
    QJsonParseError parse;
    const auto document = QJsonDocument::fromJson(body,&parse);
    if(parse.error != QJsonParseError::NoError || !document.isObject()) {
        error = QStringLiteral("번역 응답 JSON 형식 오류."); return {};
    }
    const auto array = document.object().value("translations").toArray();
    if(array.size() != 1 || !array.at(0).isObject() || !array.at(0).toObject().value("text").isString()) {
        error = QStringLiteral("번역 응답에 필수 필드가 없습니다."); return {};
    }
    const auto result = array.at(0).toObject().value("text").toString();
    if(result.trimmed().isEmpty()) { error = QStringLiteral("서버가 빈 번역 결과를 반환했습니다."); return {}; }
    // Optional; only a well-formed language code is passed on to the UI.
    const QString detected = array.at(0).toObject().value("detected_source_language").toString();
    if(detectedLanguage && validLanguage(detected)) *detectedLanguage = detected;
    return result;
}
