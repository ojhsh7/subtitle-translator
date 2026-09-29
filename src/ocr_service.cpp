#include "ocr_service.h"
#include <QBuffer>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QtConcurrent>
#include <QRegularExpression>
#include <QStringDecoder>
#define NOMINMAX
#include <windows.h>
#include <psapi.h>
OcrService::OcrService(QObject* parent) : QObject(parent) {
    deadline_.setSingleShot(true);
    connect(&deadline_, &QTimer::timeout, this, [this] {
        fail(QStringLiteral("OCR 처리 시간이 20초를 넘었습니다. 영역을 줄여 다시 시도하세요."));
    });
    memoryTimer_.setInterval(25);
    connect(&memoryTimer_, &QTimer::timeout, this, &OcrService::sample);
}
OcrService::~OcrService() { cancel(); }
void OcrService::sample() {
    if (!processHandle_) return;
    PROCESS_MEMORY_COUNTERS counters{};
    if (GetProcessMemoryInfo(static_cast<HANDLE>(processHandle_), &counters, sizeof(counters)))
        peakMemory_ = qMax(peakMemory_, quint64(counters.PeakWorkingSetSize));
}
void OcrService::metrics() {
    sample();
    if (elapsed_.isValid()) elapsedMs_ = elapsed_.elapsed();
}
void OcrService::cancel() {
    ++generation_;
    deadline_.stop(); memoryTimer_.stop(); metrics();
    if (processHandle_) { CloseHandle(static_cast<HANDLE>(processHandle_)); processHandle_ = nullptr; }
    busy_ = false;
    if (process_) {
        auto* old = process_; process_ = nullptr;
        disconnect(old, nullptr, this, nullptr);
        if (old->state() != QProcess::NotRunning) {
            connect(old, qOverload<int,QProcess::ExitStatus>(&QProcess::finished), old, &QObject::deleteLater);
            old->kill();
        } else old->deleteLater();
    }
    output_.clear(); errors_.clear();
}
void OcrService::fail(const QString& message) {
    const bool probe = probing_;
    cancel();
    if (probe) emit probeFinished(false, message);
    else emit failed(message);
}
void OcrService::recognize(const QImage& image, const QString& executable, const QString& data, int psm) {
    if (busy_) return;
    probing_ = false;
    if (image.isNull() || qint64(image.width()) * image.height() > 16000000) {
        emit failed(QStringLiteral("OCR 이미지가 비어 있거나 너무 큽니다.")); return;
    }
    if (psm != 6 && psm != 7) { emit failed(QStringLiteral("지원하지 않는 OCR 배치입니다.")); return; }
    busy_ = true; elapsed_.start(); peakMemory_ = 0; elapsedMs_ = 0;
    deadline_.start(20000);
    const auto ticket = ++generation_;
    auto* watcher = new QFutureWatcher<QByteArray>(this);
    connect(watcher, &QFutureWatcher<QByteArray>::finished, this, [this, watcher, ticket, executable, data, psm] {
        auto bytes = watcher->result(); watcher->deleteLater();
        if (!busy_ || generation_ != ticket) return;
        if (bytes.isEmpty() || bytes.size() > 32 * 1024 * 1024) {
            fail(QStringLiteral("이미지 인코딩에 실패했거나 이미지가 너무 큽니다.")); return;
        }
        QStringList args{"stdin", "stdout", "-l", "jpn", "--oem", "1", "--psm", QString::number(psm)};
        if (!data.isEmpty()) args << "--tessdata-dir" << data;
        launch(executable, args, std::move(bytes));
    });
    watcher->setFuture(QtConcurrent::run([image] {
        QByteArray bytes; QBuffer buffer(&bytes); buffer.open(QIODevice::WriteOnly);
        if (!image.save(&buffer, "PNG")) return QByteArray();
        return bytes;
    }));
}
void OcrService::probe(const QString& executable, const QString& data) {
    if (busy_) return;
    probing_ = true; busy_ = true; elapsed_.start(); elapsedMs_ = 0; peakMemory_ = 0;
    ++generation_; deadline_.start(20000);
    QStringList args{"--list-langs"};
    if (!data.isEmpty()) args << "--tessdata-dir" << data;
    launch(executable, args, {});
}
void OcrService::collect() {
    if (!process_) return;
    output_ += process_->readAllStandardOutput(); errors_ += process_->readAllStandardError();
    if (output_.size() > 1024 * 1024 || errors_.size() > 1024 * 1024)
        fail(QStringLiteral("OCR 출력이 허용 크기를 초과했습니다."));
}
void OcrService::launch(const QString& executable, const QStringList& args, QByteArray bytes) {
    if (!QFileInfo(executable).isFile()) { fail(QStringLiteral("Tesseract 실행 파일을 찾을 수 없습니다.")); return; }
    auto* p = new QProcess(this); process_ = p;
    output_.clear(); errors_.clear();
    p->setProcessChannelMode(QProcess::SeparateChannels);
    connect(p, &QProcess::started, this, [this, p, bytes] {
        if (process_ != p) return;
        processHandle_ = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, DWORD(p->processId()));
        sample(); memoryTimer_.start();
        if (!bytes.isEmpty() && p->write(bytes) != bytes.size()) { fail(QStringLiteral("OCR 이미지 전달에 실패했습니다.")); return; }
        p->closeWriteChannel();
    });
    connect(p, &QProcess::readyReadStandardOutput, this, &OcrService::collect);
    connect(p, &QProcess::readyReadStandardError, this, &OcrService::collect);
    connect(p, &QProcess::errorOccurred, this, [this, p](QProcess::ProcessError e) {
        if (process_ != p) return;
        if (e == QProcess::FailedToStart) fail(QStringLiteral("Tesseract를 시작하지 못했습니다. 실행 파일과 DLL을 확인하세요."));
        else if (e != QProcess::Crashed) fail(QStringLiteral("OCR 프로세스 통신 오류입니다."));
    });
    connect(p, qOverload<int,QProcess::ExitStatus>(&QProcess::finished), this,
        [this, p](int code, QProcess::ExitStatus exit) {
            if (process_ != p) return;
            collect();
            if (process_ != p) return;
            if (exit != QProcess::NormalExit || code != 0) {
                fail(QStringLiteral("OCR 실행 실패. 일본어 모델(jpn)과 tessdata 경로를 확인하세요.")); return;
            }
            const bool probe = probing_;
            QStringDecoder decoder(QStringDecoder::Utf8);
            const QString decoded = decoder(output_); const QString text = decoded.trimmed();
            if (decoder.hasError()) { fail(QStringLiteral("OCR 출력의 문자 인코딩을 읽지 못했습니다.")); return; }
            cancel();
            if (probe) {
                const bool japanese = text.split(QRegularExpression("[\\r\\n]+")).contains("jpn");
                emit probeFinished(japanese, japanese ? QStringLiteral("일본어 OCR 엔진과 jpn 모델을 확인했습니다.")
                    : QStringLiteral("jpn 모델이 없습니다. tessdata 폴더에 일본어 모델을 준비하세요."));
            } else if (text.isEmpty()) emit failed(QStringLiteral("글자를 인식하지 못했습니다. 영역과 글자 크기를 확인하세요."));
            else emit recognized(text);
        });
    p->start(executable, args, QIODevice::ReadWrite);
}
