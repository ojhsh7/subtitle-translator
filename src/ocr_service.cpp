#include "ocr_service.h"
#include "languages.h"
#include "paddle_ocr.h"
#include <QBuffer>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QtConcurrent>
#include <QRegularExpression>
#include <QStringDecoder>
#include <algorithm>
#include <vector>
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
    // Attempts running side by side add up: the sum of their peaks bounds the memory in use.
    quint64 total = 0;
    for (const Job& job : jobs_) {
        PROCESS_MEMORY_COUNTERS counters{};
        if (job.handle && GetProcessMemoryInfo(static_cast<HANDLE>(job.handle), &counters, sizeof(counters)))
            total += counters.PeakWorkingSetSize;
    }
    peakMemory_ = qMax(peakMemory_, total);
}
void OcrService::metrics() {
    sample();
    if (elapsed_.isValid()) elapsedMs_ = elapsed_.elapsed();
}
void OcrService::cancel() {
    ++generation_;
    if (cancelled_) cancelled_->cancel(); // also after a finished read: harmless
    deadline_.stop(); metrics();
    busy_ = false;
    stopJobs();
}
void OcrService::stopJobs() {
    memoryTimer_.stop();
    for (Job& job : jobs_) {
        if (job.handle) CloseHandle(static_cast<HANDLE>(job.handle));
        QProcess* old = job.process;
        disconnect(old, nullptr, this, nullptr);
        if (old->state() != QProcess::NotRunning) {
            connect(old, qOverload<int,QProcess::ExitStatus>(&QProcess::finished), old, &QObject::deleteLater);
            old->kill();
        } else old->deleteLater();
    }
    jobs_.clear(); outputs_.clear();
}
OcrService::Job* OcrService::job(QProcess* process) {
    for (Job& job : jobs_) if (job.process == process) return &job;
    return nullptr;
}
void OcrService::fail(const QString& message) {
    const bool probe = probing_;
    cancel();
    if (probe) emit probeFinished(false, message);
    else emit failed(message);
}
int OcrService::resolvePsm(int psm, const QSize& imageSize) {
    if (psm != 0) return psm;
    // Block mode (PSM 6) sometimes repeats the first syllable of a lone line with several models loaded.
    return imageSize.height() <= SingleLineMaxHeight ? 7 : 6;
}
QImage OcrService::lightTextMask(const QImage& image, double* coverage, bool anyColour) {
    const QImage big = image.scaled(image.size() * 3, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                           .convertToFormat(QImage::Format_RGB32);
    QImage mask(big.size(), QImage::Format_Grayscale8);
    qint64 ink = 0;
    for (int y = 0; y < big.height(); ++y) {
        const auto* in = reinterpret_cast<const QRgb*>(big.constScanLine(y));
        uchar* out = mask.scanLine(y);
        for (int x = 0; x < big.width(); ++x) {
            const int r = qRed(in[x]), g = qGreen(in[x]), b = qBlue(in[x]);
            const int low = std::min({r, g, b}), high = std::max({r, g, b});
            // Bright and nearly colourless; anyColour also accepts ClearType-style colour fringes.
            const bool text = anyColour ? qGray(in[x]) >= 185 : low >= 185 && high - low <= 60;
            out[x] = text ? 0 : 255;
            ink += text;
        }
    }
    if (coverage) *coverage = mask.isNull() ? 0 : double(ink) / (qint64(mask.width()) * mask.height());
    return mask;
}
QImage OcrService::growInk(const QImage& mask) {
    QImage grown(mask.size(), QImage::Format_Grayscale8);
    const int w = mask.width(), h = mask.height();
    for (int y = 0; y < h; ++y) {
        uchar* out = grown.scanLine(y);
        for (int x = 0; x < w; ++x) {
            uchar darkest = 255; // 3x3 minimum: black ink spreads by one pixel
            for (int dy = std::max(0, y - 1); dy <= std::min(h - 1, y + 1); ++dy) {
                const uchar* row = mask.constScanLine(dy);
                for (int dx = std::max(0, x - 1); dx <= std::min(w - 1, x + 1); ++dx) darkest = std::min(darkest, row[dx]);
            }
            out[x] = darkest;
        }
    }
    return grown;
}
OcrService::Reading OcrService::parseOutput(const QString& output) {
    Reading reading;
    QStringList textLines;
    double weighted = 0;
    qsizetype letters = 0;
    static const QRegularExpression tsvRow(QStringLiteral("^(level|\\d+)\\t"));
    for (QString line : output.split('\n')) {
        if (line.endsWith('\r')) line.chop(1);
        const QStringList cols = line.split('\t');
        if (cols.size() == 12 && tsvRow.match(line).hasMatch()) {
            // Word rows (level 5) carry the confidence; the plain text below keeps the real spacing.
            const QString word = cols[11].trimmed();
            bool ok = false;
            const double confidence = cols[10].toDouble(&ok);
            if (cols[0] == QLatin1String("5") && !word.isEmpty() && ok && confidence >= 0) {
                weighted += confidence * word.size(); letters += word.size();
            }
            continue;
        }
        if (!line.trimmed().isEmpty()) textLines << line.trimmed();
    }
    reading.text = textLines.join('\n');
    reading.confidence = letters ? weighted / letters : 0;
    return reading;
}
QString OcrService::cleanText(const QString& text, bool fromTesseract) {
    static const QRegularExpression standalonePipe(QStringLiteral("(^|\\s)\\|(?=$|\\s|[A-Za-z'’?!.,])"));
    // Japanese and Chinese have no spaces between words; Tesseract sometimes inserts them.
    // Hangul is not in this class, so Korean word spacing is kept.
    static const QString cjk = QStringLiteral("\\p{Han}\\p{Hiragana}\\p{Katakana}\\x{3001}-\\x{303F}\\x{FF01}-\\x{FF60}");
    static const QRegularExpression cjkGap(QStringLiteral("(?<=[%1])[ \\t]+(?=[%1?!.,:;])").arg(cjk));
    QString cleaned = text;
    if (fromTesseract) cleaned.replace(standalonePipe, QStringLiteral("\\1I"));
    return cleaned.replace(cjkGap, QString());
}
bool OcrService::isUniformImage(const QImage& image) {
    if (image.isNull()) return false; // Invalid input has a separate error path.
    const QImage pixels = image.convertToFormat(QImage::Format_ARGB32);
    if (pixels.isNull()) return false;
    const QRgb first = reinterpret_cast<const QRgb*>(pixels.constScanLine(0))[0];
    for (int y = 0; y < pixels.height(); ++y) {
        const auto* row = reinterpret_cast<const QRgb*>(pixels.constScanLine(y));
        for (int x = 0; x < pixels.width(); ++x)
            if (row[x] != first) return false;
    }
    return true;
}
void OcrService::recognize(const QImage& image, const QString& executable, const QString& data, int psm,
                           const QString& languages) {
    if (busy_) return;
    probing_ = false;
    if (image.isNull() || qint64(image.width()) * image.height() > 16000000) {
        emit failed(QStringLiteral("OCR 이미지가 비어 있거나 너무 큽니다.")); return;
    }
    psm = resolvePsm(psm, image.size());
    if (psm != 6 && psm != 7) { emit failed(QStringLiteral("지원하지 않는 OCR 배치입니다.")); return; }
    if (!Languages::isOcrCode(languages)) { emit failed(QStringLiteral("지원하지 않는 OCR 언어입니다.")); return; }
    languages_ = languages; executable_ = executable;
    // Plain text (real spacing) and TSV (per-word confidence) in one run; -c avoids tessdata/configs files.
    arguments_ = QStringList{"stdin", "stdout", "-l", languages, "--oem", "1", "--psm", QString::number(psm),
                             "-c", "tessedit_create_tsv=1", "-c", "tessedit_create_txt=1", "-c", "page_separator="};
    if (!data.isEmpty()) arguments_ << "--tessdata-dir" << data;
    busy_ = true; elapsed_.start(); peakMemory_ = 0; elapsedMs_ = 0;
    attempts_.clear(); launched_ = 0; judged_ = 0; attemptsUsed_ = 0; best_ = {}; bestFromMask_ = false; engineUsed_.clear();
    paddleDetails_ = {}; tesseractConfidence_ = -1;
    deadline_.start(20000);
    const auto ticket = ++generation_;
    // Inspect full-resolution pixels off the UI thread before either OCR engine runs.
    // A completely flat image contains no text, but both engines can hallucinate short strings.
    auto* watcher = new QFutureWatcher<bool>(this);
    connect(watcher, &QFutureWatcher<bool>::finished, this, [this, watcher, image, ticket] {
        const bool blank = watcher->result(); watcher->deleteLater();
        if (!busy_ || generation_ != ticket) return;
        if (blank) {
            engineUsed_ = QStringLiteral("blank-image");
            cancel();
            emit recognized(QString()); // The common core classifies this as ST_EMPTY; never translate.
        } else if (paddle_ && paddleRecognition_ && PaddleOcr::available(paddleDir_, languages_)) runPaddle(image, ticket);
        else if (paddle_ && languages_ != QLatin1String("multi")) runDetectorThenTesseract(image, ticket);
        else runTesseract(image, ticket);
    });
    watcher->setFuture(QtConcurrent::run([image] { return isUniformImage(image); }));
}
void OcrService::setPaddleModelDir(const QString& dir, bool recognition) {
    paddleRecognition_ = recognition;
    if (dir == paddleDir_) return;
    paddleDir_ = dir;
    paddle_ = dir.isEmpty() ? nullptr : std::make_shared<PaddleOcr>(dir); // models load on first use or warmUp
}
void OcrService::warmUp(const QString& languages) {
    if (!paddle_ || !Languages::isOcrCode(languages)) return;
    const auto paddle = paddle_;
    const bool recognition = paddleRecognition_;
    warmUp_ = QtConcurrent::run([paddle, languages, recognition] { paddle->warmUp(languages, recognition); });
}
void OcrService::runDetectorThenTesseract(const QImage& image, quint64 ticket) {
    // Tesseract invents letters on empty, bordered or noisy areas ("Po", "이"); the detection model
    // tells whether there is a text line at all (P-21, docs/plan/BACKLOG.md).
    auto* watcher = new QFutureWatcher<int>(this);
    connect(watcher, &QFutureWatcher<int>::finished, this, [this, watcher, image, ticket] {
        const int lines = watcher->result(); watcher->deleteLater();
        if (!busy_ || generation_ != ticket) return;
        paddleDetails_.detectorBoxes = lines;
        if (lines == 0) {
            engineUsed_ = QStringLiteral("text-detector");
            cancel();
            emit recognized(QString()); // The common core classifies this as ST_EMPTY; never translate.
            return;
        }
        runTesseract(image, ticket); // lines > 0, or -1 when the detection model is not installed
    });
    const auto paddle = paddle_;
    cancelled_ = std::make_shared<OcrCancel>();
    const auto cancelled = cancelled_;
    watcher->setFuture(QtConcurrent::run([paddle, image, cancelled] { return paddle->countTextLines(image, cancelled.get()); }));
}
void OcrService::runPaddle(const QImage& image, quint64 ticket) {
    struct Outcome { PaddleOcr::Result result; QString error; };
    auto* watcher = new QFutureWatcher<Outcome>(this);
    connect(watcher, &QFutureWatcher<Outcome>::finished, this, [this, watcher, ticket, image] {
        const Outcome outcome = watcher->result(); watcher->deleteLater();
        if (!busy_ || generation_ != ticket) return;
        attemptsUsed_ = outcome.result.modelRuns;
        paddleDetails_ = outcome.result;
        if (!outcome.result.text.isEmpty()) {
            engineUsed_ = QStringLiteral("PaddleOCR");
            const QString text = outcome.result.text;
            cancel();
            emit recognized(cleanText(text, false));
            return;
        }
        // The detector looked and found no text line, or every line was a misread symbol: the area
        // holds no text. Tesseract would invent some there (measured: "リー", "Po", "이"), so stop.
        if (outcome.error.isEmpty() && (outcome.result.detectorBoxes == 0 || !outcome.result.dropped.isEmpty())) {
            engineUsed_ = QStringLiteral("PaddleOCR");
            cancel();
            emit recognized(QString()); // The common core classifies this as ST_EMPTY; never translate.
            return;
        }
        // Nothing read (or the models failed to load): let Tesseract try the same image.
        if (QFileInfo(executable_).isFile()) { runTesseract(image, ticket); return; }
        fail(outcome.error.isEmpty() ? QStringLiteral("글자를 인식하지 못했습니다. 영역과 글자 크기를 확인하세요.") : outcome.error);
    });
    const auto paddle = paddle_;
    const QString languages = languages_;
    cancelled_ = std::make_shared<OcrCancel>();
    const auto cancelled = cancelled_;
    watcher->setFuture(QtConcurrent::run([paddle, image, languages, cancelled] {
        Outcome outcome;
        outcome.result = paddle->read(image, languages, outcome.error, cancelled.get());
        return outcome;
    }));
}
void OcrService::runTesseract(const QImage& image, quint64 ticket) {
    if (languages_ == QLatin1String("multi")) {
        fail(QStringLiteral("다국어 자동 감지는 PaddleOCR 모델 7개가 필요합니다. 모델을 설치하거나 원문 언어 하나를 선택하세요."));
        return;
    }
    auto* watcher = new QFutureWatcher<QList<QByteArray>>(this);
    connect(watcher, &QFutureWatcher<QList<QByteArray>>::finished, this, [this, watcher, ticket] {
        const auto candidates = watcher->result(); watcher->deleteLater();
        if (!busy_ || generation_ != ticket) return;
        if (candidates.isEmpty() || candidates.first().isEmpty()) {
            fail(QStringLiteral("이미지 인코딩에 실패했거나 이미지가 너무 큽니다.")); return;
        }
        attempts_ = candidates;
        launchAttempts();
    });
    watcher->setFuture(QtConcurrent::run([image] {
        QList<QByteArray> candidates;
        auto encode = [&candidates](const QImage& candidate) {
            QByteArray bytes; QBuffer buffer(&bytes); buffer.open(QIODevice::WriteOnly);
            candidates << (candidate.save(&buffer, "PNG") && bytes.size() <= 32 * 1024 * 1024 ? bytes : QByteArray());
        };
        encode(image); // upscaling was measured to hurt busy-background text, so the original goes first as is
        if (qint64(image.width()) * image.height() * 9 <= 16000000) {
            // Skip a mask when there is no light text, or when "light" is really the background.
            auto usable = [](double coverage) { return coverage > 0.003 && coverage < 0.4; };
            double coverage = 0;
            const QImage mask = lightTextMask(image, &coverage);
            if (usable(coverage)) { encode(mask); encode(growInk(mask)); }
            const QImage bright = lightTextMask(image, &coverage, true);
            if (usable(coverage)) encode(bright);
        }
        candidates.removeAll(QByteArray());
        return candidates;
    }));
}
void OcrService::launchAttempts() {
    // The original image runs alone: it is often enough. When it is not, the masks run side by side,
    // as many at once as the OCR thread budget (P-27), instead of one after another.
    const int limit = judged_ == 0 ? 1 : PaddleOcr::threads();
    while (busy_ && launched_ < attempts_.size() && int(jobs_.size()) < limit) {
        const int attempt = launched_++;
        launch(executable_, arguments_, attempts_.at(attempt), attempt);
    }
}
void OcrService::judgeAttempts() {
    // Readings are judged in image order with the one-by-one rule, so running attempts at once
    // never changes which reading wins.
    for (auto next = outputs_.find(judged_); next != outputs_.end(); next = outputs_.find(judged_)) {
        const Reading reading = parseOutput(next->second);
        outputs_.erase(next);
        const bool fromMask = judged_ > 0;
        if (!reading.text.isEmpty()) {
            const double needed = best_.confidence + (fromMask && !bestFromMask_ ? MaskMargin : 0);
            if (best_.text.isEmpty() || reading.confidence > needed) { best_ = reading; bestFromMask_ = fromMask; }
        }
        attemptsUsed_ = ++judged_;
        if (best_.confidence >= ConfidentEnough || judged_ >= attempts_.size()) {
            const Reading best = best_;
            cancel(); // stops attempts still running
            engineUsed_ = QStringLiteral("Tesseract");
            tesseractConfidence_ = best.text.isEmpty() ? -1 : best.confidence;
            if (best.text.isEmpty()) emit failed(QStringLiteral("글자를 인식하지 못했습니다. 영역과 글자 크기를 확인하세요."));
            else if (isUnsureReading(best)) emit recognized(QString()); // invented on an empty or decorated area
            else emit recognized(cleanText(best.text));
            return;
        }
    }
    launchAttempts();
}
void OcrService::probe(const QString& executable, const QString& data, const QString& languages) {
    if (busy_) return;
    languages_ = languages;
    probing_ = true; busy_ = true; elapsed_.start(); elapsedMs_ = 0; peakMemory_ = 0;
    ++generation_; deadline_.start(20000);
    QStringList args{"--list-langs"};
    if (!data.isEmpty()) args << "--tessdata-dir" << data;
    launch(executable, args, {});
}
void OcrService::collect(QProcess* process) {
    Job* running = job(process);
    if (!running) return;
    running->output += process->readAllStandardOutput(); running->errors += process->readAllStandardError();
    if (running->output.size() > 1024 * 1024 || running->errors.size() > 1024 * 1024)
        fail(QStringLiteral("OCR 출력이 허용 크기를 초과했습니다."));
}
void OcrService::launch(const QString& executable, const QStringList& args, QByteArray bytes, int attempt) {
    if (!QFileInfo(executable).isFile()) { fail(QStringLiteral("Tesseract 실행 파일을 찾을 수 없습니다.")); return; }
    auto* p = new QProcess(this);
    jobs_.push_back({p, attempt});
    p->setProcessChannelMode(QProcess::SeparateChannels);
    connect(p, &QProcess::started, this, [this, p, bytes] {
        Job* running = job(p);
        if (!running) return;
        running->handle = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, DWORD(p->processId()));
        sample(); memoryTimer_.start();
        if (!bytes.isEmpty() && p->write(bytes) != bytes.size()) { fail(QStringLiteral("OCR 이미지 전달에 실패했습니다.")); return; }
        p->closeWriteChannel();
    });
    connect(p, &QProcess::readyReadStandardOutput, this, [this, p] { collect(p); });
    connect(p, &QProcess::readyReadStandardError, this, [this, p] { collect(p); });
    connect(p, &QProcess::errorOccurred, this, [this, p](QProcess::ProcessError e) {
        if (!job(p)) return;
        if (e == QProcess::FailedToStart) fail(QStringLiteral("Tesseract를 시작하지 못했습니다. 실행 파일과 DLL을 확인하세요."));
        else if (e != QProcess::Crashed) fail(QStringLiteral("OCR 프로세스 통신 오류입니다."));
    });
    connect(p, qOverload<int,QProcess::ExitStatus>(&QProcess::finished), this,
        [this, p](int code, QProcess::ExitStatus exit) {
            collect(p);
            Job* running = job(p);
            if (!running) return;
            if (exit != QProcess::NormalExit || code != 0) {
                fail(QStringLiteral("OCR 실행 실패. 선택한 언어 모델(%1)과 tessdata 경로를 확인하세요.")
                    .arg(languages_)); return;
            }
            QStringDecoder decoder(QStringDecoder::Utf8);
            const QString decoded = decoder(running->output);
            if (decoder.hasError()) { fail(QStringLiteral("OCR 출력의 문자 인코딩을 읽지 못했습니다.")); return; }
            const int attempt = running->attempt;
            sample();
            if (running->handle) CloseHandle(static_cast<HANDLE>(running->handle));
            disconnect(p, nullptr, this, nullptr);
            p->deleteLater();
            jobs_.erase(jobs_.begin() + (running - jobs_.data()));
            if (jobs_.empty()) memoryTimer_.stop();
            if (attempt >= 0) { outputs_[attempt] = decoded; judgeAttempts(); return; }
            cancel();
            const QString text = decoded.trimmed();
            const QStringList installed = text.split(QRegularExpression("[\\r\\n]+"));
            QStringList missing;
            for (const QString& model : languages_.split('+'))
                if (!installed.contains(model)) missing << model;
            emit probeFinished(missing.isEmpty(), missing.isEmpty()
                ? QStringLiteral("OCR 엔진과 필요한 언어 모델(%1)을 확인했습니다.").arg(languages_.split('+').join(", "))
                : QStringLiteral("없는 언어 모델: %1. tessdata 폴더에 해당 traineddata를 준비하세요.").arg(missing.join(", ")));
        });
    p->start(executable, args, QIODevice::ReadWrite);
}
