#include "translation_service.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
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
void TranslationService::translate(const QString& text, const QString& apiKey, bool freeApi) {
    if (isBusy()) return;
    const QString key = apiKey.trimmed();
    if (key.isEmpty() || key.size() > 512 || key.contains('\r') || key.contains('\n')) {
        emit failed(QStringLiteral("올바른 DeepL API 키를 설정하세요.")); return;
    }
    if (text.trimmed().isEmpty() || text.toUtf8().size() > 32768) {
        emit failed(QStringLiteral("번역할 텍스트가 비어 있거나 너무 깁니다.")); return;
    }
    const QJsonObject object{{"text",QJsonArray{text}},{"source_lang","JA"},{"target_lang","KO"}};
    const QByteArray body = QJsonDocument(object).toJson(QJsonDocument::Compact);
    if (body.size() >= 128*1024) { emit failed(QStringLiteral("번역 요청 크기가 너무 큽니다.")); return; }
    QNetworkRequest request(QUrl(freeApi ? "https://api-free.deepl.com/v2/translate" : "https://api.deepl.com/v2/translate"));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Authorization", "DeepL-Auth-Key " + key.toUtf8());
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(30000);
    response_.clear();
    auto* reply = network_.post(request,body); reply_ = reply;
    reply->setReadBufferSize(MaxResponseBytes + 1);
    connect(reply,&QNetworkReply::readyRead,this,[this,reply] { if(reply_ == reply) readAvailable(); });
    connect(reply,&QNetworkReply::metaDataChanged,this,[this,reply] {
        if(reply_ == reply && reply->header(QNetworkRequest::ContentLengthHeader).toLongLong() > MaxResponseBytes)
            failCurrent(QStringLiteral("번역 응답이 허용 크기를 초과했습니다."));
    });
    connect(reply,&QNetworkReply::finished,this,[this,reply] {
        if(reply_ != reply || !readAvailable()) return;
        deadline_.stop();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const auto networkError = reply->error();
        const QByteArray body = response_;
        reply_ = nullptr; response_.clear(); reply->deleteLater();
        QString error, result;
        if(status != 0 && status != 200) result = decodeResponse(status,body,error);
        else if(networkError != QNetworkReply::NoError) {
            error = networkError == QNetworkReply::TimeoutError
                ? QStringLiteral("번역 연결 시간이 초과되었습니다.")
                : QStringLiteral("번역 서버에 안전하게 연결하지 못했습니다. 네트워크와 시스템 날짜를 확인하세요.");
        } else result = decodeResponse(status,body,error);
        if(!error.isEmpty()) emit failed(error); else emit translated(result);
    });
    deadline_.start(30000);
}
QString TranslationService::decodeResponse(int status, const QByteArray& body, QString& error) {
    error.clear();
    if(status != 200) {
        switch(status) {
        case 401: case 403: error = QStringLiteral("DeepL 인증 실패. API 키와 Free/Pro 설정을 확인하세요."); break;
        case 429: error = QStringLiteral("요청이 너무 많습니다. 잠시 후 직접 재시도하세요."); break;
        case 456: error = QStringLiteral("DeepL 사용 한도에 도달했습니다. 계정 한도를 확인하세요."); break;
        case 413: error = QStringLiteral("요청 크기가 너무 큽니다. 자막 영역을 줄이세요."); break;
        default:
            if(status >= 300 && status < 400) error = QStringLiteral("번역 서버의 주소 이동 응답을 거부했습니다.");
            else if(status >= 500) error = QStringLiteral("번역 서버 오류입니다. 잠시 후 재시도하세요.");
            else error = QStringLiteral("예상하지 못한 번역 HTTP 응답입니다 (%1).").arg(status);
        }
        return {};
    }
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
    return result;
}
