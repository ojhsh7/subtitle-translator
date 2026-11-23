#include "main_window.h"
#include "capture_service.h"
#include "credential_store.h"
#include "game_window.h"
#include "languages.h"
#include "paddle_ocr.h"
#include "region_selector.h"
#include "subtitle/text.h"
#include "subtitle_overlay.h"
#include "fluent_widgets.h"
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QCursor>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScreen>
#include <QSettings>
#include <QSignalBlocker>
#include <QSystemTrayIcon>
#include <QStandardPaths>
#include <QTimer>
#include <QImageReader>
#include <QFutureWatcher>
#include <QtConcurrent>
#include <QVBoxLayout>
#include <algorithm>
#include <windows.h>

namespace {
int editDistance(const QString& a, const QString& b) {
    QList<int> row(b.size() + 1);
    for (int j = 0; j <= b.size(); ++j) row[j] = j;
    for (int i = 1; i <= a.size(); ++i) {
        int diagonal = row[0];
        row[0] = i;
        for (int j = 1; j <= b.size(); ++j) {
            const int above = row[j];
            row[j] = qMin(qMin(row[j] + 1, row[j - 1] + 1), diagonal + (a[i - 1] == b[j - 1] ? 0 : 1));
            diagonal = above;
        }
    }
    return row[b.size()];
}
// Two readings that could be one line misread: long enough to judge, and all but an eighth alike.
bool nearlySame(const QString& a, const QString& b) {
    const qsizetype shorter = qMin(a.size(), b.size());
    if (shorter < 8 || a == b || qAbs(a.size() - b.size()) > shorter / 8 + 1) return false;
    return editDistance(a, b) <= qMax<qsizetype>(1, shorter / 8);
}
QString describeRegion(const QScreen* screen, const QRect& region) {
    return QStringLiteral("%1 화면의 %2×%3 영역").arg(screen->name()).arg(region.width()).arg(region.height());
}
// How a game is named in messages (P-41): its window title, which is usually the game's name,
// else the program file ("hspy.exe"). A long title is cut.
QString gameLabel(const GameWindow::Info& info) {
    const QString title = info.title.simplified();
    if (title.isEmpty()) return info.name;
    return title.size() > 40 ? title.left(39) + QChar(0x2026) : title;
}
// Settings group of one game. A program path has '/' and ':', which QSettings would treat as structure.
QString gameGroup(const QString& key) {
    return QStringLiteral("games/") +
        QString::fromLatin1(key.toUtf8().toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}
}

MainWindow::MainWindow() {
    buildUi();
    connect(followGame_, &QCheckBox::toggled, this, [this](bool enabled) {
        QSettings().setValue("capture/followGame", enabled);
        if (!enabled && !game_.key.isEmpty()) {
            game_ = {}; QSettings().remove("capture/game");
            if (selectedScreen_ && !region_.isEmpty()) { saveRegion(); regionLabel_->setText(describeRegion(selectedScreen_, region_)); }
        }
    });
    // A new language changes what the current line means: forget the cache and read it again.
    connect(ocrLanguageBox_, &QComboBox::currentIndexChanged, this, [this] {
        QSettings().setValue("ocr/languages", ocrLanguages());
        if (!game_.key.isEmpty()) QSettings().setValue(gameGroup(game_.key) + QStringLiteral("/ocrLanguages"), ocrLanguages());
        ocr_.warmUp(ocrLanguages());
        st_reset_cache(session_); detector_.reset(); forgetShownLines();
        if (hoverRequest_ && busy_) cancel();
        hoverSince_ = -1; hoverConsumed_ = false; hoverRequest_ = false;
    });
    connect(targetLanguageBox_, &QComboBox::currentIndexChanged, this, [this] {
        QSettings().setValue("translation/target", targetLanguage());
        st_reset_cache(session_); detector_.reset(); forgetShownLines();
        if (hoverRequest_ && busy_) cancel();
        hoverSince_ = -1; hoverConsumed_ = false; hoverRequest_ = false;
    });
    connect(autoTimer_, &QTimer::timeout, this, &MainWindow::autoTick);
    connect(qApp, &QGuiApplication::screenAdded, this, [this] { refreshScreens(); });
    connect(qApp, &QGuiApplication::screenRemoved, this, [this] {
        setHoverTranslate(false);
        cancel(); subtitleOverlay_->hide(); selectedScreen_ = nullptr; region_ = {}; refreshScreens();
        regionLabel_->setText(game_.key.isEmpty()
            ? QStringLiteral("모니터 구성이 바뀌었어요. 영역을 다시 선택하세요.")
            : QStringLiteral("모니터 구성이 바뀌었어요. 게임 창 %1 기준 영역은 다시 계산해요.").arg(game_.name));
    });
    connect(&ocr_, &OcrService::recognized, this, [this](const QString& text) {
        if (!busy_) return;
        if (checking_) {
            checking_ = false;
            ReadState* const checked = readState();
            ChangeDetector& checker = activeArea_ == 0 || !checked ? detector_ : extras_[size_t(activeArea_) - 1]->detector;
            const QString seen = lineKey(text);
            if (checked && !text.isEmpty() && seen == checked->lastRead) {
                // The known lines still say the same: what moved was animation. Nothing else to do.
                checker.noteRead(false);
                st_fail(session_, request_);
                if (!checkStatus_.isEmpty()) setStatus(checkStatus_);
                setBusy(false);
                return;
            }
            if (checked && (nearlySame(seen, checked->shownKey) || doubtful())) {
                // Probably the line on screen misread (something crossed it). Reading this same
                // picture again would agree with the misreading, so the next frame decides.
                checked->variant = seen;
                checker.lookAgain(autoClock_.elapsed() + ChangeDetector::ConfirmReadMs);
                st_fail(session_, request_);
                if (!checkStatus_.isEmpty()) setStatus(checkStatus_);
                setBusy(false);
                return;
            }
            // Something else there, or nothing: read the whole region, line finding included.
            ocr_.recognize(lastImage_, executable_, tessdata_, psm_, ocrLanguages());
            return;
        }
        if (autoRead_) ++fullReads_;
        const auto utf8 = text.toUtf8();
        const int decision = st_accept_ocr(session_, request_, utf8.constData());
        if (decision == ST_STALE) return;
        ReadState* const state = readState();
        ChangeDetector& detector = activeArea_ == 0 || !state ? detector_ : extras_[size_t(activeArea_) - 1]->detector;
        if (state) {
            // Line boxes for the next check, when PaddleOCR found the lines of this picture.
            const bool boxed = ocr_.engineUsed() == QLatin1String("PaddleOCR") && !text.isEmpty();
            state->boxes = boxed ? ocr_.paddleDetails().boxes : QList<QRect>();
            state->boxesSize = lastImage_.size();
            // Tell the change detector whether its read found other text: if not, what moved was animation.
            const QString seen = lineKey(text);
            detector.noteRead(seen != state->lastRead);
            if (activeArea_ == 0 && state->lastRead.size() >= 2 && seen.size() > state->lastRead.size() &&
                seen.startsWith(state->lastRead)) typewriterSeen_ = true;
            state->lastRead = seen;
            if (seen == state->shownKey) { state->variant.clear(); state->doubts = 0; }
        }
        original_->setPlainText(QString::fromUtf8(st_source_text(session_)));
        if (decision == ST_EMPTY || decision == ST_TOO_LONG) {
            st_fail(session_, request_);
            if (decision == ST_EMPTY && autoTranslating() && activeSource_ == ST_SCREEN_CAPTURE) {
                // An empty dialogue box is normal while playing: hide the old caption quietly.
                activeOverlay()->hide(); setBusy(false);
                const bool retry = activeArea_ == 0 && detector_.retryAfterFailure(autoClock_.elapsed());
                setStatus(retry
                    ? QStringLiteral("글자가 보이지 않아요. 3초 뒤에 다시 확인할게요.")
                    : QStringLiteral("글자를 찾지 못했어요. 다음 대사를 기다리는 동안 영역이 맞는지 확인해 주세요."));
                return;
            }
            resultStatus_->setText(QStringLiteral("OCR 결과를 번역할 수 없어요."));
            finish(decision == ST_EMPTY ? QStringLiteral("인식한 글자가 없어요.")
                : QStringLiteral("인식 텍스트가 너무 길어요. 자막 부분만 선택하세요."));
            return;
        }
        if (decision == ST_CACHED) {
            sparedCharacters_ += text.size();
            result_->setPlainText(QString::fromUtf8(st_translation(session_)));
            resultStatus_->setText(QStringLiteral("앞과 같은 대사라서 지난 번역을 다시 보여 줘요."));
            presentTranslation();
            finish(QStringLiteral("동일한 원문이므로 번역 API를 다시 호출하지 않았어요."));
            return;
        }
        // Only foreign-script runs are translated; text already in the target language stays.
        const QString target = Languages::targetLabel(targetLanguage());
        const QString read = QString::fromUtf8(st_source_text(session_));
        const QString source = applyFixes(read, false);
        if (source != read) original_->setPlainText(source);
        currentKey_ = lineKey(source);
        prefixSource_.clear(); prefixShown_.clear(); keptComesLast_ = false;
        // Translated before (within the last CacheLines lines): show it at once and spare the request.
        if (const auto found = lineCache_.constFind(memoryKey(currentKey_)); found != lineCache_.constEnd()) {
            const Remembered line = *found;
            cacheOrder_.removeOne(found.key()); cacheOrder_.append(memoryKey(currentKey_));
            sparedCharacters_ += source.size();
            showRemembered(line);
            return;
        }
        // Read while something moved, and a character is unsure: wait for the next frame to agree,
        // so a misread line is not sent (also the first line, which has nothing on screen to compare).
        if (state && detector.movingAtRead() && doubtful() && currentKey_ != state->variant && state->doubts < MaxDoubts) {
            ++state->doubts;
            state->variant = currentKey_;
            detector.lookAgain(autoClock_.elapsed() + ChangeDetector::ConfirmReadMs);
            st_finish_ocr(session_, request_);
            if (!state->shown.isEmpty()) result_->setPlainText(applyFixes(state->shown, true));
            setBusy(false);
            setStatus(QStringLiteral("글자가 흐리게 읽혀서 한 번 더 확인하고 있어요."));
            return;
        }
        if (state) state->doubts = 0;
        // Almost the line on screen: probably the same line misread (a light crossed a letter). It is
        // translated only if a second read finds the same; a real small change is still caught.
        if (state && nearlySame(currentKey_, state->shownKey) && currentKey_ != state->variant) {
            state->variant = currentKey_;
            detector.lookAgain(autoClock_.elapsed() + ChangeDetector::ConfirmReadMs);
            st_finish_ocr(session_, request_);
            sparedCharacters_ += source.size();
            result_->setPlainText(applyFixes(state->shown, true));
            setBusy(false);
            setStatus(QStringLiteral("앞 대사와 거의 같게 읽혀서 한 번 더 확인하고 있어요."));
            return;
        }
        if (state) state->variant.clear();
        const auto kept = subtitle::preservableScripts(targetLanguage().section('-', 0, 0).toLower().toStdString(),
            Languages::deeplSource(ocrLanguages()).toLower().toStdString());
        QStringList pieces;
        unsigned scripts = 0;
        auto split = [&](const QString& part) {
            segments_ = subtitle::splitForTranslation(part.toStdString(), kept);
            pieces.clear(); scripts = 0;
            for (const auto& segment : segments_)
                if (segment.translate) { pieces << QString::fromStdString(segment.utf8); scripts |= subtitle::scriptsIn(segment.utf8); }
        };
        // The line on screen with more text after it (a typewriter line sent early): send only the rest.
        if (state && state->shownKey.size() >= 4 && !state->shown.isEmpty() &&
            currentKey_.size() > state->shownKey.size() && currentKey_.startsWith(state->shownKey)) {
            qsizetype position = 0, counted = 0;
            while (position < source.size() && counted < state->shownKey.size())
                if (!source[position++].isSpace()) ++counted;
            // The shown line's final period, which its key leaves out, belongs to it.
            while (position < source.size() && (source[position].isSpace() || QStringLiteral("。．.").contains(source[position]))) ++position;
            split(source.mid(position).trimmed());
            if (!pieces.isEmpty()) { prefixSource_ = source.left(position); prefixShown_ = state->shown; }
        } else if (state && state->shownKey.size() >= 4 && !state->shown.isEmpty() &&
                   currentKey_.size() > state->shownKey.size() && currentKey_.endsWith(state->shownKey)) {
            // The same line with text in front of it (something else came into the region).
            qsizetype position = source.size(), counted = 0;
            qsizetype skip = -currentKey_.size(); // characters lineKey dropped: an arrow after the text
            for (const QChar ch : source) skip += !ch.isSpace();
            while (position > 0 && counted < state->shownKey.size() + skip)
                if (!source[--position].isSpace()) ++counted;
            split(source.left(position).trimmed());
            if (!pieces.isEmpty()) { prefixSource_ = source.mid(position); prefixShown_ = state->shown; keptComesLast_ = true; }
        }
        if (prefixShown_.isEmpty()) split(source);
        if (pieces.isEmpty()) {
            st_finish_ocr(session_, request_);
            resultStatus_->setText(QStringLiteral("원문이 이미 %1라서 번역하지 않았어요.").arg(target));
            if (autoTranslating() && activeSource_ == ST_SCREEN_CAPTURE) {
                activeOverlay()->hide(); setBusy(false);
                setStatus(QStringLiteral("원문이 이미 %1라서 번역하지 않았어요.").arg(target));
                return;
            }
            finish(QStringLiteral("원문이 이미 %1라 번역 요청을 보내지 않았어요.").arg(target));
            return;
        }
        if (apiKey_.isEmpty() || !consent_) {
            st_finish_ocr(session_, request_);
            addToBacklog(source, QString());
            resultStatus_->setText(QStringLiteral("OCR 완료. 번역하려면 설정의 번역 페이지에서 API 키와 전송 동의를 확인해 주세요."));
            finish(QStringLiteral("OCR 완료. API 키나 전송 동의가 없어서 번역 요청은 보내지 않았어요."));
            return;
        }
        if (autoTranslating() && autoClock_.elapsed() < translationPausedUntil_) {
            st_finish_ocr(session_, request_);
            const qint64 left = (translationPausedUntil_ - autoClock_.elapsed() + 999) / 1000;
            resultStatus_->setText(QStringLiteral("요청 제한 때문에 잠시 번역을 보내지 않고 있어요."));
            finish(QStringLiteral("DeepL 요청 제한으로 %1초 동안 원문만 표시해요.").arg(left));
            return;
        }
        // A line that does not look finished is read but waits until its text stayed the same for
        // SentenceSettleMs, so a typewriter pause at a comma never sends half a line. A line that
        // appeared at once is not being typed and goes out right away (P-38).
        if (autoRead_ && autoTranslating() && activeArea_ == 0 && !looksFinished(source)) {
            const qint64 now = autoClock_.elapsed();
            const QString key = lineKey(source);
            bool wait = true;
            if (key != pendingText_) {
                pendingText_ = key; pendingSince_ = now - detector_.stableMs(now);
                wait = typewriterSeen_ || !detector_.appearedAtOnce();
            }
            if (wait && now - pendingSince_ < ChangeDetector::SentenceSettleMs) {
                holding_ = true; heldPieces_ = pieces; heldScripts_ = scripts;
                setBusy(false);
                setStatus(QStringLiteral("문장이 끝나기를 기다리는 중이에요."));
                return;
            }
        }
        pendingText_.clear();
        requestTranslation(pieces, scripts);
    });
    connect(&ocr_, &OcrService::failed, this, [this](const QString& error) {
        if (!busy_) return;
        st_fail(session_, request_);
        resultStatus_->setText(QStringLiteral("글자를 읽지 못해서 번역하지 않았어요."));
        const bool retry = autoTranslating() && activeSource_ == ST_SCREEN_CAPTURE && activeArea_ == 0 &&
                           detector_.retryAfterFailure(autoClock_.elapsed());
        finish(retry ? error + QStringLiteral("\n자동 번역: 3초 뒤 다시 확인해요 (같은 화면에서 최대 2회).") : error);
    });
    connect(&translation_, &TranslationService::translated, this, [this](const QStringList& pieces, const QString& detected) {
        if (busy_) applyTranslation(pieces, detected, false);
    });
    connect(&translation_, &TranslationService::failed, this, [this](const QString& error) {
        if (!busy_) return;
        st_fail(session_, request_);
        resultStatus_->setText(QStringLiteral("번역하지 못했어요. 원문은 더 보기의 인식 내용에서 볼 수 있어요."));
        const int status = translation_.lastHttpStatus();
        if (status == 456 && autoTranslating()) {
            // Quota exhausted: every further line would fail and keep showing an error caption.
            setAutoTranslate(false);
            finish(error + QStringLiteral("\n자동 번역을 멈췄어요. 한도를 확인한 뒤 Ctrl+Alt+P로 다시 켜세요."));
            return;
        }
        if (status == 429) {
            const int seconds = qMax(MinRateLimitPauseSeconds, translation_.lastRetryAfterSeconds());
            if (autoTranslating()) {
                // Too many requests: keep reading lines but send none until the server allows it again.
                translationPausedUntil_ = autoClock_.elapsed() + qint64(seconds) * 1000;
                finish(error + QStringLiteral("\n%1초 동안 번역 요청을 보내지 않고 원문만 표시해요.").arg(seconds));
            } else {
                finish(error + QStringLiteral("\n%1초 뒤에 다시 시도하세요.").arg(seconds));
            }
            return;
        }
        finish(error);
    });
    connect(&usage_, &TranslationService::usageChecked, this, [this](qint64 used, qint64 limit) {
        usageUsed_ = used; usageLimit_ = limit; sentSinceUsage_ = 0;
        refreshUi();
        if (limit > 0 && used * 10 >= limit * 9)
            setStatus(QStringLiteral("이번 달 DeepL 한도의 %1%를 썼어요. 남은 글자는 %2자예요.")
                .arg(used * 100 / limit).arg(QLocale().toString(qMax<qint64>(0, limit - used))));
    });
    connect(&usage_, &TranslationService::failed, this, [this](const QString&) {
        // The check after start or saving is the first request with a new key: say so before a line is lost.
        if (usage_.lastHttpStatus() == 403)
            setStatus(QStringLiteral("DeepL이 키를 받지 않았어요. 설정의 번역 페이지에서 키와 요금제(Free, Pro)를 확인해 주세요."));
    });
    QTimer::singleShot(2000, this, &MainWindow::refreshUsage);
    contextOn_ = QSettings().value("translation/context", true).toBool();
    hoverDwellMs_ = qBound(300, QSettings().value("cursor/dwellMs", 1000).toInt(), 3000);
    QSettings saved;
    executable_ = saved.value("ocr/executable").toString();
    tessdata_ = saved.value("ocr/tessdata").toString();
    // New key: the old "ocr/psm" only ever held the former default, so automatic layout applies to everyone.
    psm_ = saved.value("ocr/layout", 0).toInt();
    if (psm_ != 6 && psm_ != 7) psm_ = 0;
    freeApi_ = saved.value("translation/freeApi", true).toBool();
    consent_ = saved.value("translation/consent", false).toBool();
    remember_ = saved.value("translation/rememberKey", false).toBool();
    QString credentialError;
    if (remember_) apiKey_ = Credentials::load(credentialError);
    // A saved path can vanish (e.g. an old copy in a temp folder); fall back to the bundled engine.
    if (!executable_.isEmpty() && !QFileInfo::exists(executable_)) { executable_.clear(); tessdata_.clear(); }
    if (!tessdata_.isEmpty() && !QDir(tessdata_).exists()) tessdata_.clear();
    if (executable_.isEmpty()) {
        const QString bundled = QCoreApplication::applicationDirPath() + "/ocr/tesseract.exe";
        const QString installed = "C:/Program Files/Tesseract-OCR/tesseract.exe";
        if (QFileInfo::exists(bundled)) executable_ = bundled;
        else if (QFileInfo::exists(installed)) executable_ = installed;
        else executable_ = QStandardPaths::findExecutable("tesseract");
    }
    if (tessdata_.isEmpty() && !executable_.isEmpty()) {
        const QString adjacent = QFileInfo(executable_).absolutePath() + "/tessdata";
        if (QDir(adjacent).exists()) tessdata_ = adjacent;
    }
    // PaddleOCR is the default engine (measured far more accurate on game text); Tesseract stays as fallback.
    paddleDir_ = QCoreApplication::applicationDirPath() + "/ocr/paddle";
    paddleEngine_ = saved.value("ocr/engine", QStringLiteral("paddle")).toString() != QLatin1String("tesseract");
    applyOcrEngine();
    refreshScreens();
    ignoreProcess_ = GameWindow::currentProcess();
    foregroundWindow_ = [this] { return GameWindow::foreground(ignoreProcess_); };
    restoreRegion();
    restoreExtraAreas();
    registerHotkeys();
    QString ready = nextStep();
    QStringList taken;
    if (!regionRegistered_) taken << regionShortcut_;
    if (!autoRegistered_) taken << autoShortcut_;
    if (!registered_) taken << captureShortcut_;
    if (!cursorRegistered_) taken << cursorShortcut_;
    if (!hoverRegistered_) taken << hoverShortcut_;
    if (!captionsRegistered_) taken << captionsShortcut_;
    if (!taken.isEmpty())
        ready += QStringLiteral("\n%1 키는 다른 앱이 쓰고 있어서 등록하지 못했어요. 설정의 단축키 페이지에서 바꿀 수 있어요.").arg(taken.join(", "));
    setStatus(ready);
    const bool setupComplete = saved.value("setupComplete", false).toBool();
    // The desktop shortcut passes --auto: the user chose to start translating right away.
    if (QCoreApplication::arguments().contains("--auto")) {
        if (setupComplete && hasRegion()) setAutoTranslate(true);
        else {
            autoPending_ = true;
            setStatus(QStringLiteral("설정을 마치고 자막 영역을 선택하면 자동 번역이 바로 시작돼요."));
        }
    }
    if (!credentialError.isEmpty()) setStatus(credentialError);
    if (!setupComplete &&
        !QCoreApplication::arguments().contains("--smoke-test") && !qApp->property("testMode").toBool())
        QTimer::singleShot(0, this, [this] { settings(1); }); // first run: the DeepL page
}
MainWindow::~MainWindow() {
    hoverTimer_->stop();
    autoTimer_->stop();
    cancel();
    if (registered_) UnregisterHotKey(reinterpret_cast<HWND>(winId()), 1);
    if (autoRegistered_) UnregisterHotKey(reinterpret_cast<HWND>(winId()), 2);
    if (cursorRegistered_) UnregisterHotKey(reinterpret_cast<HWND>(winId()), 3);
    if (hoverRegistered_) UnregisterHotKey(reinterpret_cast<HWND>(winId()), 4);
    if (regionRegistered_) UnregisterHotKey(reinterpret_cast<HWND>(winId()), 5);
    if (captionsRegistered_) UnregisterHotKey(reinterpret_cast<HWND>(winId()), 6);
    delete captureFlash_;
    delete subtitleOverlay_;
    delete resultWindow_;
    delete detailsWindow_;
    extras_.clear();
    delete backlogWindow_;
    st_destroy(session_);
}
void MainWindow::refreshScreens() {
    screens_->clear();
    const auto list = QGuiApplication::screens();
    for (int i = 0; i < list.size(); ++i)
        screens_->addItem(QStringLiteral("%1번 모니터 %2 (%3×%4)").arg(i + 1).arg(list[i]->name())
            .arg(list[i]->size().width()).arg(list[i]->size().height()));
}
void MainWindow::chooseRegionUnderCursor() {
    if (modal_) return;
    if (busy_) cancel(); // An explicit request from the game screen replaces the current read.
    QScreen* screen = QGuiApplication::screenAt(QCursor::pos());
    if (!screen) screen = QGuiApplication::primaryScreen();
    const int index = int(QGuiApplication::screens().indexOf(screen));
    if (index >= 0) screens_->setCurrentIndex(index);
    chooseRegion(screen);
}
void MainWindow::chooseRegion(QScreen* requested) {
    if (busy_ || modal_) return;
    setHoverTranslate(false);
    const auto list = QGuiApplication::screens();
    if (!requested && (screens_->currentIndex() < 0 || screens_->currentIndex() >= list.size())) return;
    QPointer<QScreen> screen = requested ? requested : list[screens_->currentIndex()];
    followGame(false); // Show the region where it is now, also after the game window moved.
    warmTranslation();  // a translation usually follows choosing a region
    modal_ = true;
    restoreMain_ = isVisible() && !isMinimized(); restoreResult_ = resultWindow_->isVisible();
    restoreOverlay_ = subtitleOverlay_->isVisible();
    if (!isMinimized()) hide();
    resultWindow_->hide(); subtitleOverlay_->hide();
    QTimer::singleShot(180, this, [this, screen] {
        if (!modal_ || !screen) { modal_ = false; restoreWindows(); return; }
        const QPixmap snapshot = screen->grabWindow(0);
        if (snapshot.isNull()) {
            modal_ = false; restoreWindows(); finish(QStringLiteral("모니터 화면을 읽을 수 없어요.")); return;
        }
        RegionSelector selector(snapshot, screen->geometry(), selectedScreen_.data() == screen.data() ? region_ : QRect());
        const bool selected = selector.exec() == QDialog::Accepted && screen;
        if (selected) {
            restoreOverlay_ = false;
            setRegion(screen, selector.selectedRegion());
            setStatus(game_.key.isEmpty()
                ? QStringLiteral("영역을 저장했어요. 자동 번역을 켜거나 Ctrl+Alt+T를 눌러 보세요.")
                : QStringLiteral("영역을 저장했어요. %1 창을 옮겨도 따라가요. 자동 번역을 켜거나 Ctrl+Alt+T를 눌러 보세요.").arg(game_.name));
        }
        modal_ = false;
        restoreWindows();
        if (selected && autoPending_) setAutoTranslate(true);
        else if (selected && inlineCaptions_->isChecked()) previewSubtitle();
    });
}
MainWindow::CaptureArea MainWindow::selectedArea() const {
    return {selectedScreen_, selectedGeometry_, region_, selectedDpr_};
}
void MainWindow::startCapture() {
    if (busy_ || modal_) return;
    setHoverTranslate(false);
    if (!hasRegion()) {
        setStatus(QStringLiteral("먼저 자막 영역을 선택하세요.")); return;
    }
    if (!followGame()) {
        setStatus(QStringLiteral("게임 창 %1이(가) 보이지 않아요. 게임 창을 띄운 뒤 다시 누르세요.").arg(game_.name)); return;
    }
    beginScreenCapture(selectedArea(), QStringLiteral("선택 영역 캡처 중…"));
}
void MainWindow::scheduleCursorCapture() {
    if (busy_ || modal_) return;
    setHoverTranslate(false);
    setAutoTranslate(false);
    setBusy(true);
    const quint64 ticket = ++generation_;
    setStatus(QStringLiteral("3초 안에 번역할 글자 위로 마우스를 옮기세요. 취소 버튼으로 중단할 수 있어요."));
    QTimer::singleShot(3000, this, [this, ticket] {
        if (!busy_ || ticket != generation_) return;
        setBusy(false);
        startCursorCapture();
    });
}
void MainWindow::startCursorCapture() {
    if (modal_) return;
    setHoverTranslate(false);
    // Freeze the point now: cursor movement during OCR must not move the source or result.
    const QPoint point = QCursor::pos();
    QScreen* screen = QGuiApplication::screenAt(point);
    setAutoTranslate(false); // Keep the next game subtitle from replacing this one-shot result.
    if (busy_) cancel();     // A newer mouse request replaces unfinished work.
    if (!screen) {
        setStatus(QStringLiteral("마우스가 있는 모니터를 찾지 못했어요. 화면 안으로 옮긴 뒤 다시 시도하세요."));
        return;
    }
    const auto rect = Capture::aroundPoint(screen->geometry(), point, cursorSizeBox_->currentData().toSize());
    if (rect.isEmpty()) {
        setStatus(QStringLiteral("마우스 주변 캡처 범위를 만들지 못했어요. 범위와 화면 설정을 확인하세요."));
        return;
    }
    beginScreenCapture({screen, screen->geometry(), rect, screen->devicePixelRatio()},
                       QStringLiteral("마우스 주변을 읽는 중… 자동 번역은 Ctrl+Alt+P로 다시 켤 수 있어요."));
}
void MainWindow::setHoverTranslate(bool enabled) {
    const bool wasEnabled = hoverMode_->isChecked() || hoverTimer_->isActive();
    const QSignalBlocker blocker(hoverMode_);
    if (enabled && modal_) {
        hoverMode_->setChecked(wasEnabled);
        return;
    }
    if (enabled && !ocrReady()) {
        hoverMode_->setChecked(false);
        setStatus(QStringLiteral("OCR 엔진을 먼저 준비하세요. 설정에서 모델과 실행 파일을 확인할 수 있어요."));
        return;
    }
    hoverMode_->setChecked(enabled);
    if (!enabled) {
        hoverTimer_->stop();
        hoverSince_ = -1; hoverConsumed_ = false;
        if (!wasEnabled) return;
        if (hoverRequest_ && busy_) cancel();
        hoverRequest_ = false;
        subtitleOverlay_->hide();
        setStatus(QStringLiteral("마우스 올려두기 번역을 껐어요."));
        return;
    }
    setAutoTranslate(false);
    if (busy_) cancel();
    hoverSince_ = -1; hoverConsumed_ = false; hoverRequest_ = false;
    subtitleOverlay_->hide();
    hoverTimer_->start();
    // Keep the target visible. If no global stop key was registered, leave the checkbox visible.
    if (hoverRegistered_) showMinimized();
    setStatus(QStringLiteral("마우스 올려두기를 켰어요. 글자 위에서 1초 멈추면 번역해요.%1")
        .arg(hoverRegistered_ ? QStringLiteral(" %1로 끌 수 있어요.").arg(hoverShortcut_) : QStringLiteral(" 스위치를 끄면 멈춰요.")));
}
void MainWindow::hoverTick() {
    // Query global physical button state even while the game owns mouse input.
    bool pressed = false;
    for (const int button : {VK_LBUTTON, VK_RBUTTON, VK_MBUTTON, VK_XBUTTON1, VK_XBUTTON2})
        pressed = pressed || (GetAsyncKeyState(button) & 0x8000) != 0;
    updateHover(QCursor::pos(), autoClock_.elapsed(), pressed);
}
void MainWindow::updateHover(const QPoint& point, qint64 nowMs, bool mousePressed) {
    if (!hoverMode_->isChecked()) return;
    QScreen* screen = QGuiApplication::screenAt(point);
    const QRect local = screen ? Capture::aroundPoint(screen->geometry(), point, cursorSizeBox_->currentData().toSize()) : QRect();
    const QRect global = screen ? local.translated(screen->geometry().topLeft()) : QRect();
    const bool ownWindow = (isVisible() && !isMinimized() && frameGeometry().intersects(global)) ||
        (resultWindow_->isVisible() && !resultWindow_->isMinimized() && resultWindow_->frameGeometry().intersects(global));
    const bool overCaption = subtitleOverlay_->isVisible() && subtitleOverlay_->geometry().contains(point);
    if (modal_ || mousePressed || !screen || local.isEmpty() || ownWindow || overCaption) {
        hoverSince_ = -1; hoverConsumed_ = false;
        if (hoverRequest_ && busy_) { cancel(); hoverRequest_ = false; }
        return;
    }
    constexpr int MovementTolerance = 12; // logical pixels; small hand tremors do not restart the wait.
    if (hoverSince_ < 0 || (point - hoverAnchor_).manhattanLength() > MovementTolerance) {
        if (hoverRequest_ && busy_) cancel();
        if (hoverRequest_) subtitleOverlay_->hide();
        hoverRequest_ = false;
        hoverAnchor_ = point; hoverSince_ = nowMs; hoverConsumed_ = false;
        warmTranslation(); // the dwell before the read covers opening the DeepL connection
        return;
    }
    if (hoverConsumed_ || busy_ || nowMs - hoverSince_ < hoverDwellMs_) return;
    hoverConsumed_ = true;
    if (!ocrReady()) {
        setHoverTranslate(false);
        setStatus(QStringLiteral("OCR 엔진을 찾지 못해 마우스 번역을 껐어요. 설정을 확인하세요."));
        return;
    }
    hoverRequest_ = true;
    beginScreenCapture({screen, screen->geometry(), local, screen->devicePixelRatio()},
        QStringLiteral("마우스 주변을 읽는 중… 마우스를 옮기면 취소돼요."));
}
void MainWindow::beginScreenCapture(const CaptureArea& area, const QString& status) {
    if (busy_ || modal_) return;
    if (!area.screen || area.rect.isEmpty() || area.screen->geometry() != area.screenGeometry ||
        area.screen->devicePixelRatio() != area.dpr) {
        subtitleOverlay_->hide();
        setStatus(QStringLiteral("모니터 해상도나 배율이 바뀌었어요. 영역을 다시 선택하거나 마우스 번역을 다시 실행하세요."));
        return;
    }
    if (!ocrReady()) {
        setStatus(QStringLiteral("OCR 엔진이 없어요. 설정에서 tesseract.exe 경로를 지정하거나 PaddleOCR 모델을 확인하세요."));
        return;
    }
    warmTranslation();
    activeArea_ = 0; autoRead_ = false;
    // Checked before resetOutput() hides the old caption: a window hidden just now is still on screen.
    const QRect global = area.rect.translated(area.screenGeometry.topLeft());
    const bool covered = coversRegion(global) || (captureFlash_->isVisible() && captureFlash_->geometry().intersects(global));
    setBusy(true);
    resetOutput();
    activeSource_ = ST_SCREEN_CAPTURE;
    captionArea_ = area;
    resultStatus_->setText(QStringLiteral("새 자막을 읽는 중이에요."));
    setStatus(status);
    const quint64 ticket = ++generation_;
    // Our windows are hidden, and the compositor given 180 ms to remove them, only when they would be
    // in the picture. Otherwise the capture starts at once (docs/plan/2026-11-01-ocr-speed.ko.md).
    if (covered) {
        restoreMain_ = isVisible() && !isMinimized(); restoreResult_ = resultWindow_->isVisible();
        restoreOverlay_ = subtitleOverlay_->isVisible();
        if (!isMinimized()) hide();
        resultWindow_->hide(); subtitleOverlay_->hide(); captureFlash_->hide();
    }
    QTimer::singleShot(covered ? 180 : 0, this, [this, ticket, area] {
        if (!busy_ || ticket != generation_) return;
        QString error;
        QImage image;
        if (!area.screen || area.screen->geometry() != area.screenGeometry ||
            area.screen->devicePixelRatio() != area.dpr)
            error = QStringLiteral("캡처 전에 모니터 구성이 바뀌었어요. 다시 시도하세요.");
        else
            image = Capture::region(area.screen, area.rect, error);
        restoreWindows();
        if (image.isNull()) { resultStatus_->setText(QStringLiteral("캡처 실패")); finish(error); return; }
        // Shown only after the pixels were taken, so the outline never enters this or a later image.
        if (showCaptureRange_->isChecked()) captureFlash_->flash(area.rect.translated(area.screenGeometry.topLeft()));
        processImage(image, ST_SCREEN_CAPTURE);
    });
}
void MainWindow::setBusy(bool busy) {
    busy_ = busy;
    select_->setEnabled(!busy); capture_->setEnabled(!busy); settings_->setEnabled(!busy);
    screens_->setEnabled(!busy); cancel_->setEnabled(busy);
    import_->setEnabled(!busy); retry_->setEnabled(!busy && !lastImage_.isNull());
    captionPreview_->setEnabled(!busy);
    cursorCapture_->setEnabled(!busy); cursorSizeBox_->setEnabled(!busy);
    refreshUi();
}
void MainWindow::finish(const QString& status) {
    if (!original_->toPlainText().isEmpty()) stage_->setContent(original_->toPlainText(), result_->toPlainText());
    setBusy(false); setStatus(resultPlacementWarning_.isEmpty() ? status : resultPlacementWarning_);
    if (activeSource_ == ST_SCREEN_CAPTURE && inlineCaptions_->isChecked() && result_->toPlainText().isEmpty())
        showSubtitle(QStringLiteral("번역하지 못했어요. ") + status);
}
void MainWindow::restoreWindows() {
    if (restoreMain_) show();
    if (restoreResult_) resultWindow_->show();
    if (restoreOverlay_ && inlineCaptions_->isChecked()) subtitleOverlay_->show();
    restoreMain_ = false; restoreResult_ = false; restoreOverlay_ = false;
}
void MainWindow::cancel() {
    const bool wasBusy = busy_;
    ++generation_;
    setBusy(false);
    holding_ = false; heldPieces_.clear();
    st_cancel(session_);
    ocr_.cancel(); translation_.cancel();
    restoreWindows();
    if (wasBusy) {
        subtitleOverlay_->hide();
        setStatus(QStringLiteral("처리를 취소했어요. 이미 전송된 요청의 비용은 취소되지 않을 수 있어요."));
        resultStatus_->setText(QStringLiteral("취소했어요."));
    }
}
bool MainWindow::moveResultOutside(const QRect& source) {
    if (!resultWindow_->isVisible() || resultWindow_->isMinimized() ||
        !resultWindow_->frameGeometry().intersects(source)) return true;
    auto screens = QGuiApplication::screens();
    if (selectedScreen_) { screens.removeAll(selectedScreen_.data()); screens.prepend(selectedScreen_.data()); }
    for (QScreen* screen : screens) {
        const QRect available = screen->availableGeometry().adjusted(4,4,-4,-4);
        const QSize size = resultWindow_->frameGeometry().size(); // Includes native borders/title bar.
        if (size.width() > available.width() || size.height() > available.height()) continue;
        const QList<QPoint> candidates{
            QPoint(source.left(), source.bottom()+5),
            QPoint(source.left(), source.top()-size.height()-4),
            QPoint(source.right()+5, source.top()),
            QPoint(source.left()-size.width()-4, source.top()),
            available.topLeft()
        };
        for (const QPoint& point : candidates) {
            const QPoint clamped(std::clamp(point.x(), available.left(), available.right()-size.width()+1),
                                 std::clamp(point.y(), available.top(), available.bottom()-size.height()+1));
            const QRect target(clamped,size);
            if (target.intersects(source)) continue;
            resultWindow_->move(target.topLeft());
            if (!resultWindow_->frameGeometry().intersects(source)) return true;
        }
    }
    return false;
}
void MainWindow::stopAutoForResultOverlap() {
    setAutoTranslate(false);
    resultPlacementWarning_ = QStringLiteral("결과 창을 놓을 빈 공간이 없어 자동 번역을 멈췄어요. 자막 영역을 줄이거나 결과 창을 다른 모니터로 옮긴 뒤 Ctrl+Alt+P로 다시 시작하세요.");
    setStatus(resultPlacementWarning_);
    resultStatus_->setText(resultPlacementWarning_);
}
void MainWindow::showResult() {
    resultWindow_->show();
    if (autoTranslating() && !moveResultOutside(region_.translated(selectedGeometry_.topLeft())))
        stopAutoForResultOverlap();
}
bool MainWindow::showSubtitle(const QString& text, bool selectionPreview) {
    SubtitleOverlay* overlay = selectionPreview ? subtitleOverlay_ : activeOverlay();
    // Hidden by the user: nothing on screen, and no result window popping up in its place either.
    if (captionsHidden_ && !selectionPreview) { overlay->hide(); return true; }
    const auto area = selectionPreview || captionArea_.rect.isEmpty() ? selectedArea() : captionArea_;
    if (!inlineCaptions_->isChecked() || !area.screen || area.rect.isEmpty() ||
        area.screen->geometry() != area.screenGeometry || area.screen->devicePixelRatio() != area.dpr) {
        overlay->hide(); return false;
    }
    return overlay->showText(text, area.rect.translated(area.screenGeometry.topLeft()),
                             area.screen->availableGeometry(), areaRects(selectionPreview ? 0 : activeArea_));
}
void MainWindow::presentTranslation() {
    stage_->setContent(original_->toPlainText(), result_->toPlainText());
    // An extra region without room for its caption keeps it in the stage and the backlog only.
    if (activeSource_ == ST_SCREEN_CAPTURE && (showSubtitle(result_->toPlainText()) || activeArea_ > 0)) return;
    showResult();
}
void MainWindow::previewSubtitle() {
    if (busy_ || modal_) return;
    followGame(false);
    if (!selectedScreen_ || region_.isEmpty()) {
        setStatus(QStringLiteral("먼저 원문 글자 부분만 자막 영역으로 선택하세요.")); return;
    }
    inlineCaptions_->setChecked(true);
    if (showSubtitle(QStringLiteral("번역 자막은 여기에 나와요 (미리보기)"), true))
        setStatus(QStringLiteral("미리보기 글자가 번역 위치이에요. 앱을 최소화하고 Ctrl+Alt+T로 번역하세요."));
    else setStatus(QStringLiteral("자막을 표시할 빈 공간이 없어요. 원문 글자 부분만 영역으로 다시 선택하세요."));
}
bool MainWindow::nativeEvent(const QByteArray&, void* message, qintptr* result) {
    auto* msg = static_cast<MSG*>(message);
    if (msg->message == WM_HOTKEY && msg->wParam == 1) {
        startCapture(); *result = 0; return true;
    }
    if (msg->message == WM_HOTKEY && msg->wParam == 2) {
        setAutoTranslate(!autoTranslating()); *result = 0; return true;
    }
    if (msg->message == WM_HOTKEY && msg->wParam == 3) {
        startCursorCapture(); *result = 0; return true;
    }
    if (msg->message == WM_HOTKEY && msg->wParam == 4) {
        setHoverTranslate(!hoverMode_->isChecked()); *result = 0; return true;
    }
    if (msg->message == WM_HOTKEY && msg->wParam == 6) {
        setCaptionsHidden(!captionsHidden_); *result = 0; return true;
    }
    if (msg->message == WM_HOTKEY && msg->wParam == 5) {
        // Queued: the selector runs its own event loop, which must not start inside this native event.
        QTimer::singleShot(0, this, &MainWindow::chooseRegionUnderCursor); *result = 0; return true;
    }
    return false;
}
void MainWindow::closeEvent(QCloseEvent* event) {
    // Like other overlay translators, closing the window keeps the app in the tray with its shortcuts.
    if (!quitting_ && tray_ && tray_->isVisible() && QSettings().value("ui/closeToTray", true).toBool()) {
        event->ignore();
        hide();
        if (!QSettings().value("ui/trayNoticeShown", false).toBool()) {
            tray_->showMessage(QStringLiteral("자막 번역은 트레이에서 실행 중이에요"),
                QStringLiteral("단축키와 자동 번역은 그대로 동작해요. 끝내려면 트레이 아이콘을 오른쪽 클릭하세요."),
                appIcon(), 5000);
            QSettings().setValue("ui/trayNoticeShown", true);
        }
        return;
    }
    hoverTimer_->stop();
    autoTimer_->stop();
    cancel();
    resultWindow_->hide(); subtitleOverlay_->hide(); captureFlash_->hide();
    event->accept();
    QCoreApplication::quit();
}

void MainWindow::resetOutput() {
    resultPlacementWarning_.clear();
    original_->clear(); result_->clear(); preview_->clear();
    activeOverlay()->hide(); restoreOverlay_ = false;
    resultStatus_->setText(QStringLiteral("이미지를 읽는 중이에요."));
}
void MainWindow::processImage(const QImage& image, int source, const QList<QRect>& checkBoxes) {
    resultPlacementWarning_.clear();
    activeSource_ = source;
    checking_ = !checkBoxes.isEmpty();
    if (checking_) { ++checkReads_; checkStatus_ = status_->text(); }
    // Every image source enters here. The core sees only portable metadata and UTF-8 OCR.
    dropHeld(); // a new read replaces a line still waiting to be sent
    request_ = st_begin(session_, source, image.width(), image.height());
    if (!request_) { finish(QStringLiteral("이미지가 너무 크거나 이전 요청이 진행 중이에요.")); return; }
    lastImage_ = image; lastSource_ = source;
    if (source == ST_SCREEN_CAPTURE) lastCaptionArea_ = captionArea_;
    setBusy(true);
    preview_->setPixmap(QPixmap::fromImage(image).scaled(preview_->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    if (!checking_) setStatus(QStringLiteral("PC에서 OCR 처리 중…"));
    ocr_.recognize(image, executable_, tessdata_, psm_, ocrLanguages(), checkBoxes);
}
void MainWindow::importImage() {
    if (busy_ || modal_) return;
    setHoverTranslate(false);
    modal_ = true;
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("번역할 이미지 선택"), {},
        QStringLiteral("Images (*.png *.jpg *.jpeg *.bmp *.webp)"));
    modal_ = false;
    if (path.isEmpty()) return;
    activeArea_ = 0; autoRead_ = false; checking_ = false;
    resetOutput(); setBusy(true); activeSource_ = ST_IMAGE_FILE;
    setStatus(QStringLiteral("이미지 읽는 중…"));
    const quint64 ticket = ++generation_;
    auto* watcher = new QFutureWatcher<QImage>(this);
    connect(watcher, &QFutureWatcher<QImage>::finished, this, [this, watcher, ticket] {
        const auto image = watcher->result();
        watcher->deleteLater();
        if (ticket != generation_ || !busy_) return;
        if (image.isNull()) {
            finish(QStringLiteral("이미지를 읽지 못했어요. PNG/JPEG 형식과 최대 1,600만 픽셀 제한을 확인하세요."));
            return;
        }
        processImage(image, ST_IMAGE_FILE);
    });
    watcher->setFuture(QtConcurrent::run([path] {
        QImageReader reader(path); reader.setAutoTransform(true);
        const QSize size = reader.size();
        if (!size.isValid() || qint64(size.width()) * size.height() > 16000000) return QImage();
        return reader.read();
    }));
}
bool MainWindow::autoTranslating() const { return autoTimer_->isActive(); }
void MainWindow::setAutoTranslate(bool enabled) {
    if (enabled) setHoverTranslate(false);
    const QSignalBlocker blocker(auto_);
    if (enabled && (busy_ || modal_)) {
        auto_->setChecked(autoTranslating());
        setStatus(QStringLiteral("현재 처리를 마치거나 취소한 뒤 자동 번역을 켜세요."));
        return;
    }
    detector_.reset();
    mainRead_ = {}; pendingText_.clear(); typewriterSeen_ = false;
    for (const auto& extra : extras_) { extra->detector.reset(); extra->read = {}; }
    if (!enabled) {
        dropHeld();
        autoPending_ = false;
        auto_->setChecked(false);
        if (!autoTimer_->isActive()) return;
        autoTimer_->stop();
        subtitleOverlay_->hide();
        for (const auto& extra : extras_) extra->overlay->hide();
        setStatus(QStringLiteral("자동 번역을 껐어요. Ctrl + Alt + P로 다시 켜거나 Ctrl + Alt + T로 한 번씩 번역하세요."));
        return;
    }
    if (!hasRegion()) {
        auto_->setChecked(false);
        autoPending_ = true;
        setStatus(QStringLiteral("먼저 자막 영역을 선택하세요. 선택하면 자동 번역이 바로 시작돼요."));
        return;
    }
    autoPending_ = false;
    resultPlacementWarning_.clear();
    warmTranslation();
    auto_->setChecked(true);
    autoTimer_->start();
    showMinimized(); // Keep the app window off the game's subtitle.
    const bool visible = followGame();
    setStatus(visible ? QStringLiteral("자동 번역을 켰어요. 대사가 바뀌면 바로 번역해요. Ctrl+Alt+P로 끌 수 있어요.")
        : QStringLiteral("자동 번역 대기 중이에요. %1 창이 보이면 읽기 시작해요. Ctrl+Alt+P로 끌 수 있어요.").arg(game_.name));
    captionArea_ = selectedArea();
    if (visible) showSubtitle(QStringLiteral("자동 번역을 켰어요. 대사가 바뀌면 여기에 번역이 나와요."));
}
void MainWindow::autoTick() {
    if (busy_ || modal_) return;
    if (!followGame()) {
        // A minimized or closed game is a pause, not an error: keep auto mode and wait for it.
        subtitleOverlay_->hide();
        setStatus(QStringLiteral("자동 번역 대기 중이에요. %1 창이 최소화되었거나 닫혀 있어요. 창이 보이면 다시 읽어요.").arg(game_.name));
        return;
    }
    if (!selectedScreen_ || region_.isEmpty() || selectedScreen_->geometry() != selectedGeometry_ ||
        selectedScreen_->devicePixelRatio() != selectedDpr_) {
        setAutoTranslate(false);
        setStatus(QStringLiteral("모니터 구성이 바뀌어 자동 번역을 멈췄어요. 영역을 다시 선택하세요."));
        return;
    }
    if (!ocrReady()) {
        setAutoTranslate(false);
        setStatus(QStringLiteral("OCR 엔진이 없어요. 설정에서 tesseract.exe 경로를 지정하거나 PaddleOCR 모델을 확인하세요."));
        return;
    }
    const QRect source = region_.translated(selectedGeometry_.topLeft());
    if (resultWindow_->isVisible() && !resultWindow_->isMinimized() &&
        resultWindow_->frameGeometry().intersects(source)) {
        if (!moveResultOutside(source)) stopAutoForResultOverlap();
        return; // Let the compositor repaint before the next capture; never OCR our old result.
    }
    // Polling does not hide windows (that would flicker), so skip frames our own windows cover.
    if (coversRegion(source)) {
        setStatus(QStringLiteral("앱 창이 자막 영역을 가리고 있어 자동 번역을 잠시 건너뛰어요. 창을 옮기거나 최소화하세요."));
        return;
    }
    QString error;
    const QImage image = Capture::region(selectedScreen_, region_, error);
    if (image.isNull()) {
        setAutoTranslate(false);
        setStatus(error);
        return;
    }
    const qint64 now = autoClock_.elapsed();
    const auto decision = detector_.feed(image, now);
    // Keep the DeepL connection open while a game is being watched: the server closes an idle one,
    // and the next line would wait for a new one (opening it sends no text and costs no quota).
    warmTranslation();
    // Look often only while something is happening.
    bool lively = detector_.stableMs(now) < ChangeDetector::FastPollForMs;
    for (const auto& extra : extras_) lively = lively || extra->detector.stableMs(now) < ChangeDetector::FastPollForMs;
    const int interval = lively || holding_ ? ChangeDetector::PollMs : ChangeDetector::IdlePollMs;
    if (autoTimer_->interval() != interval && autoTimer_->interval() <= ChangeDetector::IdlePollMs) autoTimer_->setInterval(interval);
    if (holding_ && decision != ChangeDetector::Decision::Process) {
        // An arrow or the background may keep moving; only text that is still arriving postpones
        // the send. A new read (below) replaces the held line, and a line held from a read taken
        // while text was still arriving waits for the read that follows it.
        if (detector_.stableMs(now) >= ChangeDetector::SettleMs && now - pendingSince_ >= ChangeDetector::SentenceSettleMs &&
            !detector_.readOwed()) {
            holding_ = false; pendingText_.clear();
            setBusy(true);
            requestTranslation(heldPieces_, heldScripts_);
        }
        return;
    }
    if (decision == ChangeDetector::Decision::Process) {
        activeArea_ = 0; autoRead_ = true;
        captionArea_ = selectedArea();
        const QList<QRect> check = checkBoxes(mainRead_, detector_, image);
        // Keep the previous caption on screen until the new line has a result. A check keeps the
        // texts too: it usually finds the same line.
        if (check.isEmpty()) { original_->clear(); result_->clear(); }
        processImage(image, ST_SCREEN_CAPTURE, check);
        return;
    }
    // Extra regions: each has its own change check; at most one read per tick, after the main one.
    for (size_t i = 0; i < extras_.size(); ++i) {
        ExtraArea& extra = *extras_[i];
        if (!extraInUse(extra) || extra.screen->geometry() != extra.screenGeometry) continue;
        if (coversRegion(extra.rect.translated(extra.screenGeometry.topLeft()))) continue;
        QString ignored;
        const QImage frame = Capture::region(extra.screen, extra.rect, ignored);
        if (frame.isNull() || extra.detector.feed(frame, now) != ChangeDetector::Decision::Process) continue;
        activeArea_ = int(i) + 1; autoRead_ = true;
        captionArea_ = {extra.screen, extra.screenGeometry, extra.rect, extra.dpr};
        const QList<QRect> check = checkBoxes(extra.read, extra.detector, frame);
        if (check.isEmpty()) { original_->clear(); result_->clear(); }
        processImage(frame, ST_SCREEN_CAPTURE, check);
        return;
    }
}
bool MainWindow::coversRegion(const QRect& global) const {
    for (const auto& extra : extras_)
        if (extra->overlay->isVisible() && extra->overlay->geometry().intersects(global)) return true;
    return (isVisible() && !isMinimized() && frameGeometry().intersects(global)) ||
           (resultWindow_->isVisible() && !resultWindow_->isMinimized() && resultWindow_->frameGeometry().intersects(global)) ||
           (subtitleOverlay_->isVisible() && subtitleOverlay_->geometry().intersects(global));
}
bool MainWindow::hasRegion() const {
    return !game_.key.isEmpty() || (selectedScreen_ && !region_.isEmpty());
}
void MainWindow::setRegion(QScreen* screen, const QRect& region) {
    selectedScreen_ = screen; region_ = region;
    selectedGeometry_ = screen->geometry(); selectedDpr_ = screen->devicePixelRatio();
    game_ = {};
    if (followGame_->isChecked()) {
        // Remember the game under the region: its pixels, relative to its client area.
        const QRect global = region.translated(selectedGeometry_.topLeft());
        const GameWindow::Info info = GameWindow::at(global.center(), ignoreProcess_);
        const QRect inside = info.isValid() ? GameWindow::toPhysical(global).intersected(info.client) : QRect();
        if (inside.width() >= 8 && inside.height() >= 8)
            game_ = {info.key, gameLabel(info), GameWindow::relativeTo(inside, info.client), info.client.size(), info.handle};
    }
    regionLabel_->setText(describeRegion(screen, region_) +
        (game_.key.isEmpty() ? QString() : QStringLiteral(" (게임 창 %1 기준)").arg(game_.name)));
    saveRegion();
    detector_.reset();
}
void MainWindow::rememberGame(const GameRegion& game) {
    knownGames_.insert(game.key, game);
    QSettings saved;
    saved.beginGroup(gameGroup(game.key));
    saved.setValue("key", game.key); saved.setValue("name", game.name);
    saved.setValue("region", game.relative); saved.setValue("client", game.client);
    saved.setValue("ocrLanguages", ocrLanguages());
    saved.endGroup();
    saved.setValue("capture/game", game.key);
}
bool MainWindow::followGame(bool switchToForeground) {
    if (!followGame_->isChecked()) return true;
    if (switchToForeground) {
        // Another game with its own saved region came to the front: read that game's region instead.
        const GameWindow::Info front = foregroundWindow_();
        if (front.isValid() && front.key != game_.key && knownGames_.contains(front.key)) {
            game_ = knownGames_.value(front.key);
            game_.handle = front.handle;
            QSettings().setValue("capture/game", game_.key);
            detector_.reset();
            restoreGameLanguage();
        }
    }
    if (game_.key.isEmpty()) return true;
    const GameWindow::Info info = GameWindow::find(game_.key, game_.handle);
    if (!info.isValid()) return false;
    game_.handle = info.handle;
    // P-41: a region saved when games were named by their program file ("hspy.exe") takes the window
    // title once the window is found. Only then: some games keep changing their title.
    if (game_.name.endsWith(QLatin1String(".exe"), Qt::CaseInsensitive) && !info.title.trimmed().isEmpty()) {
        game_.name = gameLabel(info);
        if (knownGames_.contains(game_.key)) knownGames_[game_.key].name = game_.name;
        QSettings().setValue(gameGroup(game_.key) + QStringLiteral("/name"), game_.name);
    }
    const GameWindow::Area area = GameWindow::toScreen(GameWindow::place(game_.relative, game_.client, info.client));
    if (!area.screen || area.rect.width() < 8 || area.rect.height() < 8) return false;
    if (area.screen != selectedScreen_ || area.rect != region_ || area.screen->geometry() != selectedGeometry_ ||
        area.screen->devicePixelRatio() != selectedDpr_) {
        selectedScreen_ = area.screen; region_ = area.rect;
        selectedGeometry_ = area.screen->geometry(); selectedDpr_ = area.screen->devicePixelRatio();
        regionLabel_->setText(describeRegion(area.screen, region_) + QStringLiteral(" (게임 창 %1을(를) 따라감)").arg(game_.name));
    }
    for (const auto& extra : extras_) {
        if (extra->gameKey != game_.key || extra->relative.isEmpty()) continue;
        const GameWindow::Area at = GameWindow::toScreen(GameWindow::place(extra->relative, extra->client, info.client));
        if (!at.screen) continue;
        extra->screen = at.screen; extra->screenGeometry = at.screen->geometry();
        extra->dpr = at.screen->devicePixelRatio(); extra->rect = at.rect;
    }
    return true;
}
void MainWindow::saveRegion() {
    QSettings saved;
    saved.setValue("capture/screen", selectedScreen_->name());
    saved.setValue("capture/screenGeometry", selectedGeometry_);
    saved.setValue("capture/devicePixelRatio", selectedDpr_);
    saved.setValue("capture/region", region_);
    if (game_.key.isEmpty()) saved.remove("capture/game");
    else rememberGame(game_);
}
void MainWindow::restoreRegion() {
    QSettings saved;
    saved.beginGroup(QStringLiteral("games"));
    const QStringList groups = saved.childGroups();
    for (const QString& group : groups) {
        saved.beginGroup(group);
        const GameRegion game{saved.value("key").toString(), saved.value("name").toString(),
                              saved.value("region").toRect(), saved.value("client").toSize()};
        saved.endGroup();
        if (!game.key.isEmpty() && !game.relative.isEmpty() && !game.client.isEmpty()) knownGames_.insert(game.key, game);
    }
    saved.endGroup();
    if (followGame_->isChecked()) game_ = knownGames_.value(saved.value("capture/game").toString());
    if (!game_.key.isEmpty()) {
        if (followGame(false)) { regionLabel_->setText(regionLabel_->text() + QStringLiteral(" (저장된 영역)")); return; }
        regionLabel_->setText(QStringLiteral("%1 창 기준으로 저장된 영역이에요. 게임을 실행하면 위치를 다시 맞춰요.").arg(game_.name));
        return;
    }
    const QRect region = saved.value("capture/region").toRect();
    if (region.isEmpty()) return;
    const auto list = QGuiApplication::screens();
    for (int i = 0; i < list.size(); ++i) {
        QScreen* screen = list[i];
        if (screen->name() != saved.value("capture/screen").toString()) continue;
        // A saved region is only meaningful on the same resolution and scale.
        if (screen->geometry() != saved.value("capture/screenGeometry").toRect() ||
            !qFuzzyCompare(screen->devicePixelRatio(), saved.value("capture/devicePixelRatio").toReal())) break;
        selectedScreen_ = screen; selectedGeometry_ = screen->geometry();
        selectedDpr_ = screen->devicePixelRatio(); region_ = region;
        screens_->setCurrentIndex(i);
        regionLabel_->setText(describeRegion(screen, region_) + QStringLiteral(" (저장된 영역)"));
        return;
    }
    regionLabel_->setText(QStringLiteral("저장한 영역의 모니터 구성이 바뀌었어요. 영역을 다시 선택하세요."));
}
QString MainWindow::ocrLanguages() const { return ocrLanguageBox_->currentData().toString(); }
QString MainWindow::targetLanguage() const { return targetLanguageBox_->currentData().toString(); }
void MainWindow::applyOcrEngine() {
    // With Tesseract chosen, the bundled detection model still checks that a text line exists.
    ocr_.setPaddleModelDir(QDir(paddleDir_).exists() ? paddleDir_ : QString(), paddleEngine_);
    ocr_.warmUp(ocrLanguages());
}
bool MainWindow::ocrReady() const {
    return (paddleEngine_ && PaddleOcr::available(paddleDir_, ocrLanguages())) ||
           (ocrLanguages() != QLatin1String("multi") && !executable_.isEmpty() && QFileInfo::exists(executable_));
}
bool MainWindow::fromPieceMemory(const QStringList& pieces) {
    pendingPieces_.clear();
    if (!prefixShown_.isEmpty() || pieces.isEmpty()) return false;
    QStringList known;
    QString detected;
    for (const QString& piece : pieces) {
        const auto found = pieceCache_.constFind(memoryKey(lineKey(piece)));
        if (found == pieceCache_.constEnd()) { pendingPieces_ = pieces; return false; }
        known << found->translation; detected = found->detected;
    }
    for (const QString& piece : pieces) sparedCharacters_ += piece.size();
    pendingCacheKey_.clear();
    applyTranslation(known, detected, true);
    return true;
}
void MainWindow::requestTranslation(const QStringList& pieces, unsigned scripts) {
    if (fromPieceMemory(pieces)) return;
    const QString source = Languages::sourceForScripts(scripts, ocrLanguages());
    if (autoRead_) {
        const qint64 now = autoClock_.elapsed();
        while (!sendTimes_.isEmpty() && now - sendTimes_.first() > FuseWindowMs) sendTimes_.removeFirst();
        if (sendTimes_.size() >= FuseRequests) {
            // Something keeps producing new text (a moving picture inside the region, a misread
            // that changes every time). Stop before it spends the quota.
            translationPausedUntil_ = now + FusePauseMs;
            st_finish_ocr(session_, request_);
            resultStatus_->setText(QStringLiteral("번역 요청이 너무 잦아서 잠시 보내지 않고 있어요."));
            finish(QStringLiteral("%1초 동안 번역 요청이 %2번을 넘어서, 한도를 아끼려고 %3초 동안 원문만 표시해요.\n영역에 움직이는 그림이 들어 있으면 글자 부분만 다시 선택해 보세요.")
                .arg(FuseWindowMs / 1000).arg(FuseRequests).arg(FusePauseMs / 1000));
            return;
        }
        sendTimes_.append(now);
    }
    pendingCacheKey_ = memoryKey(currentKey_);
    ++sentRequests_;
    for (const QString& piece : pieces) sentCharacters_ += piece.size();
    sparedCharacters_ += prefixSource_.trimmed().size();
    if (++sentSinceUsage_ >= UsageEveryRequests) refreshUsage();
    setStatus(QStringLiteral("DeepL에서 %1(으)로 번역 중…").arg(Languages::targetLabel(targetLanguage())));
    resultStatus_->setText(QStringLiteral("번역 중…"));
    const QString current = original_->toPlainText();
    // The part already translated is the best context for the rest; otherwise the line before.
    const QString context = !prefixSource_.isEmpty() ? prefixSource_
                          : contextOn_ && contextLine_ != current ? contextLine_ : QString();
    translation_.translate(pieces, apiKey_, freeApi_, targetLanguage(), source, context);
    contextLine_ = current;
}
QString MainWindow::memoryKey(const QString& lineKey) const {
    return QStringList{targetLanguage(), ocrLanguages(), lineKey}.join(QChar(0x1f));
}
MainWindow::ReadState* MainWindow::readState() {
    if (!autoRead_ || activeArea_ < 0 || activeArea_ > int(extras_.size())) return nullptr;
    return activeArea_ == 0 ? &mainRead_ : &extras_[size_t(activeArea_) - 1]->read;
}
bool MainWindow::doubtful() const {
    if (ocr_.engineUsed() != QLatin1String("PaddleOCR")) return false;
    const auto& lines = ocr_.paddleDetails().lines;
    return std::any_of(lines.begin(), lines.end(), [](const auto& line) { return line.lowestLetter < DoubtfulBelow; });
}
QList<QRect> MainWindow::checkBoxes(ReadState& state, const ChangeDetector& detector, const QImage& image) const {
    // Only a look (nothing beyond known animation was seen) of a picture whose lines are known.
    if (!detector.onlyLooking() || state.lastRead.isEmpty() || state.boxesSize != image.size()) { state.checksInRow = 0; return {}; }
    if (++state.checksInRow >= CheckEvery) { state.checksInRow = 0; return {}; }
    return state.boxes;
}
void MainWindow::forgetShownLines() {
    mainRead_ = {};
    for (const auto& extra : extras_) extra->read = {};
}
void MainWindow::showRemembered(const Remembered& line) {
    // The user's fixes apply now, so a fix added after the line was translated is used too.
    const QString shown = applyFixes(line.translation, true);
    if (!st_complete(session_, request_, shown.toUtf8().constData())) return;
    result_->setPlainText(shown);
    addToBacklog(original_->toPlainText(), shown);
    if (ReadState* state = readState()) { state->shownKey = currentKey_; state->shown = line.translation; state->variant.clear(); }
    resultStatus_->setText(QStringLiteral("전에 번역한 대사라서 기억해 둔 번역을 보여 줘요."));
    presentTranslation();
    finish(QStringLiteral("기억해 둔 번역이라 번역 요청을 보내지 않았어요."));
}
void MainWindow::applyTranslation(const QStringList& pieces, const QString& detected, bool remembered) {
    std::vector<std::string> translations;
    for (const QString& piece : pieces) translations.push_back(piece.toStdString());
    const std::string joined = subtitle::joinTranslated(segments_, translations);
    if (joined.empty()) {
        st_fail(session_, request_);
        resultStatus_->setText(QStringLiteral("번역하지 못했어요. 원문은 더 보기의 인식 내용에서 볼 수 있어요."));
        finish(QStringLiteral("번역 응답 개수가 요청과 달라 결과를 표시하지 않았어요."));
        return;
    }
    QString translation = QString::fromStdString(joined);
    if (!prefixShown_.isEmpty()) {
        // Only the new part was sent: put it next to the part translated before.
        const QString target = targetLanguage().section('-', 0, 0);
        const QString between = target == QLatin1String("JA") || target == QLatin1String("ZH") ? QString() : QStringLiteral(" ");
        translation = keptComesLast_ ? translation + between + prefixShown_ : prefixShown_ + between + translation;
    }
    const QString shown = applyFixes(translation, true);
    if (!st_complete(session_, request_, shown.toUtf8().constData())) return;
    if (!remembered && !pendingCacheKey_.isEmpty() && !lineCache_.contains(pendingCacheKey_)) {
        if (lineCache_.size() >= CacheLines) lineCache_.remove(cacheOrder_.takeFirst());
        lineCache_.insert(pendingCacheKey_, {translation, detected});
        cacheOrder_.append(pendingCacheKey_);
    }
    if (!remembered && pendingPieces_.size() == pieces.size()) {
        for (qsizetype i = 0; i < pieces.size(); ++i) {
            const QString key = memoryKey(lineKey(pendingPieces_[i]));
            if (pieceCache_.contains(key)) continue;
            if (pieceCache_.size() >= CacheLines) pieceCache_.remove(pieceOrder_.takeFirst());
            pieceCache_.insert(key, {pieces[i], detected});
            pieceOrder_.append(key);
        }
    }
    pendingPieces_.clear();
    if (ReadState* state = readState()) { state->shownKey = currentKey_; state->shown = translation; state->variant.clear(); }
    result_->setPlainText(shown);
    addToBacklog(original_->toPlainText(), shown);
    const QString target = Languages::targetLabel(targetLanguage());
    const bool partial = std::any_of(segments_.begin(), segments_.end(), [](const auto& s) { return !s.translate; });
    resultStatus_->setText(Languages::sameLanguage(detected, targetLanguage())
        ? QStringLiteral("원문이 이미 %1라서 DeepL이 그대로 돌려줬어요.").arg(target)
        : QStringLiteral("%1에서 %2로 번역했어요%3.")
              .arg(detected.isEmpty() ? QStringLiteral("원문") : Languages::languageName(detected), target,
                   (partial ? QStringLiteral(" (외국어 부분만)") : QString()) +
                   (remembered ? QStringLiteral(" (기억해 둔 번역)") : QString())));
    presentTranslation();
    finish(autoTranslating() ? QStringLiteral("번역했어요. 다음 대사를 기다리고 있어요.")
                             : QStringLiteral("번역 완료. 다음 자막에서 Ctrl + Alt + T를 누르세요."));
}
void MainWindow::dropHeld() {
    if (!holding_) return;
    holding_ = false; heldPieces_.clear();
    st_fail(session_, request_);
}
void MainWindow::warmTranslation() {
    if (qApp->property("testMode").toBool() && !translation_.hasTestEndpoint()) return; // tests never reach DeepL
    if (!apiKey_.isEmpty() && consent_) translation_.warmUp(freeApi_);
}
QString MainWindow::lineKey(const QString& text) {
    static const QString arrows = QStringLiteral("▼▽▾▿↓⇩▶►▸▷⏎↵");
    QString key;
    key.reserve(text.size());
    for (const QChar ch : text)
        if (!ch.isSpace()) key.append(ch);
    while (!key.isEmpty() && arrows.contains(key.back())) key.chop(1);
    // Final periods the OCR misses (or doubles) in some fonts or frames are not a new line (P-12: 25 of
    // 720 lines were read without "。", one of them both ways and sent twice). "？" "！" change the
    // meaning and stay, and so do "…" "‥".
    static const QString periods = QStringLiteral("。．.");
    while (key.size() >= 2 && periods.contains(key.back())) key.chop(1);
    return key;
}
bool MainWindow::looksFinished(const QString& text) {
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) return true;
    static const QString endings = QStringLiteral("。．.！!？?…‥」』）)】〉》\"”’'～~♪♡♥☆★‼⁉⁈・･^");
    if (endings.contains(trimmed.back())) return true;
    // P-40: a line that ends in an emoticon is finished too. Emoji and symbols such as ♫ ✨ ❤️
    // (the last code point, past a variation selector, joiner or skin tone).
    const QList<uint> points = trimmed.toUcs4();
    qsizetype end = points.size();
    auto modifier = [](uint c) { return c == 0xFE0F || c == 0xFE0E || c == 0x200D || (c >= 0x1F3FB && c <= 0x1F3FF); };
    while (end > 0 && modifier(points[end - 1])) --end;
    if (end == 0) return true;
    const uint last = points[end - 1];
    if (QChar::category(last) == QChar::Symbol_Other) return true;
    // A run of the same mark: ㅋㅋ ㅠㅠ ㅇㅇ, ｗｗｗ (not "aww"), laughter あはは えへへ 哈哈, 666.
    qsizetype run = 1;
    while (end - run > 0 && points[end - run - 1] == last) ++run;
    const bool jamo = last >= 0x3131 && last <= 0x318E;
    if (jamo && run >= 2) return true;
    if (run >= 2 && (last == 'w' || last == 'W' || last == 0xFF57 || last == 0xFF37)) {
        const qsizetype before = end - run - 1;
        if (before < 0 || !(QChar::isLetter(points[before]) && QChar::script(points[before]) == QChar::Script_Latin)) return true;
    }
    static const QString laughs = QStringLiteral("はハへヘふフひヒ哈呵嘿嘻");
    if (run >= 2 && last <= 0xFFFF && laughs.contains(QChar(char16_t(last)))) return true;
    if (run >= 3 && last == '6') return true;
    // An emoticon word after a space: "orz", "XD", ":D", "<3", "T_T".
    const qsizetype space = trimmed.lastIndexOf(QLatin1Char(' '));
    if (space > 0) {
        const QString word = trimmed.mid(space + 1);
        static const QStringList words = {QStringLiteral("orz"), QStringLiteral("OTL"), QStringLiteral("lol"), QStringLiteral("LOL"),
                                          QStringLiteral("xD"), QStringLiteral("XD"), QStringLiteral("<3")};
        if (words.contains(word)) return true;
        if (word.size() >= 2 && word.size() <= 4 && QStringLiteral(":;=").contains(word.front())) return true;
        if (word.size() >= 3 && word.size() <= 5 && word.contains(QLatin1Char('_'))) return true;
    }
    return false;
}
void MainWindow::restoreGameLanguage() {
    if (game_.key.isEmpty()) return;
    const QString saved = QSettings().value(gameGroup(game_.key) + QStringLiteral("/ocrLanguages")).toString();
    const int index = ocrLanguageBox_->findData(saved);
    if (index >= 0 && index != ocrLanguageBox_->currentIndex()) ocrLanguageBox_->setCurrentIndex(index);
}
void MainWindow::loadFixes() {
    fixes_.clear();
    QSettings saved;
    const int count = saved.beginReadArray("fixes");
    for (int i = 0; i < count; ++i) {
        saved.setArrayIndex(i);
        const TextFix fix{saved.value("onTranslation", true).toBool(), saved.value("find").toString(), saved.value("replace").toString()};
        if (!fix.find.isEmpty()) fixes_ << fix;
    }
    saved.endArray();
}
QString MainWindow::applyFixes(QString text, bool onTranslation) const {
    for (const TextFix& fix : fixes_)
        if (fix.onTranslation == onTranslation) text.replace(fix.find, fix.replace);
    return text;
}
void MainWindow::refreshUsage() {
    if (apiKey_.isEmpty() || !consent_ || usage_.isBusy()) return;
    if (qApp->property("testMode").toBool() && !usage_.hasTestEndpoint()) return; // tests never reach DeepL
    usage_.checkUsage(apiKey_, freeApi_);
}
void MainWindow::setCaptionsHidden(bool hidden) {
    captionsHidden_ = hidden;
    if (hidden) {
        subtitleOverlay_->hide();
        for (const auto& extra : extras_) extra->overlay->hide();
        setStatus(QStringLiteral("자막을 숨겼어요. 읽기와 번역은 계속해요. Ctrl+Alt+V로 다시 보여요."));
    } else {
        if (lastSource_ == ST_SCREEN_CAPTURE && !result_->toPlainText().isEmpty()) showSubtitle(result_->toPlainText());
        setStatus(QStringLiteral("자막을 다시 보여요."));
    }
}
QString MainWindow::diagnostics() const {
    // States and timings only: what was read or translated never goes into a report.
    const PaddleOcr::Result& paddle = ocr_.paddleDetails();
    QStringList lines;
    lines << QStringLiteral("자막 번역 %1 (빌드 %2), Qt %3").arg(QCoreApplication::applicationVersion(),
                                                                qApp->property("buildId").toString().isEmpty() ? QStringLiteral("모름") : qApp->property("buildId").toString(),
                                                                QString::fromLatin1(qVersion()))
          << QStringLiteral("OCR 엔진 %1, 게임 언어 %2, 번역 언어 %3, OCR 스레드 %4")
                 .arg(paddleEngine_ ? QStringLiteral("PaddleOCR") : QStringLiteral("Tesseract"), ocrLanguages(), targetLanguage())
                 .arg(PaddleOcr::threads())
          << QStringLiteral("PaddleOCR 모델 %1, Tesseract %2")
                 .arg(PaddleOcr::available(paddleDir_, ocrLanguages()) ? QStringLiteral("있음") : QStringLiteral("없음"),
                      QFileInfo::exists(executable_) ? QStringLiteral("있음") : QStringLiteral("없음"))
          << QStringLiteral("마지막 읽기: 엔진 %1, %2ms (검출 %3ms, 인식 %4ms), 줄 %5")
                 .arg(ocr_.engineUsed().isEmpty() ? QStringLiteral("없음") : ocr_.engineUsed()).arg(ocr_.elapsedMs())
                 .arg(qRound(paddle.detectMs)).arg(qRound(paddle.recognizeMs)).arg(paddle.lines.size())
          << QStringLiteral("번역: 키 %1, 전송 동의 %2, 요금제 %3, 마지막 HTTP %4, 사용량 %5")
                 .arg(apiKey_.isEmpty() ? QStringLiteral("없음") : QStringLiteral("있음"), consent_ ? QStringLiteral("예") : QStringLiteral("아니요"),
                      freeApi_ ? QStringLiteral("Free") : QStringLiteral("Pro")).arg(translation_.lastHttpStatus())
                 .arg(usageLimit_ > 0 ? QStringLiteral("%1%").arg(usageUsed_ * 100 / usageLimit_) : QStringLiteral("모름"))
          << QStringLiteral("이번 실행: 번역 요청 %1번, 보낸 글자 %2자, 기억으로 아낀 글자 %3자, 움직임 허용 %4%, 자동 읽기 %5번(확인만 %6번)")
                 .arg(sentRequests_).arg(sentCharacters_).arg(sparedCharacters_).arg(detector_.allowance() * 100, 0, 'f', 2)
                 .arg(fullReads_ + checkReads_).arg(checkReads_)
          << QStringLiteral("자동 번역 %1, 감지 %2ms, 대기 %3ms, 문장 대기 %4ms, 올려두기 %5, 자막 숨김 %6")
                 .arg(autoTranslating() ? QStringLiteral("켬") : QStringLiteral("끔")).arg(ChangeDetector::PollMs)
                 .arg(ChangeDetector::SettleMs).arg(ChangeDetector::SentenceSettleMs)
                 .arg(hoverMode_->isChecked() ? QStringLiteral("켬") : QStringLiteral("끔"))
                 .arg(captionsHidden_ ? QStringLiteral("예") : QStringLiteral("아니요"))
          << QStringLiteral("영역 %1, 게임 창 따라가기 %2%3")
                 .arg(hasRegion() ? QStringLiteral("있음") : QStringLiteral("없음"),
                      followGame_->isChecked() ? QStringLiteral("켬") : QStringLiteral("끔"),
                      game_.name.isEmpty() ? QString() : QStringLiteral(" (%1)").arg(game_.name));
    QStringList screens;
    for (const QScreen* screen : QGuiApplication::screens())
        screens << QStringLiteral("%1×%2 배율 %3").arg(screen->size().width()).arg(screen->size().height()).arg(screen->devicePixelRatio());
    lines << QStringLiteral("모니터 %1").arg(screens.join(QStringLiteral(", ")))
          << QStringLiteral("단축키: 영역 %1, 자동 %2, 지금 %3, 마우스 %4, 올려두기 %5, 자막 %6")
                 .arg(regionRegistered_ ? regionShortcut_ : QStringLiteral("등록 안 됨"), autoRegistered_ ? autoShortcut_ : QStringLiteral("등록 안 됨"),
                      registered_ ? captureShortcut_ : QStringLiteral("등록 안 됨"), cursorRegistered_ ? cursorShortcut_ : QStringLiteral("등록 안 됨"),
                      hoverRegistered_ ? hoverShortcut_ : QStringLiteral("등록 안 됨"), captionsRegistered_ ? captionsShortcut_ : QStringLiteral("등록 안 됨"));
    return lines.join(QLatin1Char('\n'));
}
MainWindow::ExtraArea::~ExtraArea() { delete overlay; }
bool MainWindow::extraInUse(const ExtraArea& extra) const {
    return extra.gameKey == game_.key && extra.screen && !extra.rect.isEmpty();
}
SubtitleOverlay* MainWindow::activeOverlay() const {
    return activeArea_ > 0 && activeArea_ <= int(extras_.size()) ? extras_[size_t(activeArea_ - 1)]->overlay : subtitleOverlay_;
}
int MainWindow::activeExtraCount() const {
    return int(std::count_if(extras_.begin(), extras_.end(), [this](const auto& e) { return extraInUse(*e); }));
}
QList<QRect> MainWindow::areaRects(int except) const {
    QList<QRect> rects;
    if (except != 0 && selectedScreen_ && !region_.isEmpty()) rects << region_.translated(selectedGeometry_.topLeft());
    for (size_t i = 0; i < extras_.size(); ++i)
        if (int(i) + 1 != except && extraInUse(*extras_[i])) rects << extras_[i]->rect.translated(extras_[i]->screenGeometry.topLeft());
    return rects;
}
void MainWindow::addExtraAreaAt(QScreen* screen, const QRect& rect) {
    if (!screen || rect.width() < 8 || rect.height() < 8 || int(extras_.size()) >= MaxExtraAreas) return;
    auto extra = std::make_unique<ExtraArea>();
    extra->screen = screen; extra->screenGeometry = screen->geometry(); extra->dpr = screen->devicePixelRatio(); extra->rect = rect;
    // On the main region's game window: follow that window like the main region does.
    if (!game_.key.isEmpty()) {
        const QRect global = rect.translated(extra->screenGeometry.topLeft());
        const GameWindow::Info info = GameWindow::at(global.center(), ignoreProcess_);
        const QRect inside = info.isValid() && info.key == game_.key ? GameWindow::toPhysical(global).intersected(info.client) : QRect();
        if (inside.width() >= 8 && inside.height() >= 8) {
            extra->gameKey = game_.key; extra->relative = GameWindow::relativeTo(inside, info.client); extra->client = info.client.size();
        }
    }
    // A plain screen region kept while the main one is a game's still belongs to that game, so it is
    // read only with that game in front.
    if (extra->gameKey.isEmpty()) extra->gameKey = game_.key;
    extra->overlay = new SubtitleOverlay;
    extra->overlay->setStyle(QSettings().value("display/captionScale", 100).toInt() / 100.0, QSettings().value("display/captionBox", false).toBool());
    extras_.push_back(std::move(extra));
    saveExtraAreas();
    refreshUi();
}
void MainWindow::clearExtraAreas() {
    extras_.clear();
    activeArea_ = 0;
    saveExtraAreas();
    refreshUi();
    setStatus(QStringLiteral("추가 영역을 지웠어요. 대사창 영역만 읽어요."));
}
void MainWindow::saveExtraAreas() const {
    QSettings saved;
    saved.beginWriteArray("capture/extraAreas");
    for (int i = 0; i < int(extras_.size()); ++i) {
        const ExtraArea& extra = *extras_[size_t(i)];
        saved.setArrayIndex(i);
        saved.setValue("game", extra.gameKey);
        saved.setValue("screen", extra.screen ? extra.screen->name() : QString());
        saved.setValue("screenGeometry", extra.screenGeometry);
        saved.setValue("devicePixelRatio", extra.dpr);
        saved.setValue("region", extra.rect);
        saved.setValue("relative", extra.relative);
        saved.setValue("client", extra.client);
    }
    saved.endArray();
}
void MainWindow::restoreExtraAreas() {
    QSettings saved;
    const int count = qMin(saved.beginReadArray("capture/extraAreas"), int(MaxExtraAreas));
    for (int i = 0; i < count; ++i) {
        saved.setArrayIndex(i);
        auto extra = std::make_unique<ExtraArea>();
        extra->gameKey = saved.value("game").toString();
        extra->screenGeometry = saved.value("screenGeometry").toRect();
        extra->dpr = saved.value("devicePixelRatio", 1.0).toReal();
        extra->rect = saved.value("region").toRect();
        extra->relative = saved.value("relative").toRect();
        extra->client = saved.value("client").toSize();
        for (QScreen* screen : QGuiApplication::screens())
            if (screen->name() == saved.value("screen").toString() && screen->geometry() == extra->screenGeometry) extra->screen = screen;
        extra->overlay = new SubtitleOverlay;
        extra->overlay->setStyle(QSettings().value("display/captionScale", 100).toInt() / 100.0, QSettings().value("display/captionBox", false).toBool());
        extras_.push_back(std::move(extra));
    }
    saved.endArray();
    followGame(false); // a game's extra regions take their place from its window
}
void MainWindow::addExtraArea() {
    if (busy_ || modal_) return;
    if (!hasRegion()) { setStatus(QStringLiteral("먼저 영역 선택으로 대사창을 고른 다음 영역을 추가하세요.")); return; }
    if (int(extras_.size()) >= MaxExtraAreas) {
        setStatus(QStringLiteral("추가 영역은 %1개까지예요. 더 보기에서 추가 영역을 지운 뒤 다시 고르세요.").arg(MaxExtraAreas)); return;
    }
    followGame(false);
    QPointer<QScreen> screen = selectedScreen_ ? selectedScreen_.data() : QGuiApplication::screenAt(QCursor::pos());
    if (!screen) return;
    modal_ = true;
    restoreMain_ = isVisible() && !isMinimized(); restoreResult_ = resultWindow_->isVisible();
    if (!isMinimized()) hide();
    resultWindow_->hide();
    QTimer::singleShot(180, this, [this, screen] {
        if (!modal_ || !screen) { modal_ = false; restoreWindows(); return; }
        const QPixmap snapshot = screen->grabWindow(0);
        RegionSelector selector(snapshot, screen->geometry(), QRect());
        const bool selected = !snapshot.isNull() && selector.exec() == QDialog::Accepted && screen;
        modal_ = false;
        restoreWindows();
        if (!selected) return;
        addExtraAreaAt(screen, selector.selectedRegion());
        setStatus(QStringLiteral("영역을 추가했어요. 자동 번역이 대사창과 함께 이 영역도 읽어요."));
        showAllAreas();
    });
}
void MainWindow::showAllAreas() {
    followGame(false);
    QList<QRect> rects = areaRects(-1);
    if (rects.isEmpty()) { setStatus(QStringLiteral("아직 고른 영역이 없어요.")); return; }
    for (const QRect& rect : rects) {
        auto* flash = new CaptureFlash;
        flash->flash(rect, 1500);
        QTimer::singleShot(1700, flash, &QObject::deleteLater);
    }
}
