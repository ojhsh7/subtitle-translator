#include "video_mode.h"
#include "capture_service.h"
#include "diagnostics.h"
#include "game_window.h"
#include "history_store.h"
#include "languages.h"
#include "ocr_service.h"
#include "region_selector.h"
#include "subtitle/models.h"
#include "subtitle_presenter.h"
#include "translation_queue.h"
#include "widget_helpers.h"
#include <QComboBox>
#include <QCursor>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPointer>
#include <QPushButton>
#include <QScreen>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <algorithm>

namespace {
const QString RegionKey = QStringLiteral("modes/video/capture");
}

VideoMode::VideoMode(const ModeContext& context, QWidget* parent) : QWidget(parent), ctx_(context) {
    region_ = SavedRegion::load(RegionKey);
    style_ = Modes::loadStyle(Mode::Video, ctx_.presenter->exclusionActive());
    tick_.setInterval(TickMs);
    connect(&tick_, &QTimer::timeout, this, &VideoMode::tick);
    connect(&worker_, &CaptureWorker::frameReady, this, &VideoMode::onFrame);
    connect(&worker_, &CaptureWorker::blank, this, &VideoMode::onBlank);
    connect(&worker_, &CaptureWorker::captureFailed, this, [this](const QString& error, quint64 ticket) {
        if (ticket != ticket_ || !watching_) return;
        setWatching(false);
        ctx_.status(QStringLiteral("영상 자막 번역을 멈췄습니다 · %1").arg(error));
    });
    connect(ctx_.ocr, &OcrService::recognized, this, &VideoMode::onRecognized);
    connect(ctx_.ocr, &OcrService::failed, this, &VideoMode::onOcrFailed);
    connect(ctx_.queue, &TranslationQueue::finished, this,
            [this](quint64 id, const QStringList& texts, const QString&, bool) { onTranslated(id, texts); });
    connect(ctx_.queue, &TranslationQueue::failed, this, &VideoMode::onTranslateFailed);
    connect(ctx_.queue, &TranslationQueue::dropped, this, [this](quint64 id) { requests_.remove(id); });
    buildUi();
    updateUi();
}
VideoMode::~VideoMode() {
    tick_.stop();
    if (ocrBusy_) ctx_.ocr->cancel();
    for (auto it = requests_.cbegin(); it != requests_.cend(); ++it) ctx_.queue->cancel(it.key());
}
void VideoMode::buildUi() {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);
    layout->addWidget(Ui::hint(QStringLiteral("video"),
        QStringLiteral("유튜브·뉴스·영상 화면의 자막을 읽어 번역합니다.\n"
                       "① [영상 창에 맞추기]로 자막 위치를 정하고 ② [영상 자막 번역 시작](Ctrl+Alt+2)을 누르세요.\n"
                       "넷플릭스 같은 보호된 영상은 화면이 검게 잡혀 번역할 수 없습니다.")));
    auto* regionRow = new QHBoxLayout;
    auto* fit = new QPushButton(QStringLiteral("영상 창에 맞추기 · 3초"));
    fit->setToolTip(QStringLiteral("누른 뒤 3초 안에 영상 위로 마우스를 옮기세요. 그 창의 아래쪽 자막 자리를 제안합니다."));
    auto* bottom = new QPushButton(QStringLiteral("화면 아래쪽 20%"));
    bottom->setToolTip(QStringLiteral("전체 화면 영상용. 마우스가 있는 모니터의 아래쪽을 제안합니다."));
    auto* manual = new QPushButton(QStringLiteral("직접 선택"));
    regionRow->addWidget(fit, 2); regionRow->addWidget(bottom, 1); regionRow->addWidget(manual, 1);
    layout->addLayout(regionRow);
    connect(fit, &QPushButton::clicked, this, &VideoMode::fitToWindow);
    connect(bottom, &QPushButton::clicked, this, &VideoMode::bottomOfScreen);
    connect(manual, &QPushButton::clicked, this, &VideoMode::chooseRegion);
    regionLabel_ = new QLabel;
    regionLabel_->setWordWrap(true);
    layout->addWidget(regionLabel_);

    auto* styleGrid = new QGridLayout;
    placement_ = new QComboBox;
    placement_->addItem(QStringLiteral("원문 자막 위에 덮기"), int(SubtitleOverlay::Placement::Cover));
    placement_->addItem(QStringLiteral("원문 아래에 표시"), int(SubtitleOverlay::Placement::Below));
    placement_->addItem(QStringLiteral("원문 위쪽에 표시"), int(SubtitleOverlay::Placement::Above));
    font_ = new QComboBox;
    for (const auto& [label, value] : {std::pair{"작게", 80}, {"보통", 100}, {"크게", 125}, {"아주 크게", 150}})
        font_->addItem(QString::fromUtf8(label), value);
    background_ = new QComboBox;
    for (const auto& [label, value] : {std::pair{"없음 · 글자 테두리만", 0}, {"반투명", 60}, {"진하게", 85}, {"불투명", 100}})
        background_->addItem(QString::fromUtf8(label), value);
    auto* preview = new QPushButton(QStringLiteral("미리보기"));
    auto* placementLabel = new QLabel(QStringLiteral("번역 위치")); placementLabel->setBuddy(placement_);
    auto* fontLabel = new QLabel(QStringLiteral("글자 크기")); fontLabel->setBuddy(font_);
    auto* backgroundLabel = new QLabel(QStringLiteral("배경")); backgroundLabel->setBuddy(background_);
    styleGrid->addWidget(placementLabel, 0, 0); styleGrid->addWidget(placement_, 0, 1);
    styleGrid->addWidget(fontLabel, 0, 2); styleGrid->addWidget(font_, 0, 3);
    styleGrid->addWidget(backgroundLabel, 1, 0); styleGrid->addWidget(background_, 1, 1);
    styleGrid->addWidget(preview, 1, 3);
    layout->addLayout(styleGrid);
    const auto saveStyle = [this] {
        style_.placement = SubtitleOverlay::Placement(placement_->currentData().toInt());
        style_.fontScale = font_->currentData().toInt();
        style_.backgroundOpacity = background_->currentData().toInt();
        Modes::saveStyle(Mode::Video, style_);
        applyStyle();
    };
    connect(placement_, &QComboBox::currentIndexChanged, this, saveStyle);
    connect(font_, &QComboBox::currentIndexChanged, this, saveStyle);
    connect(background_, &QComboBox::currentIndexChanged, this, saveStyle);
    connect(preview, &QPushButton::clicked, this, &VideoMode::previewCaption);

    start_ = new QPushButton;
    start_->setCheckable(true);
    start_->setMinimumHeight(40);
    QFont big = start_->font(); big.setPointSize(big.pointSize() + 2); big.setBold(true); start_->setFont(big);
    connect(start_, &QPushButton::clicked, this, [this](bool on) { setWatching(on); });
    layout->addWidget(start_);

    auto* latest = new QGridLayout;
    original_ = new QLabel; translated_ = new QLabel;
    for (QLabel* label : {original_, translated_}) {
        label->setWordWrap(true);
        label->setTextInteractionFlags(Qt::TextSelectableByMouse);
        label->setMinimumHeight(36);
    }
    QFont large = translated_->font(); large.setPointSize(large.pointSize() + 3); translated_->setFont(large);
    latest->addWidget(new QLabel(QStringLiteral("원문")), 0, 0, Qt::AlignTop); latest->addWidget(original_, 0, 1);
    latest->addWidget(new QLabel(QStringLiteral("번역")), 1, 0, Qt::AlignTop); latest->addWidget(translated_, 1, 1);
    latest->setColumnStretch(1, 1);
    layout->addLayout(latest);
    auto* history = new QPushButton(QStringLiteral("번역 기록 · 놓친 자막 다시 보기"));
    connect(history, &QPushButton::clicked, this, [this] { if (ctx_.showHistory) ctx_.showHistory(); });
    layout->addWidget(history);
    layout->addStretch(1);
}
void VideoMode::applyStyle() {
    SubtitleOverlay::Style effective = style_;
    // Covering the original only works when our caption is invisible to the capture.
    if (effective.placement == SubtitleOverlay::Placement::Cover && !ctx_.presenter->exclusionActive())
        effective.placement = SubtitleOverlay::Placement::Below;
    if (watching_) ctx_.presenter->setStyle(effective);
    updateUi();
}
void VideoMode::updateUi() {
    if (!start_) return;
    const QSignalBlocker a(placement_), b(font_), c(background_), d(start_);
    placement_->setCurrentIndex(std::max(0, placement_->findData(int(style_.placement))));
    font_->setCurrentIndex(std::max(0, font_->findData(style_.fontScale)));
    background_->setCurrentIndex(std::max(0, background_->findData(style_.backgroundOpacity)));
    const bool coverOk = ctx_.presenter->exclusionActive();
    placement_->setToolTip(coverOk ? QStringLiteral("덮기: 번역 창은 화면 캡처에 잡히지 않아 원문을 가려도 계속 읽을 수 있습니다.")
        : QStringLiteral("이 PC에서는 덮기를 쓸 수 없어 원문 아래에 표시합니다 (Windows 10 2004 이상 + 설정의 캡처 숨기기 필요)."));
    start_->setChecked(watching_);
    start_->setText(watching_ ? QStringLiteral("■ 영상 자막 번역 멈추기 · Ctrl+Alt+2") : QStringLiteral("▶ 영상 자막 번역 시작 · Ctrl+Alt+2"));
    regionLabel_->setText(region_.valid() ? QStringLiteral("자막 영역: ") + region_.describe()
        : region_.screen ? QStringLiteral("모니터 구성이 바뀌었습니다. 자막 영역을 다시 정하세요.")
                         : QStringLiteral("자막 영역이 없습니다. [영상 창에 맞추기]를 눌러 정하세요."));
}
void VideoMode::setRegion(const SavedRegion& region) {
    region_ = region;
    region_.save(RegionKey);
    worker_.reset();
    updateUi();
}
void VideoMode::setWatching(bool on) {
    if (on == watching_) { updateUi(); return; }
    if (on) {
        if (!region_.valid()) {
            updateUi();
            ctx_.status(QStringLiteral("시작하지 못했습니다 · 자막 영역이 없습니다 · [영상 창에 맞추기]나 [화면 아래쪽 20%]로 먼저 정하세요."));
            return;
        }
        if (!ctx_.ocrReady()) {
            updateUi();
            ctx_.status(QStringLiteral("시작하지 못했습니다 · OCR 엔진이 준비되지 않았습니다 · 설정 → OCR에서 모델을 확인하세요."));
            return;
        }
        watching_ = true;
        ++ticket_;
        worker_.setProfile(ChangeDetector::videoProfile());
        lastSource_.clear(); previousSource_.clear();
        ++line_;
        blackSince_ = -1;
        applyStyle();
        tick_.start();
        if (ctx_.minimizeMain) ctx_.minimizeMain();
        const QString blocked = ctx_.translationBlocked();
        ctx_.status(blocked.isEmpty()
            ? QStringLiteral("영상 자막 번역 중 · 자막이 바뀌면 번역합니다 · Ctrl+Alt+2로 멈춥니다.")
            : QStringLiteral("영상 자막 읽는 중 · 번역은 하지 않습니다 · %1").arg(blocked));
        qCInfo(stApp, "video watch on");
    } else {
        watching_ = false;
        ++ticket_;
        tick_.stop();
        if (ocrBusy_) { ctx_.ocr->cancel(); ocrBusy_ = false; }
        pendingImage_ = QImage();
        for (auto it = requests_.cbegin(); it != requests_.cend(); ++it) ctx_.queue->cancel(it.key());
        requests_.clear();
        endLine(Diagnostics::nowMs());
        lastSource_.clear();
        ctx_.presenter->hide();
        ctx_.status(QStringLiteral("영상 자막 번역을 멈췄습니다. Ctrl+Alt+2로 다시 시작합니다."));
    }
    updateUi();
    emit watchingChanged(watching_);
}
void VideoMode::tick() {
    if (!watching_) return;
    if (!region_.valid()) {
        setWatching(false);
        ctx_.status(QStringLiteral("영상 자막 번역을 멈췄습니다 · 모니터 구성이 바뀌었습니다 · 자막 영역을 다시 정하세요."));
        return;
    }
    // Another part of the app may have cancelled the shared OCR engine (e.g. a mouse translation).
    if (ocrBusy_ && !ctx_.ocr->isBusy() && Diagnostics::nowMs() - ocrStartedAt_ > 500) ocrBusy_ = false;
    const QRect global = region_.global();
    if (ctx_.ownWindowCovers && ctx_.ownWindowCovers(global)) return; // never read our own window
    if (!ctx_.presenter->exclusionActive() && ctx_.presenter->isVisible() &&
        ctx_.presenter->overlay()->geometry().intersects(global)) return;
    // Protected (DRM) video is captured as pure black: tell the user once instead of reading nothing.
    if (!protectedHintShown_) {
        const qint64 now = Diagnostics::nowMs();
        if (worker_.lastBlackFraction() >= 0.995) {
            if (blackSince_ < 0) blackSince_ = now;
            else if (now - blackSince_ >= ProtectedMs) {
                protectedHintShown_ = true;
                ctx_.status(QStringLiteral("번역할 자막이 보이지 않습니다 · 이 영상은 화면 캡처가 막혀 검게 잡힙니다(보호된 영상) · "
                                           "넷플릭스 등 DRM 영상은 지원하지 않습니다. 브라우저 영상이면 하드웨어 가속을 끄면 보일 수 있습니다."));
                ctx_.presenter->toast(QStringLiteral("보호된 영상은 화면 자막을 읽을 수 없습니다"), region_.screen->availableGeometry(), 4000);
            }
        } else blackSince_ = -1;
    }
    worker_.requestCheck(Capture::physicalRect(region_.screen, region_.rect), region_.dpr, Diagnostics::nowMs(), ticket_, region_.probe);
}
void VideoMode::onFrame(const QImage& image, qint64 capturedAt, quint64 ticket) {
    if (ticket != ticket_ || !watching_) return;
    if (ocrBusy_ || ctx_.ocr->isBusy()) { pendingImage_ = image; pendingCapturedAt_ = capturedAt; return; } // newest wins
    startOcr(image, capturedAt);
}
void VideoMode::onBlank(qint64 capturedAt, quint64 ticket) {
    if (ticket != ticket_ || !watching_) return;
    pendingImage_ = QImage(); // an older picture waiting for OCR is gone from the screen
    lineCleared(capturedAt);
}
void VideoMode::startOcr(const QImage& image, qint64 capturedAt) {
    const OcrSetup setup = ctx_.ocrSetup();
    ocrBusy_ = true;
    ocrStartedAt_ = Diagnostics::nowMs();
    ocrCapturedAt_ = capturedAt;
    ctx_.ocr->recognize(image, setup.executable, setup.tessdata, setup.psm, setup.languages);
}
void VideoMode::onRecognized(const QString& text) {
    if (!ocrBusy_) return; // another mode's request
    ocrBusy_ = false;
    const qint64 now = Diagnostics::nowMs();
    Diagnostics::stage(line_ + 1, "video", "ocr", now - ocrStartedAt_);
    handleText(text, ocrCapturedAt_, now);
    if (!pendingImage_.isNull() && watching_) {
        const QImage next = pendingImage_;
        pendingImage_ = QImage();
        startOcr(next, pendingCapturedAt_);
    }
}
void VideoMode::onOcrFailed(const QString& error) {
    if (!ocrBusy_) return;
    ocrBusy_ = false;
    worker_.retryAfterFailure(Diagnostics::nowMs());
    ctx_.status(QStringLiteral("영상 자막 번역 중 · 이번 자막을 읽지 못했습니다 · %1").arg(error));
    if (!pendingImage_.isNull() && watching_) {
        const QImage next = pendingImage_;
        pendingImage_ = QImage();
        startOcr(next, pendingCapturedAt_);
    }
}
void VideoMode::handleText(const QString& text, qint64 capturedAt, qint64 ocrDoneAt) {
    const QString source = QString::fromStdString(subtitle::normalizeText(text.toStdString()));
    if (source.isEmpty()) { lineCleared(capturedAt); return; }
    if (source == lastSource_) return; // the same line read again (forced re-read)
    endLine(capturedAt);
    lastSource_ = source;
    const QString context = previousSource_;
    previousSource_ = source;
    ++line_;
    historyId_ = ctx_.history->add(QStringLiteral("video"), source, QString(), capturedAt);
    original_->setText(source);
    LineTiming timing;
    timing.capturedAt = capturedAt; timing.ocrDoneAt = ocrDoneAt; timing.source = source;
    timings_ << timing;
    const int timingIndex = int(timings_.size()) - 1;
    const QString target = ctx_.targetLanguage();
    const QString languages = ctx_.sourceLanguages();
    auto segments = subtitle::splitForTranslation(source.toStdString(),
        subtitle::preservableScripts(target.section('-', 0, 0).toLower().toStdString(),
                                     Languages::deeplSource(languages).toLower().toStdString()));
    subtitle::keepUnworthySegments(segments);
    QStringList pieces;
    unsigned scripts = 0;
    for (const auto& segment : segments)
        if (segment.translate) { pieces << QString::fromStdString(segment.utf8); scripts |= subtitle::scriptsIn(segment.utf8); }
    if (pieces.isEmpty()) {
        // Already in the target language (or only symbols): nothing to cover; show the line as it is.
        translated_->setText(source);
        ctx_.history->setTranslation(historyId_, source);
        ctx_.presenter->hide();
        return;
    }
    const QString blocked = ctx_.translationBlocked();
    if (!blocked.isEmpty()) { translated_->setText(QString()); return; }
    TranslateOptions options;
    options.target = target;
    options.source = Languages::sourceForScripts(scripts, languages);
    options.context = context.left(300);
    Request request;
    request.line = line_;
    request.segments = std::move(segments);
    request.timing = timingIndex;
    request.history = historyId_;
    const quint64 id = ctx_.queue->translateLive(pieces, options, QStringLiteral("video"));
    requests_.insert(id, std::move(request));
}
void VideoMode::lineCleared(qint64 at) {
    if (lastSource_.isEmpty()) return;
    endLine(at);
    lastSource_.clear();
    ++line_; // a translation arriving now belongs to a line that is gone
    ctx_.presenter->hideAfter(LingerMs);
}
void VideoMode::endLine(qint64 at) {
    if (historyId_) ctx_.history->setEnd(historyId_, at);
    historyId_ = 0;
}
void VideoMode::onTranslated(quint64 id, const QStringList& translations) {
    const auto found = requests_.find(id);
    if (found == requests_.end()) return;
    const Request request = found.value();
    requests_.erase(found);
    std::vector<std::string> parts;
    for (const QString& piece : translations) parts.push_back(piece.toStdString());
    const QString text = QString::fromStdString(subtitle::joinTranslated(request.segments, parts));
    if (text.isEmpty()) return;
    const qint64 now = Diagnostics::nowMs();
    ctx_.history->setTranslation(request.history, text);
    if (request.timing >= 0 && request.timing < timings_.size()) {
        timings_[request.timing].translatedAt = now;
        timings_[request.timing].translation = text;
    }
    // Shown only while its line is still on screen; otherwise the history keeps it.
    if (request.line != line_ || !watching_) return;
    translated_->setText(text);
    show(text);
    if (request.timing >= 0 && request.timing < timings_.size()) {
        timings_[request.timing].shownAt = Diagnostics::nowMs();
        Diagnostics::stage(request.line, "video", "line-to-caption", timings_[request.timing].shownAt - timings_[request.timing].capturedAt);
    }
}
void VideoMode::onTranslateFailed(quint64 id, const QString& error, bool userAction) {
    if (!requests_.remove(id)) return;
    if (userAction) {
        setWatching(false);
        ctx_.status(QStringLiteral("영상 자막 번역을 멈췄습니다 · %1").arg(error));
    } else {
        ctx_.status(QStringLiteral("영상 자막 번역 중 · 이번 자막은 번역하지 못했습니다 · %1").arg(error));
    }
}
void VideoMode::show(const QString& text) {
    if (!region_.valid()) return;
    applyStyle();
    ctx_.presenter->show(text, region_.global(), region_.screen->availableGeometry());
}
void VideoMode::previewCaption() {
    if (!region_.valid()) {
        ctx_.status(QStringLiteral("미리볼 자막 영역이 없습니다 · [영상 창에 맞추기]로 먼저 정하세요."));
        return;
    }
    const SubtitleOverlay::Style previous = ctx_.presenter->style();
    SubtitleOverlay::Style effective = style_;
    if (effective.placement == SubtitleOverlay::Placement::Cover && !ctx_.presenter->exclusionActive())
        effective.placement = SubtitleOverlay::Placement::Below;
    ctx_.presenter->setStyle(effective);
    ctx_.presenter->show(QStringLiteral("번역 자막 미리보기 · 실제 번역이 아닙니다"), region_.global(), region_.screen->availableGeometry());
    ctx_.presenter->hideAfter(3000);
    if (!watching_) QTimer::singleShot(3100, this, [this, previous] { if (!watching_) ctx_.presenter->setStyle(previous); });
}
void VideoMode::fitToWindow() {
    if (selecting_) return;
    selecting_ = true;
    ctx_.status(QStringLiteral("3초 안에 영상 위로 마우스를 옮기세요. 그 창의 자막 자리를 제안합니다."));
    QTimer::singleShot(3000, this, [this] {
        selecting_ = false;
        const auto info = GameWindow::at(QCursor::pos(), GameWindow::currentProcess());
        const auto area = info.isValid() ? GameWindow::toScreen(info.client) : GameWindow::Area{};
        if (!area.screen || area.rect.width() < 160 || area.rect.height() < 90) {
            ctx_.status(QStringLiteral("영상 창을 찾지 못했습니다 · 마우스가 영상 위에 있지 않았습니다 · 다시 누르고 3초 안에 영상 위로 옮기세요."));
            return;
        }
        const QRect r = area.rect;
        const QRect band(r.left() + r.width() / 20, r.top() + r.height() * 78 / 100, r.width() * 9 / 10,
                         std::max(40, r.height() * 19 / 100));
        selectOn(area.screen, band, info.client);
    });
}
void VideoMode::bottomOfScreen() {
    QScreen* screen = QGuiApplication::screenAt(QCursor::pos());
    if (!screen) screen = QGuiApplication::primaryScreen();
    const QSize size = screen->size();
    const QRect band(size.width() / 20, size.height() * 78 / 100, size.width() * 9 / 10, size.height() * 19 / 100);
    selectOn(screen, band, Capture::physicalRect(screen, QRect(QPoint(), size)));
}
void VideoMode::chooseRegion() {
    QScreen* screen = region_.screen ? region_.screen.data() : QGuiApplication::screenAt(QCursor::pos());
    if (!screen) screen = QGuiApplication::primaryScreen();
    selectOn(screen, {}, {});
}
void VideoMode::selectOn(QScreen* screen, const QRect& suggestion, const QRect& probe) {
    if (selecting_ || !screen) return;
    const bool wasWatching = watching_;
    if (wasWatching) setWatching(false);
    selecting_ = true;
    if (ctx_.hideOwnWindows) ctx_.hideOwnWindows();
    QPointer<QScreen> target = screen;
    QTimer::singleShot(180, this, [this, target, suggestion, probe, wasWatching] {
        selecting_ = false;
        if (!target) { if (ctx_.restoreOwnWindows) ctx_.restoreOwnWindows(); return; }
        const QPixmap snapshot = target->grabWindow(0);
        RegionSelector selector(snapshot, target->geometry());
        selector.setHint(suggestion.isEmpty() ? QStringLiteral("자막이 나오는 띠를 드래그하세요 · Esc: 취소")
                                              : QStringLiteral("Enter: 이 영역 사용 · 드래그: 다시 그리기 · Esc: 취소"));
        if (!suggestion.isEmpty()) selector.setSuggestion(suggestion);
        const bool accepted = selector.exec() == QDialog::Accepted && target;
        if (ctx_.restoreOwnWindows) ctx_.restoreOwnWindows();
        if (!accepted) { ctx_.status(QStringLiteral("자막 영역 선택을 취소했습니다.")); return; }
        SavedRegion region;
        region.screen = target;
        region.screenGeometry = target->geometry();
        region.dpr = target->devicePixelRatio();
        region.rect = selector.selectedRegion();
        region.probe = probe;
        setRegion(region);
        ctx_.status(QStringLiteral("자막 영역을 정했습니다. [영상 자막 번역 시작] 또는 Ctrl+Alt+2를 누르세요."));
        if (wasWatching) setWatching(true);
    });
}
