#include "main_window.h"
#include "capture_service.h"
#include "credential_store.h"
#include "diagnostics.h"
#include "file_mode.h"
#include "first_run_wizard.h"
#include "history_window.h"
#include "languages.h"
#include "mode_context.h"
#include "paddle_ocr.h"
#include "region_selector.h"
#include "subtitle/text.h"
#include "subtitle_overlay.h"
#include "subtitle_presenter.h"
#include "text_mode.h"
#include "translation_queue.h"
#include "widget_helpers.h"
#include "video_mode.h"
#include <QActionGroup>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QCursor>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QSaveFile>
#include <QScreen>
#include <QSettings>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QSystemTrayIcon>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>
#include <QUrl>
#include <QImageReader>
#include <QFutureWatcher>
#include <QtConcurrent>
#include <QVBoxLayout>
#include <algorithm>
#include <windows.h>

namespace {
QString describeRegion(const QScreen* screen, const QRect& region) {
    return QStringLiteral("%1 · 위치 (%2, %3) · %4 × %5 논리 픽셀")
        .arg(screen->name()).arg(region.x()).arg(region.y()).arg(region.width()).arg(region.height());
}
QIcon trayIcon(bool active) {
    QPixmap pixmap(64, 64);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(active ? QColor(22, 163, 74) : QColor(100, 116, 139));
    p.drawRoundedRect(QRectF(2, 2, 60, 60), 14, 14);
    QFont font(QStringLiteral("Malgun Gothic"));
    font.setPixelSize(38); font.setBold(true);
    p.setFont(font);
    p.setPen(Qt::white);
    p.drawText(pixmap.rect(), Qt::AlignCenter, QStringLiteral("자"));
    return QIcon(pixmap);
}
QString modeIntro(Mode mode) {
    switch (mode) {
    case Mode::Game: return QStringLiteral("게임 모드 · 자막 영역을 고르고 자동 번역(Ctrl+Alt+P)을 켜세요.");
    case Mode::Video: return QStringLiteral("영상 모드 · 자막 위치를 정하고 Ctrl+Alt+2로 시작하세요.");
    case Mode::Text: return QStringLiteral("글 모드 · 글을 복사한 뒤 Ctrl+Alt+3을 누르세요.");
    case Mode::File: return QStringLiteral("자막 파일 모드 · .srt·.vtt·.ass 파일을 끌어 놓으세요.");
    }
    return {};
}
}

QWidget* MainWindow::buildGamePage() {
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);
    layout->addWidget(Ui::hint(QStringLiteral("game"),
        QStringLiteral("게임 대사창을 읽어 번역합니다.\n"
                       "① [자막 영역 선택]으로 대사 글자 부분을 고르고 ② 자동 번역(Ctrl+Alt+P)을 켜세요.\n"
                       "한 번만 번역은 Ctrl+Alt+T, 마우스 주변 글자는 Ctrl+Alt+M입니다.")));
    auto* row = new QHBoxLayout;
    screens_ = new QComboBox;
    auto* monitorLabel = new QLabel(QStringLiteral("모니터"));
    monitorLabel->setBuddy(screens_);
    row->addWidget(monitorLabel);
    row->addWidget(screens_, 1);
    layout->addLayout(row);
    auto* actions = new QHBoxLayout;
    select_ = new QPushButton(QStringLiteral("자막 영역 선택"));
    capture_ = new QPushButton(QStringLiteral("캡처·번역  Ctrl+Alt+T"));
    cancel_ = new QPushButton(QStringLiteral("취소"));
    cancel_->setEnabled(false);
    auto_ = new QPushButton(QStringLiteral("자동 번역  Ctrl+Alt+P"));
    auto_->setCheckable(true);
    actions->addWidget(select_);
    actions->addWidget(auto_, 1);
    actions->addWidget(capture_, 1);
    actions->addWidget(cancel_);
    layout->addLayout(actions);
    auto* cursorRow = new QHBoxLayout;
    cursorCapture_ = new QPushButton(QStringLiteral("마우스 번역 · 3초 준비"));
    cursorCapture_->setToolTip(QStringLiteral("버튼을 누른 뒤 3초 안에 글자 위로 마우스를 옮기세요. Ctrl+Alt+M은 즉시 실행합니다."));
    cursorSizeBox_ = new QComboBox;
    cursorSizeBox_->addItem(QStringLiteral("좁게 · 360×100"), QSize(360, 100));
    cursorSizeBox_->addItem(QStringLiteral("보통 · 640×160"), QSize(640, 160));
    cursorSizeBox_->addItem(QStringLiteral("넓게 · 960×240"), QSize(960, 240));
    cursorWidth_ = new QSpinBox;
    cursorHeight_ = new QSpinBox;
    cursorWidth_->setRange(80, 3840);
    cursorHeight_->setRange(40, 2160);
    cursorWidth_->setAccessibleName(QStringLiteral("마우스 번역 가로 범위"));
    cursorHeight_->setAccessibleName(QStringLiteral("마우스 번역 세로 범위"));
    for (auto* field : {cursorWidth_, cursorHeight_}) {
        field->setSuffix(QStringLiteral(" px"));
        field->setSingleStep(10);
        field->setKeyboardTracking(false); // Save on Enter/focus loss, not each unfinished digit.
        field->setToolTip(QStringLiteral("마우스 중심의 사각형 크기. 화면 배율을 적용하기 전 픽셀 기준이며, 화면 가장자리에서는 범위를 자동 조정합니다."));
    }
    QSize savedCursorSize = QSettings().value("cursor/size", QSize(640,160)).toSize();
    if (savedCursorSize.width() < cursorWidth_->minimum() || savedCursorSize.width() > cursorWidth_->maximum() ||
        savedCursorSize.height() < cursorHeight_->minimum() || savedCursorSize.height() > cursorHeight_->maximum())
        savedCursorSize = QSize(640,160);
    cursorSizeBox_->addItem(QStringLiteral("직접 지정"), savedCursorSize);
    cursorSizeBox_->setCurrentIndex(cursorSizeBox_->findData(savedCursorSize));
    cursorWidth_->setValue(savedCursorSize.width());
    cursorHeight_->setValue(savedCursorSize.height());
    cursorSizeBox_->setToolTip(QStringLiteral("마우스 주변 캡처 범위 (화면 배율과 무관한 논리 픽셀)"));
    cursorSizeBox_->setAccessibleName(QStringLiteral("마우스 번역 범위"));
    cursorRow->addWidget(cursorCapture_);
    cursorShortcutLabel_ = new QLabel;
    cursorRow->addWidget(cursorShortcutLabel_);
    cursorRow->addWidget(cursorSizeBox_, 1);
    layout->addLayout(cursorRow);
    auto* cursorDimensions = new QHBoxLayout;
    auto* widthLabel = new QLabel(QStringLiteral("가로")); widthLabel->setBuddy(cursorWidth_);
    auto* heightLabel = new QLabel(QStringLiteral("세로")); heightLabel->setBuddy(cursorHeight_);
    cursorDimensions->addWidget(widthLabel);
    cursorDimensions->addWidget(cursorWidth_);
    cursorDimensions->addWidget(heightLabel);
    cursorDimensions->addWidget(cursorHeight_);
    auto* sizeHint = new QLabel(QStringLiteral("직접 입력 가능 · 자동 저장 · 마우스 중심"));
    sizeHint->setWordWrap(true);
    cursorDimensions->addWidget(sizeHint,1);
    layout->addLayout(cursorDimensions);
    hoverMode_ = new QCheckBox(QStringLiteral("마우스 올려두기 번역"));
    hoverMode_->setToolTip(QStringLiteral("글자 위에서 1초 멈추면 한 번 번역합니다. 이동하면 다시 준비합니다. 앱을 다시 열면 꺼진 상태로 시작합니다."));
    auto* hoverRow = new QHBoxLayout;
    hoverRow->addWidget(hoverMode_);
    auto* hoverHint = new QLabel(QStringLiteral("1초 정지 → 1회 번역 · 전송·비용 설정 적용"));
    hoverHint->setWordWrap(true);
    hoverRow->addWidget(hoverHint, 1);
    layout->addLayout(hoverRow);
    auto* imageActions = new QHBoxLayout;
    import_ = new QPushButton(QStringLiteral("이미지 파일 선택"));
    retry_ = new QPushButton(QStringLiteral("최근 이미지 재시도"));
    retry_->setEnabled(false);
    imageActions->addWidget(import_); imageActions->addWidget(retry_);
    layout->addLayout(imageActions);
    regionLabel_ = new QLabel(QStringLiteral("선택한 영역이 없습니다."));
    regionLabel_->setWordWrap(true);
    layout->addWidget(regionLabel_);
    auto* captionRow = new QHBoxLayout;
    inlineCaptions_ = new QCheckBox(QStringLiteral("원문 아래에 번역 글자만 표시"));
    inlineCaptions_->setChecked(QSettings().value("display/inlineCaptions", true).toBool());
    captionPreview_ = new QPushButton(QStringLiteral("위치 미리보기"));
    captionRow->addWidget(inlineCaptions_, 1); captionRow->addWidget(captionPreview_);
    layout->addLayout(captionRow);
    layout->addWidget(new QLabel(QStringLiteral("최근 캡처 · 이 이미지는 외부로 전송하지 않습니다")));
    preview_ = new QLabel(QStringLiteral("영역을 선택하고 캡처하면 이미지가 표시됩니다."));
    preview_->setAlignment(Qt::AlignCenter);
    preview_->setMinimumHeight(90);
    preview_->setMaximumHeight(140);
    preview_->setStyleSheet("background:#202733; color:#d7dee9; border-radius:6px;");
    layout->addWidget(preview_);
    auto* originalLabel = new QLabel(QStringLiteral("OCR 원문"));
    layout->addWidget(originalLabel);
    original_ = new QPlainTextEdit;
    original_->setReadOnly(true);
    original_->setPlaceholderText(QStringLiteral("실제 OCR 결과가 여기에 표시됩니다."));
    original_->setAccessibleName(QStringLiteral("OCR 원문"));
    originalLabel->setBuddy(original_);
    layout->addWidget(original_, 1);
    auto* resultButton = new QPushButton(QStringLiteral("번역 결과 창 열기"));
    layout->addWidget(resultButton);
    connect(resultButton, &QPushButton::clicked, this, &MainWindow::showResult);
    note_ = new QLabel(QStringLiteral("자동 번역은 0.4초마다 자막 영역만 확인합니다. 창 모드·테두리 없는 창 모드를 권장합니다.\n"
        "마우스 번역: 글자 위에서 Ctrl+Alt+M. 주변 사각형을 한 번 읽으며, 자동 번역은 멈춥니다 (Ctrl+Alt+P로 재개)."));
    note_->setWordWrap(true);
    layout->addWidget(note_);
    return page;
}

MainWindow::MainWindow() {
    setWindowTitle(QStringLiteral("자막 번역"));
    resize(780, 900);
    setMinimumSize(660, 760);
    setAttribute(Qt::WA_ShowWithoutActivating);
    auto* central = new QWidget(this);
    setCentralWidget(central);
    auto* layout = new QVBoxLayout(central);
    layout->setContentsMargins(20, 14, 20, 14);
    layout->setSpacing(10);
    auto* header = new QHBoxLayout;
    auto* title = new QLabel(QStringLiteral("자막 번역"));
    title->setStyleSheet("font-size:22px; font-weight:600;");
    header->addWidget(title, 1);
    auto* historyButton = new QPushButton(QStringLiteral("번역 기록"));
    settings_ = new QPushButton(QStringLiteral("설정"));
    header->addWidget(historyButton);
    header->addWidget(settings_);
    layout->addLayout(header);
    modeTabs_ = new QTabBar;
    for (int i = 0; i < Modes::Count; ++i) modeTabs_->addTab(Modes::label(Mode(i)));
    modeTabs_->setExpanding(true);
    modeTabs_->setDocumentMode(true);
    modeTabs_->setAccessibleName(QStringLiteral("번역 모드"));
    layout->addWidget(modeTabs_);
    auto* languageRow = new QHBoxLayout;
    ocrLanguageBox_ = new QComboBox;
    for (const auto& option : Languages::Ocr) ocrLanguageBox_->addItem(QString::fromUtf8(option.label), QString::fromLatin1(option.code));
    targetLanguageBox_ = new QComboBox;
    for (const auto& option : Languages::Target) targetLanguageBox_->addItem(QString::fromUtf8(option.label), QString::fromLatin1(option.code));
    ocrLanguageBox_->setAccessibleName(QStringLiteral("원문 언어"));
    targetLanguageBox_->setAccessibleName(QStringLiteral("번역 언어"));
    loadLanguages(Mode::Game);
    auto* sourceLabel = new QLabel(QStringLiteral("원문 언어")); sourceLabel->setBuddy(ocrLanguageBox_);
    auto* targetLabel = new QLabel(QStringLiteral("→ 번역 언어")); targetLabel->setBuddy(targetLanguageBox_);
    languageRow->addWidget(sourceLabel);
    languageRow->addWidget(ocrLanguageBox_, 3);
    languageRow->addWidget(targetLabel);
    languageRow->addWidget(targetLanguageBox_, 2);
    layout->addLayout(languageRow);
    pages_ = new QStackedWidget;
    gamePage_ = buildGamePage();
    pages_->addWidget(gamePage_);
    for (int i = 1; i < Modes::Count; ++i) {
        auto* slot = new QWidget; // filled on first visit, so start-up stays fast
        auto* slotLayout = new QVBoxLayout(slot);
        slotLayout->setContentsMargins(0, 0, 0, 0);
        pages_->addWidget(slot);
    }
    layout->addWidget(pages_, 1);
    status_ = new QLabel(QStringLiteral("준비 중"));
    Ui::panel(status_);
    status_->setAccessibleName(QStringLiteral("상태"));
    layout->addWidget(status_);
    usageLabel_ = new QLabel;
    usageLabel_->setStyleSheet(QStringLiteral("color: palette(placeholder-text);"));
    layout->addWidget(usageLabel_);

    hoverTimer_ = new QTimer(this);
    hoverTimer_->setInterval(150);
    connect(hoverMode_, &QCheckBox::toggled, this, &MainWindow::setHoverTranslate);
    connect(hoverTimer_, &QTimer::timeout, this, &MainWindow::hoverTick);
    connect(cursorCapture_, &QPushButton::clicked, this, &MainWindow::scheduleCursorCapture);
    connect(cursorSizeBox_, &QComboBox::currentIndexChanged, this, [this] {
        const QSize size = cursorSizeBox_->currentData().toSize();
        const QSignalBlocker widthBlock(cursorWidth_), heightBlock(cursorHeight_);
        cursorWidth_->setValue(size.width()); cursorHeight_->setValue(size.height());
        QSettings().setValue("cursor/size", size);
        hoverSince_ = -1; hoverConsumed_ = false;
    });
    const auto saveCustomSize = [this] {
        const QSize size(cursorWidth_->value(), cursorHeight_->value());
        const QSignalBlocker block(cursorSizeBox_);
        cursorSizeBox_->setItemData(3, size);
        cursorSizeBox_->setCurrentIndex(cursorSizeBox_->findData(size));
        QSettings().setValue("cursor/size", size);
        hoverSince_ = -1; hoverConsumed_ = false;
    };
    connect(cursorWidth_, &QSpinBox::valueChanged, this, saveCustomSize);
    connect(cursorHeight_, &QSpinBox::valueChanged, this, saveCustomSize);
    autoTimer_ = new QTimer(this);
    autoTimer_->setInterval(400);
    autoClock_.start();
    connect(import_, &QPushButton::clicked, this, &MainWindow::importImage);
    connect(retry_, &QPushButton::clicked, this, [this] {
        if (!busy_ && !lastImage_.isNull()) {
            setHoverTranslate(false);
            captionArea_ = lastCaptionArea_;
            resetOutput(); processImage(lastImage_, lastSource_);
        }
    });
    presenter_ = new SubtitlePresenter(this);
    subtitleOverlay_ = presenter_->overlay();
    connect(captionPreview_, &QPushButton::clicked, this, &MainWindow::previewSubtitle);
    connect(inlineCaptions_, &QCheckBox::toggled, this, [this](bool enabled) {
        QSettings().setValue("display/inlineCaptions", enabled);
        if (!enabled) { subtitleOverlay_->hide(); restoreOverlay_ = false; }
        else if (!busy_ && lastSource_ == ST_SCREEN_CAPTURE && !result_->toPlainText().isEmpty())
            showSubtitle(result_->toPlainText());
    });
    resultWindow_ = new QWidget(nullptr, Qt::Window | Qt::WindowStaysOnTopHint);
    resultWindow_->setAttribute(Qt::WA_ShowWithoutActivating);
    resultWindow_->setWindowTitle(QStringLiteral("자막 번역 결과"));
    resultWindow_->resize(640, 300);
    auto* resultLayout = new QVBoxLayout(resultWindow_);
    resultStatus_ = new QLabel(QStringLiteral("아직 번역한 자막이 없습니다."));
    resultStatus_->setWordWrap(true);
    resultLayout->addWidget(resultStatus_);
    result_ = new QPlainTextEdit;
    result_->setReadOnly(true);
    result_->setFont(QFont("Malgun Gothic", 16));
    result_->setPlaceholderText(QStringLiteral("번역이 완료되면 표시됩니다."));
    resultLayout->addWidget(result_);
    auto* copy = new QPushButton(QStringLiteral("번역 전체 선택"));
    resultLayout->addWidget(copy);
    connect(copy, &QPushButton::clicked, result_, &QPlainTextEdit::selectAll);
    connect(settings_, &QPushButton::clicked, this, &MainWindow::settings);
    connect(historyButton, &QPushButton::clicked, this, &MainWindow::showHistory);
    connect(select_, &QPushButton::clicked, this, &MainWindow::chooseRegion);
    connect(capture_, &QPushButton::clicked, this, &MainWindow::startCapture);
    connect(auto_, &QPushButton::toggled, this, &MainWindow::setAutoTranslate);
    connect(modeTabs_, &QTabBar::currentChanged, this, [this](int index) { setMode(Mode(index)); });
    // A new language changes what the current line means: forget the cache and read it again.
    connect(ocrLanguageBox_, &QComboBox::currentIndexChanged, this, [this] {
        QSettings().setValue(Modes::languageKey(mode_, false), ocrLanguages());
        st_reset_cache(session_); captureWorker_.reset();
        if (hoverRequest_ && busy_) cancel();
        hoverSince_ = -1; hoverConsumed_ = false; hoverRequest_ = false;
    });
    connect(targetLanguageBox_, &QComboBox::currentIndexChanged, this, [this] {
        QSettings().setValue(Modes::languageKey(mode_, true), targetLanguage());
        st_reset_cache(session_); captureWorker_.reset();
        if (hoverRequest_ && busy_) cancel();
        hoverSince_ = -1; hoverConsumed_ = false; hoverRequest_ = false;
    });
    connect(autoTimer_, &QTimer::timeout, this, &MainWindow::autoTick);
    // Capturing and comparing run on the capture thread; only frames worth reading come back.
    connect(&captureWorker_, &CaptureWorker::frameReady, this, [this](const QImage& image, qint64, quint64 ticket) {
        if (ticket != autoTicket_ || !autoTranslating()) return;
        if (busy_ || modal_) { captureWorker_.reset(); return; } // read it again once we are free
        // Keep the previous caption on screen until the new line has a result.
        original_->clear(); result_->clear();
        captionArea_ = selectedArea();
        processImage(image, ST_SCREEN_CAPTURE);
    });
    connect(&captureWorker_, &CaptureWorker::captureFailed, this, [this](const QString& error, quint64 ticket) {
        if (ticket != autoTicket_ || !autoTranslating()) return;
        setAutoTranslate(false);
        status_->setText(error);
    });
    connect(cancel_, &QPushButton::clicked, this, [this] {
        if (hoverMode_->isChecked()) setHoverTranslate(false);
        else cancel();
    });
    connect(qApp, &QGuiApplication::screenAdded, this, [this] { refreshScreens(); });
    connect(qApp, &QGuiApplication::screenRemoved, this, [this] {
        setHoverTranslate(false);
        cancel(); subtitleOverlay_->hide(); selectedScreen_ = nullptr; region_ = {}; refreshScreens();
        regionLabel_->setText(QStringLiteral("모니터 구성이 바뀌었습니다. 영역을 다시 선택하세요."));
    });
    connect(&ocr_, &OcrService::recognized, this, [this](const QString& text) {
        if (!busy_) return;
        const auto utf8 = text.toUtf8();
        const int decision = st_accept_ocr(session_, request_, utf8.constData());
        if (decision == ST_STALE) return;
        original_->setPlainText(QString::fromUtf8(st_source_text(session_)));
        if (decision == ST_EMPTY || decision == ST_TOO_LONG) {
            st_fail(session_, request_);
            if (decision == ST_EMPTY && autoTranslating() && activeSource_ == ST_SCREEN_CAPTURE) {
                // An empty dialogue box is normal while playing: hide the old caption quietly.
                subtitleOverlay_->hide(); setBusy(false);
                const bool retry = captureWorker_.retryAfterFailure(autoClock_.elapsed());
                status_->setText(ocr_.readingRejected()
                    ? QStringLiteral("자동 번역 중 · 인식이 불확실해 번역하지 않았습니다. 영역을 좁히거나 원문 언어를 지정하세요.")
                    : retry ? QStringLiteral("자동 번역 중 · 글자가 없어 3초 뒤 다시 확인합니다.")
                    : QStringLiteral("자동 번역 중 · 글자를 찾지 못했습니다. 다음 대사를 기다리며, 캡처 영역도 확인해 주세요."));
                return;
            }
            resultStatus_->setText(QStringLiteral("OCR 결과를 번역할 수 없습니다."));
            finish(decision == ST_EMPTY ? (ocr_.readingRejected()
                    ? QStringLiteral("인식이 불확실해 번역하지 않았습니다. 영역을 좁히거나 원문 언어를 지정한 뒤 다시 시도하세요.")
                    : QStringLiteral("인식한 글자가 없습니다."))
                : QStringLiteral("인식 텍스트가 너무 깁니다. 자막 부분만 선택하세요."));
            return;
        }
        if (decision == ST_CACHED) {
            result_->setPlainText(QString::fromUtf8(st_translation(session_)));
            resultStatus_->setText(QStringLiteral("동일 원문 · 최근 번역 재사용"));
            presentTranslation();
            finish(QStringLiteral("동일한 원문이므로 번역 API를 다시 호출하지 않았습니다."));
            return;
        }
        // Only foreign-script runs are translated; text already in the target language stays.
        const QString target = Languages::targetLabel(targetLanguage());
        segments_ = subtitle::splitForTranslation(st_source_text(session_),
            subtitle::preservableScripts(targetLanguage().section('-', 0, 0).toLower().toStdString(),
                Languages::deeplSource(ocrLanguages()).toLower().toStdString()));
        // Symbols misread as letters ("★" -> "O") and stat tokens ("Lv.10") stay as they are (QA B-04).
        const bool keptSymbols = subtitle::keepUnworthySegments(segments_) > 0;
        const bool symbolsOnly = keptSymbols && !(subtitle::scriptsIn(st_source_text(session_)) &
            subtitle::nativeScripts(targetLanguage().section('-', 0, 0).toLower().toStdString()));
        QStringList pieces;
        unsigned scripts = 0;
        for (const auto& segment : segments_)
            if (segment.translate) { pieces << QString::fromStdString(segment.utf8); scripts |= subtitle::scriptsIn(segment.utf8); }
        if (pieces.isEmpty()) {
            st_finish_ocr(session_, request_);
            const QString reason = symbolsOnly ? QStringLiteral("기호·숫자나 짧은 영문만 있어")
                                               : QStringLiteral("원문이 이미 %1라").arg(target);
            resultStatus_->setText(symbolsOnly ? QStringLiteral("번역할 글자가 없습니다 · 기호·숫자나 짧은 영문만 인식했습니다.")
                                               : QStringLiteral("원문이 이미 %1입니다 · 번역하지 않았습니다.").arg(target));
            if (autoTranslating() && activeSource_ == ST_SCREEN_CAPTURE) {
                subtitleOverlay_->hide(); setBusy(false);
                status_->setText(QStringLiteral("자동 번역 중 · %1 번역하지 않았습니다.").arg(reason));
                return;
            }
            finish(QStringLiteral("%1 번역 요청을 보내지 않았습니다.").arg(reason));
            return;
        }
        if (apiKey_.isEmpty() || !consent_) {
            st_finish_ocr(session_, request_);
            history_.add(QStringLiteral("game"), QString::fromUtf8(st_source_text(session_)), QString());
            resultStatus_->setText(QStringLiteral("OCR 완료 · 번역 미실행. 설정에서 API 키와 전송 동의를 확인하세요."));
            finish(QStringLiteral("OCR 완료. API 키·전송 동의가 없어 번역 요청은 보내지 않았습니다."));
            return;
        }
        // The session cap is checked before anything is sent.
        if (!usage_.allows(UsageMeter::characters(pieces))) {
            st_fail(session_, request_);
            const QString message = usage_.capReachedMessage();
            setBusy(false);
            setAutoTranslate(false);
            setHoverTranslate(false);
            resultStatus_->setText(message);
            finish(message);
            return;
        }
        sentPieces_ = pieces;
        status_->setText(QStringLiteral("DeepL에서 %1(으)로 번역 중…").arg(target));
        resultStatus_->setText(QStringLiteral("번역 중…"));
        translation_.translate(pieces, apiKey_, freeApi_, targetLanguage(), Languages::sourceForScripts(scripts, ocrLanguages()));
    });
    connect(&ocr_, &OcrService::failed, this, [this](const QString& error) {
        if (!busy_) return;
        st_fail(session_, request_);
        resultStatus_->setText(QStringLiteral("OCR 실패 · 번역 없음"));
        const bool retry = autoTranslating() && activeSource_ == ST_SCREEN_CAPTURE &&
                           captureWorker_.retryAfterFailure(autoClock_.elapsed());
        finish(retry ? error + QStringLiteral("\n자동 번역: 3초 뒤 다시 확인합니다 (같은 화면에서 최대 2회).") : error);
    });
    connect(&translation_, &TranslationService::translated, this, [this](const QStringList& pieces, const QString& detected) {
        if (!busy_) return;
        std::vector<std::string> translations;
        for (const QString& piece : pieces) translations.push_back(piece.toStdString());
        const std::string joined = subtitle::joinTranslated(segments_, translations);
        if (joined.empty()) {
            st_fail(session_, request_);
            resultStatus_->setText(QStringLiteral("번역 실패 · 원문은 앱에서 확인할 수 있습니다."));
            finish(QStringLiteral("번역 응답 개수가 요청과 달라 결과를 표시하지 않았습니다."));
            return;
        }
        if (!st_complete(session_, request_, joined.c_str())) return;
        usage_.record(UsageMeter::characters(sentPieces_));
        const QString text = QString::fromStdString(joined);
        gameHistoryId_ = history_.add(QStringLiteral("game"), QString::fromUtf8(st_source_text(session_)), text);
        result_->setPlainText(text);
        const QString target = Languages::targetLabel(targetLanguage());
        const bool partial = std::any_of(segments_.begin(), segments_.end(), [](const auto& s) { return !s.translate; });
        resultStatus_->setText(Languages::sameLanguage(detected, targetLanguage())
            ? QStringLiteral("원문이 이미 %1입니다 · DeepL이 그대로 돌려준 문장입니다.").arg(target)
            : QStringLiteral("번역 완료 · DeepL · %1 → %2%3")
                  .arg(detected.isEmpty() ? QStringLiteral("자동 감지") : Languages::languageName(detected), target,
                       partial ? QStringLiteral(" · 외국어 부분만 번역") : QString()));
        presentTranslation();
        finish(autoTranslating() ? QStringLiteral("자동 번역 중 · 번역 완료. 다음 대사를 기다립니다.")
                                 : QStringLiteral("번역 완료. 다음 자막에서 %1을 누르세요.").arg(hotkeys_.text(HotkeyManager::Capture)));
    });
    connect(&translation_, &TranslationService::failed, this, [this](const QString& error) {
        if (!busy_) return;
        st_fail(session_, request_);
        QString message = error;
        if (translation_.requiresUserAction() && (autoTranslating() || hoverMode_->isChecked())) {
            setBusy(false); // Stopping hover must not change the failed request into a cancelled one.
            setAutoTranslate(false);
            setHoverTranslate(false);
            message += QStringLiteral("\n반복 요청을 막기 위해 자동 번역과 마우스 올려두기 번역을 멈췄습니다. 원인을 확인한 뒤 직접 다시 켜세요.");
        }
        resultStatus_->setText(message);
        finish(message);
    });
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
    restoreRegion();
    // Shared services for every mode.
    presenter_->setExcludeFromCapture(saved.value("display/excludeFromCapture", true).toBool());
    presenter_->setStyle(Modes::loadStyle(Mode::Game, presenter_->exclusionActive()));
    presenter_->track(resultWindow_);
    queue_ = new TranslationQueue([this](QObject* parent) -> ITranslator* { return new TranslationService(parent, queueTransport_); },
                                  &usage_, this);
    queue_->setConfigurator([this](ITranslator* translator) {
        if (auto* deepl = qobject_cast<TranslationService*>(translator))
            deepl->setCredentials(consent_ ? apiKey_ : QString(), freeApi_);
    });
    connect(&usage_, &UsageMeter::changed, this, &MainWindow::updateUsage);
    history_.setPersistent(saved.value("history/keep", false).toBool());
    updateUsage();
    registerHotkeys();
    QString ready = registered_ ? QStringLiteral("준비됨 · 먼저 설정을 확인하고 자막 영역을 선택하세요.")
        : QStringLiteral("%1 등록 실패: 다른 앱에서 사용 중일 수 있습니다. 캡처 버튼을 사용하세요.").arg(hotkeys_.sequence(HotkeyManager::Capture).toString());
    if (!autoRegistered_) ready += QStringLiteral("\n%1 등록 실패: 자동 번역 버튼을 사용하세요.").arg(hotkeys_.sequence(HotkeyManager::Auto).toString());
    if (!cursorRegistered_) ready += QStringLiteral("\nCtrl+Alt+M/F8 등록 실패: 마우스 번역 · 3초 준비 버튼을 사용하세요.");
    else if (cursorShortcut_ == QLatin1String("Ctrl+Alt+F8"))
        ready += QStringLiteral("\nCtrl+Alt+M 사용 중: 마우스 번역은 Ctrl+Alt+F8로 실행하세요.");
    if (!hoverRegistered_) ready += QStringLiteral("\n마우스 올려두기 단축키 등록 실패: 체크 상자로 켜고 끄세요.");
    status_->setText(ready);
    createTray();
    const bool setupComplete = saved.value("setupComplete", false).toBool();
    const Mode startMode = Modes::fromId(saved.value("mode/current").toString());
    if (startMode != Mode::Game) setMode(startMode);
    // The desktop shortcut passes --auto: the user chose to start translating right away.
    if (QCoreApplication::arguments().contains("--auto")) {
        if (mode_ == Mode::Video && videoMode_ && videoMode_->hasRegion()) videoMode_->setWatching(true);
        else if (setupComplete && !region_.isEmpty()) { if (mode_ != Mode::Game) setMode(Mode::Game); setAutoTranslate(true); }
        else {
            autoPending_ = true;
            status_->setText(QStringLiteral("설정을 마치고 자막 영역을 선택하면 자동 번역이 바로 시작됩니다."));
        }
    }
    if (!credentialError.isEmpty()) status_->setText(credentialError);
    if (!setupComplete &&
        !QCoreApplication::arguments().contains("--smoke-test") && !qApp->property("testMode").toBool())
        QTimer::singleShot(0, this, &MainWindow::firstRun);
}
MainWindow::~MainWindow() {
    hoverTimer_->stop();
    autoTimer_->stop();
    if (videoMode_) videoMode_->setWatching(false);
    cancel();
    hotkeys_.unregisterAll();
    if (historyWindow_) delete historyWindow_;
    delete resultWindow_;
    st_destroy(session_);
}
QString MainWindow::translationBlocked() const {
    if (apiKey_.isEmpty()) return QStringLiteral("DeepL API 키가 없어 번역하지 않습니다 · 설정에서 키를 입력하세요.");
    if (!consent_) return QStringLiteral("DeepL 전송 동의를 하지 않아 번역하지 않습니다 · 설정에서 확인하세요.");
    return {};
}
void MainWindow::loadLanguages(Mode mode) {
    const QSignalBlocker a(ocrLanguageBox_), b(targetLanguageBox_);
    const QSettings saved;
    const QString defaultSource = mode == Mode::File || mode == Mode::Text ? QStringLiteral("jpn+eng+kor") : QString::fromLatin1(Languages::DefaultOcr);
    const int ocrIndex = ocrLanguageBox_->findData(saved.value(Modes::languageKey(mode, false), defaultSource).toString());
    ocrLanguageBox_->setCurrentIndex(qMax(0, ocrIndex));
    const int targetIndex = targetLanguageBox_->findData(saved.value(Modes::languageKey(mode, true), Languages::DefaultTarget).toString());
    targetLanguageBox_->setCurrentIndex(qMax(0, targetIndex));
}
ModeContext MainWindow::context() {
    ModeContext c;
    c.ocr = &ocr_;
    c.queue = queue_;
    c.presenter = presenter_;
    c.history = &history_;
    c.usage = &usage_;
    c.ocrSetup = [this] { return OcrSetup{executable_, tessdata_, psm_, ocrLanguages()}; };
    c.ocrReady = [this] { return ocrReady(); };
    c.sourceLanguages = [this] { return ocrLanguages(); };
    c.targetLanguage = [this] { return targetLanguage(); };
    c.translationBlocked = [this] { return translationBlocked(); };
    c.status = [this](const QString& text) { status_->setText(text); updateTray(); };
    c.hideOwnWindows = [this] { hideOwnWindows(); };
    c.restoreOwnWindows = [this] { restoreWindows(); };
    c.minimizeMain = [this] { minimizeForCapture(); };
    c.ownWindowCovers = [this](const QRect& global) {
        // Windows hidden from capture never reach the picture; only the main window can.
        return (isVisible() && !isMinimized() && frameGeometry().intersects(global)) ||
               (!presenter_->exclusionActive() && resultWindow_->isVisible() && !resultWindow_->isMinimized() &&
                resultWindow_->frameGeometry().intersects(global));
    };
    c.showHistory = [this] { showHistory(); };
    return c;
}
QWidget* MainWindow::page(Mode mode) {
    if (mode == Mode::Game) return gamePage_;
    QWidget* slot = pages_->widget(int(mode));
    if (!slot->layout()->isEmpty()) return slot;
    QWidget* content = nullptr;
    if (mode == Mode::Video) {
        videoMode_ = new VideoMode(context());
        connect(videoMode_, &VideoMode::watchingChanged, this, [this] { updateTray(); });
        content = videoMode_;
    } else if (mode == Mode::Text) {
        textMode_ = new TextMode(context());
        content = textMode_;
    } else {
        fileMode_ = new FileMode(context());
        content = fileMode_;
    }
    slot->layout()->addWidget(content);
    return slot;
}
void MainWindow::stopScreenWork() {
    setHoverTranslate(false);
    setAutoTranslate(false);
    if (busy_) cancel();
    if (videoMode_) videoMode_->setWatching(false);
}
void MainWindow::setMode(Mode mode) {
    if (mode == mode_) {
        if (modeTabs_->currentIndex() != int(mode)) { const QSignalBlocker block(modeTabs_); modeTabs_->setCurrentIndex(int(mode)); }
        return;
    }
    if (modal_) { const QSignalBlocker block(modeTabs_); modeTabs_->setCurrentIndex(int(mode_)); return; }
    stopScreenWork(); // a file job keeps running: it does not use the screen
    mode_ = mode;
    QSettings().setValue("mode/current", Modes::id(mode));
    loadLanguages(mode);
    st_reset_cache(session_);
    captureWorker_.reset();
    page(mode);
    pages_->setCurrentIndex(int(mode));
    { const QSignalBlocker block(modeTabs_); modeTabs_->setCurrentIndex(int(mode)); }
    presenter_->hide();
    presenter_->setStyle(Modes::loadStyle(mode == Mode::Video ? Mode::Video : Mode::Game, presenter_->exclusionActive()));
    status_->setText(modeIntro(mode));
    updateTray();
}
void MainWindow::registerHotkeys() {
    hotkeys_.registerAll(quintptr(winId()));
    registered_ = hotkeys_.isRegistered(HotkeyManager::Capture);
    autoRegistered_ = hotkeys_.isRegistered(HotkeyManager::Auto);
    cursorRegistered_ = hotkeys_.isRegistered(HotkeyManager::Mouse);
    hoverRegistered_ = hotkeys_.isRegistered(HotkeyManager::Hover);
    if (cursorRegistered_) cursorShortcut_ = hotkeys_.text(HotkeyManager::Mouse);
    if (hoverRegistered_) hoverShortcut_ = hotkeys_.text(HotkeyManager::Hover);
    applyHotkeyTexts();
}
void MainWindow::applyHotkeyTexts() {
    const auto key = [this](HotkeyManager::Action action) { return hotkeys_.text(action); };
    capture_->setText(QStringLiteral("캡처·번역  %1").arg(registered_ ? key(HotkeyManager::Capture) : QString()).trimmed());
    auto_->setText(QStringLiteral("자동 번역  %1").arg(autoRegistered_ ? key(HotkeyManager::Auto) : QString()).trimmed());
    cursorShortcutLabel_->setText((cursorRegistered_ ? cursorShortcut_ : QStringLiteral("단축키 사용 불가")) + QStringLiteral(" · 범위"));
    QString note = QStringLiteral("자동 번역은 0.4초마다 자막 영역만 확인합니다. 창 모드·테두리 없는 창 모드를 권장합니다.\n");
    if (cursorRegistered_) {
        cursorCapture_->setToolTip(QStringLiteral("버튼을 누른 뒤 3초 안에 글자 위로 마우스를 옮기세요. %1은 즉시 실행합니다.").arg(cursorShortcut_));
        note += QStringLiteral("마우스 번역: 글자 위에서 %1. 주변 사각형을 한 번 읽으며, 자동 번역은 멈춥니다.").arg(cursorShortcut_);
    } else {
        cursorCapture_->setToolTip(QStringLiteral("버튼을 누른 뒤 3초 안에 글자 위로 마우스를 옮기세요."));
        note += QStringLiteral("마우스 번역: 3초 준비 버튼 사용. 주변 사각형을 한 번 읽으며, 자동 번역은 멈춥니다.");
    }
    note_->setText(note);
    hoverMode_->setText(hoverRegistered_ ? QStringLiteral("마우스 올려두기 번역 · %1").arg(hoverShortcut_)
                                         : QStringLiteral("마우스 올려두기 번역"));
    const HotkeyManager::Action modeKeys[] = {HotkeyManager::ModeGame, HotkeyManager::ModeVideo, HotkeyManager::ModeText};
    for (int i = 0; i < 3; ++i)
        modeTabs_->setTabToolTip(i, hotkeys_.isRegistered(modeKeys[i]) ? QStringLiteral("단축키 %1").arg(key(modeKeys[i])) : QString());
    modeTabs_->setTabToolTip(3, QStringLiteral("자막 파일을 끌어 놓으면 새 파일로 번역합니다"));
}
void MainWindow::createTray() {
    if (qApp->property("testMode").toBool() || QCoreApplication::arguments().contains("--smoke-test") ||
        !QSystemTrayIcon::isSystemTrayAvailable()) return;
    tray_ = new QSystemTrayIcon(this);
    auto* menu = new QMenu(this);
    menu->addAction(QStringLiteral("창 열기"), this, [this] { showNormal(); raise(); activateWindow(); });
    menu->addSeparator();
    auto* modes = new QActionGroup(menu);
    for (int i = 0; i < Modes::Count; ++i) {
        QAction* action = menu->addAction(Modes::label(Mode(i)) + QStringLiteral(" 모드"));
        action->setCheckable(true);
        action->setData(i);
        modes->addAction(action);
        connect(action, &QAction::triggered, this, [this, i] { setMode(Mode(i)); });
    }
    menu->addSeparator();
    menu->addAction(QStringLiteral("켜기 · 끄기 (현재 모드)"), this, [this] {
        if (mode_ == Mode::Video && videoMode_) videoMode_->toggle();
        else { if (mode_ != Mode::Game) setMode(Mode::Game); setAutoTranslate(!autoTranslating()); }
    });
    menu->addAction(QStringLiteral("번역 기록"), this, &MainWindow::showHistory);
    menu->addAction(QStringLiteral("설정"), this, [this] { showNormal(); raise(); settings(); });
    menu->addSeparator();
    menu->addAction(QStringLiteral("종료"), this, &QWidget::close);
    connect(menu, &QMenu::aboutToShow, this, [this, modes] {
        for (QAction* action : modes->actions()) action->setChecked(action->data().toInt() == int(mode_));
    });
    tray_->setContextMenu(menu);
    connect(tray_, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason != QSystemTrayIcon::Trigger && reason != QSystemTrayIcon::DoubleClick) return;
        if (isVisible() && !isMinimized()) showMinimized();
        else { showNormal(); raise(); activateWindow(); }
    });
    updateTray();
    tray_->show();
}
void MainWindow::updateTray() {
    if (!tray_) return;
    const bool active = autoTranslating() || hoverMode_->isChecked() || (videoMode_ && videoMode_->watching());
    tray_->setIcon(trayIcon(active));
    tray_->setToolTip(QStringLiteral("자막 번역 · %1 모드 · %2").arg(Modes::label(mode_), active ? QStringLiteral("켜짐") : QStringLiteral("꺼짐")));
}
void MainWindow::updateUsage() {
    usageLabel_->setText(QStringLiteral("DeepL 사용량(이 앱 집계) · ") + usage_.summary());
}
void MainWindow::showHistory() {
    if (!historyWindow_) {
        historyWindow_ = new HistoryWindow(&history_);
        historyWindow_->setAttribute(Qt::WA_DeleteOnClose, false);
        presenter_->track(historyWindow_);
    }
    historyWindow_->refresh();
    historyWindow_->show();
    historyWindow_->raise();
    historyWindow_->activateWindow();
}
void MainWindow::toast(const QString& text) {
    QScreen* screen = QGuiApplication::screenAt(QCursor::pos());
    if (!screen) screen = QGuiApplication::primaryScreen();
    presenter_->toast(text, screen->availableGeometry());
}
void MainWindow::hideOwnWindows() {
    restoreMain_ = isVisible() && !isMinimized(); restoreResult_ = resultWindow_->isVisible();
    restoreOverlay_ = false;
    if (!isMinimized()) hide();
    resultWindow_->hide(); presenter_->hide();
}
void MainWindow::activateFromLaunch(const QStringList& arguments) {
    if (arguments.contains(QStringLiteral("--auto"))) {
        // The desktop shortcut again: keep the game in front and make sure translation runs.
        const bool running = autoTranslating() || (videoMode_ && videoMode_->watching());
        if (!running) {
            if (mode_ == Mode::Video && videoMode_ && videoMode_->hasRegion()) videoMode_->setWatching(true);
            else { if (mode_ != Mode::Game) setMode(Mode::Game); setAutoTranslate(true); }
        }
        if (autoTranslating() || (videoMode_ && videoMode_->watching())) {
            toast(QStringLiteral("자막 번역이 이미 실행 중입니다"));
            return;
        }
    }
    showNormal();
    raise();
    activateWindow();
    status_->setText(QStringLiteral("자막 번역이 이미 실행 중이라 이 창을 앞으로 가져왔습니다."));
}
void MainWindow::firstRun() {
    if (busy_ || modal_) return;
    FirstRunWizard wizard(this, paddleEngine_ ? paddleDir_ : QString(), executable_, tessdata_);
    modal_ = true;
    const bool done = wizard.exec() == QDialog::Accepted;
    modal_ = false;
    if (!done) { settings(); return; }
    const auto result = wizard.choices();
    {
        QSettings saved;
        saved.setValue(Modes::languageKey(result.mode, false), result.source);
        saved.setValue(Modes::languageKey(result.mode, true), result.target);
    }
    if (!result.apiKey.isEmpty()) {
        QString error;
        if (result.rememberKey && !Credentials::save(result.apiKey, error))
            QMessageBox::warning(this, QStringLiteral("키 저장 실패"), error);
        apiKey_ = result.apiKey;
        freeApi_ = TranslationService::isFreeKey(result.apiKey);
        remember_ = result.rememberKey;
    }
    consent_ = result.consent && !apiKey_.isEmpty();
    QSettings saved;
    saved.setValue("translation/freeApi", freeApi_);
    saved.setValue("translation/consent", consent_);
    saved.setValue("translation/rememberKey", remember_);
    saved.setValue("setupComplete", true);
    if (result.mode == mode_) loadLanguages(mode_);
    else setMode(result.mode);
    status_->setText(modeIntro(result.mode));
}
void MainWindow::refreshScreens() {
    screens_->clear();
    const auto list = QGuiApplication::screens();
    for (int i = 0; i < list.size(); ++i)
        screens_->addItem(QStringLiteral("%1 · %2 (%3 × %4)").arg(i + 1).arg(list[i]->name())
            .arg(list[i]->size().width()).arg(list[i]->size().height()));
}
namespace {
// Settings backup: every value except the consent decision and anything key-related.
bool exportable(const QString& key) {
    static const QStringList prefixes{"ocr/", "translation/target", "translation/freeApi", "display/", "capture/",
        "cursor/", "modes/", "hotkeys/", "usage/capEnabled", "usage/sessionCap", "history/keep", "diagnostics/", "hints/", "mode/"};
    return std::any_of(prefixes.begin(), prefixes.end(), [&](const QString& p) { return key.startsWith(p); });
}
QJsonValue toJson(const QVariant& value) {
    switch (value.typeId()) {
    case QMetaType::QRect: { const QRect r = value.toRect(); return QJsonObject{{"rect", QJsonArray{r.x(), r.y(), r.width(), r.height()}}}; }
    case QMetaType::QSize: { const QSize s = value.toSize(); return QJsonObject{{"size", QJsonArray{s.width(), s.height()}}}; }
    case QMetaType::Bool: return value.toBool();
    case QMetaType::Int: case QMetaType::LongLong: case QMetaType::UInt: return value.toLongLong();
    case QMetaType::Double: return value.toDouble();
    default: return value.toString();
    }
}
QVariant fromJson(const QJsonValue& value) {
    if (value.isObject()) {
        const QJsonObject o = value.toObject();
        const QJsonArray r = o.value("rect").toArray(), s = o.value("size").toArray();
        if (r.size() == 4) return QRect(r[0].toInt(), r[1].toInt(), r[2].toInt(), r[3].toInt());
        if (s.size() == 2) return QSize(s[0].toInt(), s[1].toInt());
        return {};
    }
    return value.toVariant();
}
}
void MainWindow::settings() {
    if (busy_ || modal_) return;
    modal_ = true;
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("설정"));
    dialog.resize(720, 640);
    auto* outer = new QVBoxLayout(&dialog);
    auto* tabs = new QTabWidget;
    outer->addWidget(tabs);

    // ---------- OCR ----------
    auto* ocrPage = new QWidget;
    auto* layout = new QVBoxLayout(ocrPage);
    layout->addWidget(new QLabel(QStringLiteral("OCR 준비 · 원문 언어: %1").arg(ocrLanguageBox_->currentText())));
    auto* form = new QFormLayout;
    auto* exe = new QLineEdit(executable_);
    auto* tessdataEdit = new QLineEdit(tessdata_);
    auto pathRow = [&](QLineEdit* edit, bool directory) {
        auto* widget = new QWidget;
        auto* line = new QHBoxLayout(widget);
        line->setContentsMargins(0, 0, 0, 0);
        auto* browse = new QPushButton(QStringLiteral("찾기"));
        line->addWidget(edit); line->addWidget(browse);
        connect(browse, &QPushButton::clicked, &dialog, [&, edit, directory] {
            QString path = directory ? QFileDialog::getExistingDirectory(&dialog, QStringLiteral("tessdata 폴더"), edit->text())
                : QFileDialog::getOpenFileName(&dialog, QStringLiteral("Tesseract 실행 파일"), edit->text(), "Executable (*.exe)");
            if (!path.isEmpty()) edit->setText(path);
        });
        return widget;
    };
    auto* engine = new QComboBox;
    engine->addItem(QStringLiteral("PaddleOCR · 게임·영상 자막용 (권장)"), true);
    engine->addItem(QStringLiteral("Tesseract · 기존 방식"), false);
    engine->setCurrentIndex(paddleEngine_ ? 0 : 1);
    form->addRow(QStringLiteral("OCR 엔진"), engine);
    auto* mode = new QComboBox;
    mode->addItem(QStringLiteral("자동 · 선택 영역 높이로 판단"), 0);
    mode->addItem(QStringLiteral("여러 줄 자막 (PSM 6)"), 6);
    mode->addItem(QStringLiteral("한 줄 자막 (PSM 7)"), 7);
    mode->setCurrentIndex(qMax(0, mode->findData(psm_)));
    form->addRow(QStringLiteral("자막 배치"), mode);
    form->addRow(QStringLiteral("tesseract.exe"), pathRow(exe, false));
    form->addRow(QStringLiteral("tessdata 폴더"), pathRow(tessdataEdit, true));
    layout->addLayout(form);
    auto* probe = new QPushButton(QStringLiteral("OCR 설치·언어 모델 확인"));
    auto* probeStatus = new QLabel(ocrLanguages() == QLatin1String("vie")
        ? QStringLiteral("베트남어는 성조 문자를 읽는 Tesseract vie 모델을 사용합니다. OCR은 로컬에서 처리합니다.")
        : PaddleOcr::available(paddleDir_, ocrLanguages())
        ? QStringLiteral("PaddleOCR 모델이 앱에 들어 있습니다. PaddleOCR이 읽지 못하면 Tesseract가 대신 읽습니다. 키 없이 OCR만 사용할 수 있습니다.")
        : QStringLiteral("PaddleOCR 모델이 없어 Tesseract로 읽습니다. Tesseract 5와 선택한 원문 언어의 traineddata가 필요합니다."));
    probeStatus->setWordWrap(true);
    layout->addWidget(probe); layout->addWidget(probeStatus);
    layout->addStretch(1);
    OcrService checker;
    connect(probe, &QPushButton::clicked, &dialog, [&] {
        if (engine->currentData().toBool() && ocrLanguages() != QLatin1String("vie")) {
            const bool ready = PaddleOcr::available(paddleDir_, ocrLanguages());
            probeStatus->setText(ready
                ? QStringLiteral("선택 언어의 PaddleOCR 모델 파일을 확인했습니다. 실제 인식은 이미지로 시험하세요.")
                : QStringLiteral("선택 언어의 PaddleOCR 모델이 없습니다. 앱의 ocr/paddle 폴더를 확인하세요."));
            return;
        }
        if (ocrLanguages() == QLatin1String("multi")) {
            probeStatus->setText(QStringLiteral("다국어 자동 감지는 PaddleOCR에서만 지원합니다. 원문 언어 하나를 선택하세요."));
            return;
        }
        probe->setEnabled(false);
        probeStatus->setText(QStringLiteral("언어 모델 확인 중…"));
        checker.probe(exe->text().trimmed(), tessdataEdit->text().trimmed(), ocrLanguages());
    });
    connect(&checker, &OcrService::probeFinished, &dialog, [&](bool, const QString& message) {
        probeStatus->setText(message); probe->setEnabled(true);
    });
    connect(&checker, &OcrService::failed, &dialog, [&](const QString& message) {
        probeStatus->setText(message); probe->setEnabled(true);
    });
    tabs->addTab(ocrPage, QStringLiteral("OCR"));

    // ---------- DeepL and usage ----------
    auto* apiPage = new QWidget;
    auto* apiLayout = new QVBoxLayout(apiPage);
    auto* apiForm = new QFormLayout;
    auto* plan = new QComboBox;
    plan->addItem(QStringLiteral("DeepL API Free · api-free.deepl.com"));
    plan->addItem(QStringLiteral("DeepL API Pro · api.deepl.com"));
    plan->setCurrentIndex(freeApi_ ? 0 : 1);
    auto* key = new QLineEdit(apiKey_);
    key->setEchoMode(QLineEdit::Password);
    key->setPlaceholderText(QStringLiteral("API 키 입력 · 채팅이나 소스 코드에 적지 마세요"));
    key->setMaxLength(512);
    key->setAccessibleName(QStringLiteral("DeepL API 키"));
    apiForm->addRow(QStringLiteral("API 요금제"), plan);
    apiForm->addRow(QStringLiteral("API 키"), key);
    apiLayout->addLayout(apiForm);
    auto* checkKey = new QPushButton(QStringLiteral("키 확인 · 남은 양 보기"));
    auto* keyStatus = new QLabel(QStringLiteral("키 확인은 API 키만 DeepL에 보내 이번 달 사용량을 조회합니다. 번역 글자는 차감되지 않습니다."));
    keyStatus->setWordWrap(true);
    apiLayout->addWidget(checkKey); apiLayout->addWidget(keyStatus);
    TranslationService keyChecker;
    // DeepL API Free keys end with ":fx"; a mismatched endpoint fails with 403.
    connect(key, &QLineEdit::textEdited, &dialog, [&](const QString& text) {
        if (text.trimmed().isEmpty()) return;
        const int wanted = TranslationService::isFreeKey(text) ? 0 : 1;
        if (plan->currentIndex() == wanted) return;
        plan->setCurrentIndex(wanted);
        keyStatus->setText(wanted == 0 ? QStringLiteral("키 끝이 :fx라서 DeepL API Free로 맞췄습니다.")
            : QStringLiteral("키 끝에 :fx가 없어 DeepL API Pro로 맞췄습니다. Free 키라면 키 전체를 다시 복사하세요."));
    });
    connect(checkKey, &QPushButton::clicked, &dialog, [&] {
        if (key->text().trimmed().isEmpty()) { keyStatus->setText(QStringLiteral("먼저 API 키를 입력하세요.")); return; }
        checkKey->setEnabled(false);
        keyStatus->setText(QStringLiteral("DeepL에서 키 확인 중…"));
        keyChecker.checkUsage(key->text(), plan->currentIndex() == 0);
    });
    connect(&keyChecker, &TranslationService::usageChecked, &dialog, [&](qint64 used, qint64 limit) {
        checkKey->setEnabled(true);
        const QLocale locale;
        keyStatus->setText(limit > 0
            ? QStringLiteral("키 확인 완료 · 이번 청구 기간 %1 / %2자 사용 (남은 글자 %3자)")
                .arg(locale.toString(used), locale.toString(limit), locale.toString(qMax<qint64>(0, limit - used)))
            : QStringLiteral("키 확인 완료 · 이번 청구 기간 %1자 사용 · 한도는 DeepL 계정에서 확인하세요.").arg(locale.toString(used)));
    });
    connect(&keyChecker, &TranslationService::failed, &dialog, [&](const QString& error) {
        checkKey->setEnabled(true);
        keyStatus->setText(QStringLiteral("키 확인 실패 · ") + error);
    });
    auto* remember = new QCheckBox(QStringLiteral("이 Windows 계정의 자격 증명 관리자에 키 저장"));
    remember->setChecked(remember_);
    apiLayout->addWidget(remember);
    auto* privacy = new QLabel(QStringLiteral(
        "화면 이미지는 PC 메모리에서 OCR로 처리하며 파일로 저장하거나 서버에 보내지 않습니다.\n"
        "번역 시 인식한 원문 텍스트와 API 인증 정보가 HTTPS로 DeepL에 전송됩니다.\n"
        "자동·영상 번역을 켜면 자막이 바뀔 때마다 새 원문이 전송됩니다. 같은 원문은 다시 보내지 않습니다.\n"
        "요금제와 사용량에 따라 비용이 발생하거나 무료 한도가 소진될 수 있습니다.\n"
        "동의하지 않거나 키가 없으면 로컬 OCR까지만 실행합니다. 저장을 선택하지 않은 키는 종료 시 사라집니다."));
    Ui::panel(privacy);
    apiLayout->addWidget(privacy);
    auto* consent = new QCheckBox(QStringLiteral("인식 텍스트의 DeepL 전송과 비용 가능성을 확인했습니다"));
    consent->setChecked(consent_);
    apiLayout->addWidget(consent);
    auto* usageBox = new QGroupBox(QStringLiteral("사용량"));
    auto* usageLayout = new QVBoxLayout(usageBox);
    auto* usageNow = new QLabel(usage_.summary() + QStringLiteral("\n이 앱이 보낸 글자 수입니다. 정확한 청구량은 위의 [키 확인]으로 확인하세요."));
    usageNow->setWordWrap(true);
    usageLayout->addWidget(usageNow);
    auto* capRow = new QHBoxLayout;
    auto* capEnabled = new QCheckBox(QStringLiteral("이번 실행에서 이만큼 보내면 번역 멈추기"));
    capEnabled->setChecked(usage_.capEnabled());
    auto* cap = new QSpinBox;
    cap->setRange(1000, 10000000); cap->setSingleStep(10000); cap->setSuffix(QStringLiteral(" 자"));
    cap->setGroupSeparatorShown(true);
    cap->setValue(int(std::min<qint64>(usage_.cap(), 10000000)));
    cap->setAccessibleName(QStringLiteral("세션 사용량 상한"));
    capRow->addWidget(capEnabled, 1); capRow->addWidget(cap);
    usageLayout->addLayout(capRow);
    apiLayout->addWidget(usageBox);
    auto* links = new QLabel(QStringLiteral("<a href='https://www.deepl.com/your-account/keys'>DeepL API 키 발급·확인</a> · "
        "<a href='https://developers.deepl.com/docs/getting-started/auth'>DeepL API 안내</a> · "
        "<a href='https://tesseract-ocr.github.io/tessdoc/Installation.html'>Tesseract 설치 안내</a>"));
    links->setOpenExternalLinks(true);
    apiLayout->addWidget(links);
    apiLayout->addStretch(1);
    tabs->addTab(apiPage, QStringLiteral("DeepL·사용량"));

    // ---------- captions ----------
    auto* displayPage = new QWidget;
    auto* displayLayout = new QVBoxLayout(displayPage);
    auto* exclude = new QCheckBox(QStringLiteral("번역 자막·결과·기록 창을 화면 캡처에서 숨기기 (권장)"));
    exclude->setChecked(presenter_->excludeFromCapture());
    exclude->setEnabled(Capture::exclusionSupported());
    displayLayout->addWidget(exclude);
    auto* excludeNote = new QLabel(Capture::exclusionSupported()
        ? QStringLiteral("켜면 앱이 자기 번역 자막을 다시 읽지 않아, 원문 위에 덮어 표시할 수 있습니다.\n"
                         "대신 스크린샷·녹화·방송(OBS, 디스코드 화면 공유)에도 번역 창이 보이지 않습니다. 번역을 캡처해 공유하려면 끄세요.")
        : QStringLiteral("이 Windows 버전은 캡처에서 창을 숨기는 기능이 없습니다 (Windows 10 2004 이상 필요). 번역은 원문 아래에 표시합니다."));
    Ui::panel(excludeNote);
    displayLayout->addWidget(excludeNote);
    auto* gameStyle = new QGroupBox(QStringLiteral("게임 모드 자막 모양"));
    auto* styleForm = new QFormLayout(gameStyle);
    const SubtitleOverlay::Style currentGame = Modes::loadStyle(Mode::Game, presenter_->exclusionActive());
    auto* placement = new QComboBox;
    placement->addItem(QStringLiteral("원문 아래 (권장)"), int(SubtitleOverlay::Placement::Below));
    placement->addItem(QStringLiteral("원문 위쪽"), int(SubtitleOverlay::Placement::Above));
    placement->setCurrentIndex(qMax(0, placement->findData(int(currentGame.placement))));
    auto* fontScale = new QComboBox;
    for (const auto& [label, value] : {std::pair{"작게", 80}, {"보통", 100}, {"크게", 125}, {"아주 크게", 150}})
        fontScale->addItem(QString::fromUtf8(label), value);
    fontScale->setCurrentIndex(qMax(0, fontScale->findData(currentGame.fontScale)));
    auto* background = new QComboBox;
    for (const auto& [label, value] : {std::pair{"없음 · 글자 테두리만", 0}, {"반투명", 60}, {"진하게", 85}, {"불투명", 100}})
        background->addItem(QString::fromUtf8(label), value);
    background->setCurrentIndex(qMax(0, background->findData(currentGame.backgroundOpacity)));
    styleForm->addRow(QStringLiteral("위치"), placement);
    styleForm->addRow(QStringLiteral("글자 크기"), fontScale);
    styleForm->addRow(QStringLiteral("배경"), background);
    displayLayout->addWidget(gameStyle);
    displayLayout->addWidget(new QLabel(QStringLiteral("영상 모드의 자막 모양은 영상 탭에서 바로 바꿀 수 있습니다.")));
    displayLayout->addStretch(1);
    tabs->addTab(displayPage, QStringLiteral("자막 표시"));

    // ---------- hotkeys ----------
    auto* keyPage = new QWidget;
    auto* keyLayout = new QVBoxLayout(keyPage);
    keyLayout->addWidget(new QLabel(QStringLiteral("칸을 누르고 원하는 조합을 누르세요. Ctrl이나 Alt를 함께 눌러야 합니다. 비우면 그 단축키를 쓰지 않습니다.")));
    auto* keyGrid = new QGridLayout;
    struct KeyRow { HotkeyManager::Action action; QKeySequenceEdit* edit; QLabel* state; };
    QList<KeyRow> keyRows;
    int r = 0;
    for (const auto& binding : HotkeyManager::bindings()) {
        auto* label = new QLabel(QString::fromUtf8(binding.label));
        auto* edit = new QKeySequenceEdit(hotkeys_.sequence(binding.action));
        edit->setMaximumSequenceLength(1);
        edit->setClearButtonEnabled(true);
        edit->setAccessibleName(QString::fromUtf8(binding.label));
        label->setBuddy(edit);
        auto* state = new QLabel;
        keyGrid->addWidget(label, r, 0); keyGrid->addWidget(edit, r, 1); keyGrid->addWidget(state, r, 2);
        keyRows << KeyRow{binding.action, edit, state};
        ++r;
    }
    keyGrid->setColumnStretch(2, 1);
    keyLayout->addLayout(keyGrid);
    auto* keyDefaults = new QPushButton(QStringLiteral("기본값으로"));
    keyLayout->addWidget(keyDefaults, 0, Qt::AlignLeft);
    keyLayout->addStretch(1);
    const auto validateKeys = [&]() {
        bool ok = true;
        for (const auto& row : keyRows) {
            const QKeySequence sequence = row.edit->keySequence();
            QString text;
            if (sequence.isEmpty()) text = QStringLiteral("쓰지 않음 · 버튼으로 실행");
            else if (!HotkeyManager::toNative(sequence, nullptr, nullptr)) { text = QStringLiteral("Ctrl·Alt + 글자·숫자·F키로 정하세요"); ok = false; }
            else {
                for (const auto& other : keyRows)
                    if (other.action != row.action && other.edit->keySequence() == sequence) {
                        text = QStringLiteral("'%1'와 겹칩니다").arg(QString::fromUtf8(HotkeyManager::binding(other.action)->label));
                        ok = false;
                    }
                if (text.isEmpty()) {
                    bool ours = false;
                    for (const auto& binding : HotkeyManager::bindings()) ours = ours || hotkeys_.active(binding.action) == sequence;
                    text = ours || HotkeyManager::availableSystemWide(sequence) ? QStringLiteral("사용 가능")
                                                                                  : QStringLiteral("다른 앱이 쓰는 키입니다 · 저장해도 등록되지 않습니다");
                }
            }
            row.state->setText(text);
        }
        return ok;
    };
    for (const auto& row : keyRows) connect(row.edit, &QKeySequenceEdit::keySequenceChanged, &dialog, [&] { validateKeys(); });
    connect(keyDefaults, &QPushButton::clicked, &dialog, [&] {
        for (const auto& row : keyRows) row.edit->setKeySequence(HotkeyManager::binding(row.action)->standard);
        validateKeys();
    });
    validateKeys();
    tabs->addTab(keyPage, QStringLiteral("단축키"));

    // ---------- history, log, backup ----------
    auto* dataPage = new QWidget;
    auto* dataLayout = new QVBoxLayout(dataPage);
    auto* historyBox = new QGroupBox(QStringLiteral("번역 기록"));
    auto* historyLayout = new QVBoxLayout(historyBox);
    auto* forget = new QRadioButton(QStringLiteral("앱을 끄면 지우기 (기본)"));
    auto* keep = new QRadioButton(QStringLiteral("이 PC에 보관 (최근 5,000개)"));
    (history_.persistent() ? keep : forget)->setChecked(true);
    historyLayout->addWidget(forget); historyLayout->addWidget(keep);
    dataLayout->addWidget(historyBox);
    auto* logBox = new QGroupBox(QStringLiteral("진단 로그"));
    auto* logLayout = new QVBoxLayout(logBox);
    logLayout->addWidget(new QLabel(QStringLiteral("문제 분석용으로 처리 시간과 오류만 기록합니다 (최대 5MB × 3개). API 키는 기록하지 않습니다.")));
    auto* logText = new QCheckBox(QStringLiteral("원문·번역문도 로그에 남기기 (문제 분석 때만 켜세요)"));
    logText->setChecked(QSettings().value("diagnostics/logText", false).toBool());
    logLayout->addWidget(logText);
    auto* openLogs = new QPushButton(QStringLiteral("로그 폴더 열기"));
    logLayout->addWidget(openLogs, 0, Qt::AlignLeft);
    connect(openLogs, &QPushButton::clicked, &dialog, [] {
        QDir().mkpath(Diagnostics::defaultDirectory());
        QDesktopServices::openUrl(QUrl::fromLocalFile(Diagnostics::defaultDirectory()));
    });
    dataLayout->addWidget(logBox);
    auto* backupBox = new QGroupBox(QStringLiteral("설정 백업 (API 키와 전송 동의는 넣지 않습니다)"));
    auto* backupLayout = new QHBoxLayout(backupBox);
    auto* exportButton = new QPushButton(QStringLiteral("설정 내보내기"));
    auto* importButton = new QPushButton(QStringLiteral("설정 가져오기"));
    auto* backupStatus = new QLabel;
    backupStatus->setWordWrap(true);
    backupLayout->addWidget(exportButton); backupLayout->addWidget(importButton); backupLayout->addWidget(backupStatus, 1);
    connect(exportButton, &QPushButton::clicked, &dialog, [&] {
        const QString path = QFileDialog::getSaveFileName(&dialog, QStringLiteral("설정 내보내기"),
            QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + QStringLiteral("/자막번역-설정.json"),
            QStringLiteral("JSON (*.json)"));
        if (path.isEmpty()) return;
        QJsonObject values;
        const QSettings settings;
        for (const QString& name : settings.allKeys()) if (exportable(name)) values.insert(name, toJson(settings.value(name)));
        QSaveFile file(path);
        const QByteArray bytes = QJsonDocument(QJsonObject{{"app", "SubtitleTranslator"}, {"version", 1}, {"settings", values}}).toJson();
        if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
            backupStatus->setText(QStringLiteral("저장하지 못했습니다 · 다른 폴더를 고르세요."));
        else backupStatus->setText(QStringLiteral("설정 %1개를 저장했습니다.").arg(values.size()));
    });
    connect(importButton, &QPushButton::clicked, &dialog, [&] {
        const QString path = QFileDialog::getOpenFileName(&dialog, QStringLiteral("설정 가져오기"),
            QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation), QStringLiteral("JSON (*.json)"));
        if (path.isEmpty()) return;
        QFile file(path);
        const QJsonObject root = file.open(QIODevice::ReadOnly) && file.size() < 1024 * 1024
            ? QJsonDocument::fromJson(file.readAll()).object() : QJsonObject();
        if (root.value("app").toString() != QLatin1String("SubtitleTranslator")) {
            backupStatus->setText(QStringLiteral("가져오지 못했습니다 · 이 앱에서 내보낸 설정 파일이 아닙니다."));
            return;
        }
        QSettings settings;
        int count = 0;
        const QJsonObject values = root.value("settings").toObject();
        for (auto it = values.begin(); it != values.end(); ++it)
            if (exportable(it.key())) { const QVariant v = fromJson(it.value()); if (v.isValid()) { settings.setValue(it.key(), v); ++count; } }
        backupStatus->setText(QStringLiteral("설정 %1개를 가져왔습니다. 앱을 다시 시작하면 모두 적용됩니다.").arg(count));
    });
    dataLayout->addWidget(backupBox);
    dataLayout->addStretch(1);
    tabs->addTab(dataPage, QStringLiteral("기록·로그"));

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Save)->setText(QStringLiteral("설정 저장"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("취소"));
    outer->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        const QString enteredKey = key->text().trimmed();
        if (enteredKey.contains('\r') || enteredKey.contains('\n')) {
            tabs->setCurrentWidget(apiPage);
            QMessageBox::warning(&dialog, QStringLiteral("키 확인"), QStringLiteral("API 키에 줄바꿈을 넣을 수 없습니다.")); return;
        }
        if (!validateKeys()) {
            tabs->setCurrentWidget(keyPage);
            QMessageBox::warning(&dialog, QStringLiteral("단축키 확인"), QStringLiteral("단축키 탭의 빨간 안내를 먼저 고치세요. 겹치거나 쓸 수 없는 조합입니다."));
            return;
        }
        QString error;
        if (remember->isChecked() && !enteredKey.isEmpty()) {
            if (!Credentials::save(enteredKey, error)) {
                QMessageBox::warning(&dialog, QStringLiteral("키 저장 실패"), error); return;
            }
        } else if (!Credentials::remove(error)) {
            QMessageBox::warning(&dialog, QStringLiteral("기존 키 삭제 실패"), error); return;
        }
        executable_ = exe->text().trimmed(); tessdata_ = tessdataEdit->text().trimmed();
        st_reset_cache(session_);
        apiKey_ = enteredKey; freeApi_ = plan->currentIndex() == 0;
        consent_ = consent->isChecked(); remember_ = remember->isChecked() && !apiKey_.isEmpty();
        psm_ = mode->currentData().toInt();
        paddleEngine_ = engine->currentData().toBool();
        applyOcrEngine();
        QSettings saved;
        saved.setValue("ocr/engine", paddleEngine_ ? QStringLiteral("paddle") : QStringLiteral("tesseract"));
        saved.setValue("ocr/executable", executable_); saved.setValue("ocr/tessdata", tessdata_);
        saved.setValue("ocr/layout", psm_); saved.setValue("translation/freeApi", freeApi_);
        saved.setValue("translation/consent", consent_); saved.setValue("translation/rememberKey", remember_);
        saved.setValue("setupComplete", true);
        saved.setValue("display/excludeFromCapture", exclude->isChecked());
        saved.setValue("history/keep", keep->isChecked());
        saved.setValue("diagnostics/logText", logText->isChecked());
        for (const auto& row : keyRows)
            saved.setValue(QStringLiteral("hotkeys/") + QLatin1String(HotkeyManager::binding(row.action)->key),
                           row.edit->keySequence().toString(QKeySequence::PortableText));
        saved.sync();
        if (saved.status() != QSettings::NoError) {
            QMessageBox::warning(&dialog, QStringLiteral("설정 저장 실패"), QStringLiteral("Windows 사용자 설정을 저장할 수 없습니다.")); return;
        }
        SubtitleOverlay::Style style;
        style.placement = SubtitleOverlay::Placement(placement->currentData().toInt());
        style.fontScale = fontScale->currentData().toInt();
        style.backgroundOpacity = background->currentData().toInt();
        Modes::saveStyle(Mode::Game, style);
        usage_.setCap(capEnabled->isChecked(), cap->value());
        presenter_->setExcludeFromCapture(exclude->isChecked());
        presenter_->setStyle(Modes::loadStyle(mode_ == Mode::Video ? Mode::Video : Mode::Game, presenter_->exclusionActive()));
        history_.setPersistent(keep->isChecked());
        Diagnostics::setLogText(logText->isChecked());
        queue_->clearCache();
        dialog.accept();
    });
    dialog.exec();
    checker.cancel();
    keyChecker.cancel();
    modal_ = false;
    if (dialog.result() == QDialog::Accepted) {
        registerHotkeys();
        if (!hotkeys_.failed().isEmpty()) {
            QStringList names;
            for (auto action : hotkeys_.failed()) names << QString::fromUtf8(HotkeyManager::binding(action)->label);
            status_->setText(QStringLiteral("설정을 저장했습니다 · 단축키 일부를 등록하지 못했습니다(%1) · 다른 앱이 쓰는 키입니다. 설정 → 단축키에서 바꾸세요.")
                                 .arg(names.join(QStringLiteral(", "))));
        } else status_->setText(QStringLiteral("설정을 저장했습니다."));
    }
}
void MainWindow::chooseRegion() {
    if (busy_ || modal_) return;
    setHoverTranslate(false);
    const auto list = QGuiApplication::screens();
    if (screens_->currentIndex() < 0 || screens_->currentIndex() >= list.size()) return;
    QPointer<QScreen> screen = list[screens_->currentIndex()];
    modal_ = true;
    restoreMain_ = isVisible() && !isMinimized(); restoreResult_ = resultWindow_->isVisible();
    restoreOverlay_ = subtitleOverlay_->isVisible();
    if (!isMinimized()) hide();
    resultWindow_->hide(); subtitleOverlay_->hide();
    QTimer::singleShot(180, this, [this, screen] {
        if (!modal_ || !screen) { modal_ = false; restoreWindows(); return; }
        const QPixmap snapshot = screen->grabWindow(0);
        if (snapshot.isNull()) {
            modal_ = false; restoreWindows(); finish(QStringLiteral("모니터 화면을 읽을 수 없습니다.")); return;
        }
        RegionSelector selector(snapshot, screen->geometry());
        const bool selected = selector.exec() == QDialog::Accepted && screen;
        if (selected) {
            restoreOverlay_ = false;
            selectedScreen_ = screen; region_ = selector.selectedRegion();
            selectedGeometry_ = screen->geometry(); selectedDpr_ = screen->devicePixelRatio();
            regionLabel_->setText(describeRegion(screen, region_));
            saveRegion();
            captureWorker_.reset();
            status_->setText(QStringLiteral("영역 선택 완료. 자동 번역을 켜거나 Ctrl + Alt + T를 누르세요."));
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
    if (!selectedScreen_ || region_.isEmpty()) {
        status_->setText(QStringLiteral("먼저 자막 영역을 선택하세요.")); return;
    }
    beginScreenCapture(selectedArea(), QStringLiteral("선택 영역 캡처 중…"));
}
void MainWindow::scheduleCursorCapture() {
    if (busy_ || modal_) return;
    setHoverTranslate(false);
    setAutoTranslate(false);
    setBusy(true);
    const quint64 ticket = ++generation_;
    status_->setText(QStringLiteral("3초 안에 번역할 글자 위로 마우스를 옮기세요. 취소 버튼으로 중단할 수 있습니다."));
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
        status_->setText(QStringLiteral("마우스가 있는 모니터를 찾지 못했습니다. 화면 안으로 옮긴 뒤 다시 시도하세요."));
        return;
    }
    const auto rect = Capture::aroundPoint(screen->geometry(), point, cursorSizeBox_->currentData().toSize());
    if (rect.isEmpty()) {
        status_->setText(QStringLiteral("마우스 주변 캡처 범위를 만들지 못했습니다. 범위와 화면 설정을 확인하세요."));
        return;
    }
    beginScreenCapture({screen, screen->geometry(), rect, screen->devicePixelRatio()},
                       QStringLiteral("마우스 주변 캡처 중… · 자동 번역은 Ctrl+Alt+P로 다시 켤 수 있습니다."));
}
void MainWindow::minimizeForCapture() {
    showMinimized();
    const HWND handle = reinterpret_cast<HWND>(winId());
    // The launcher can override the first ShowWindow command through STARTUPINFO.
    // Qt may then report minimized while the actual HWND remains on the desktop.
    // Reconcile immediately; never queue a later minimize that could undo a user restore.
    if (!IsIconic(handle)) ShowWindow(handle, SW_MINIMIZE);
}
void MainWindow::setHoverTranslate(bool enabled) {
    QMetaObject::invokeMethod(this, [this] { updateTray(); }, Qt::QueuedConnection);
    const bool wasEnabled = hoverMode_->isChecked() || hoverTimer_->isActive();
    const QSignalBlocker blocker(hoverMode_);
    if (enabled && modal_) {
        hoverMode_->setChecked(wasEnabled);
        return;
    }
    if (enabled && !ocrReady()) {
        hoverMode_->setChecked(false);
        status_->setText(QStringLiteral("OCR 엔진을 먼저 준비하세요. 설정에서 모델과 실행 파일을 확인할 수 있습니다."));
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
        status_->setText(QStringLiteral("마우스 올려두기 번역을 껐습니다."));
        return;
    }
    setAutoTranslate(false);
    if (busy_) cancel();
    hoverSince_ = -1; hoverConsumed_ = false; hoverRequest_ = false;
    subtitleOverlay_->hide();
    hoverTimer_->start();
    // Keep the target visible. If no global stop key was registered, leave the checkbox visible.
    if (hoverRegistered_) minimizeForCapture();
    status_->setText(QStringLiteral("마우스 번역 켜짐 · 글자 위에서 1초 멈추세요. 같은 위치는 한 번만 읽습니다.%1")
        .arg(hoverRegistered_ ? QStringLiteral(" %1로 끕니다.").arg(hoverShortcut_) : QStringLiteral(" 체크를 해제하면 꺼집니다.")));
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
    constexpr qint64 DwellMs = 1000;
    if (hoverSince_ < 0 || (point - hoverAnchor_).manhattanLength() > MovementTolerance) {
        if (hoverRequest_ && busy_) cancel();
        if (hoverRequest_) subtitleOverlay_->hide();
        hoverRequest_ = false;
        hoverAnchor_ = point; hoverSince_ = nowMs; hoverConsumed_ = false;
        return;
    }
    if (hoverConsumed_ || busy_ || nowMs - hoverSince_ < DwellMs) return;
    hoverConsumed_ = true;
    if (!ocrReady()) {
        setHoverTranslate(false);
        status_->setText(QStringLiteral("OCR 엔진을 찾지 못해 마우스 번역을 껐습니다. 설정을 확인하세요."));
        return;
    }
    hoverRequest_ = true;
    beginScreenCapture({screen, screen->geometry(), local, screen->devicePixelRatio()},
        QStringLiteral("마우스 주변 캡처 중… · 마우스를 옮기면 현재 처리를 취소합니다."));
}
void MainWindow::beginScreenCapture(const CaptureArea& area, const QString& status) {
    if (busy_ || modal_) return;
    if (!area.screen || area.rect.isEmpty() || area.screen->geometry() != area.screenGeometry ||
        area.screen->devicePixelRatio() != area.dpr) {
        subtitleOverlay_->hide();
        status_->setText(QStringLiteral("모니터 해상도나 배율이 바뀌었습니다. 영역을 다시 선택하거나 마우스 번역을 다시 실행하세요."));
        return;
    }
    if (!ocrReady()) {
        status_->setText(QStringLiteral("OCR 엔진이 없습니다. 설정에서 tesseract.exe 경로를 지정하거나 PaddleOCR 모델을 확인하세요."));
        return;
    }
    setBusy(true);
    resetOutput();
    activeSource_ = ST_SCREEN_CAPTURE;
    captionArea_ = area;
    resultStatus_->setText(QStringLiteral("새 자막 처리 중 · 아직 번역 결과가 없습니다."));
    status_->setText(status);
    const quint64 ticket = ++generation_;
    restoreMain_ = isVisible() && !isMinimized(); restoreResult_ = resultWindow_->isVisible();
    restoreOverlay_ = subtitleOverlay_->isVisible();
    if (!isMinimized()) hide();
    resultWindow_->hide(); subtitleOverlay_->hide();
    QTimer::singleShot(180, this, [this, ticket, area] {
        if (!busy_ || ticket != generation_) return;
        QString error;
        QImage image;
        if (!area.screen || area.screen->geometry() != area.screenGeometry ||
            area.screen->devicePixelRatio() != area.dpr)
            error = QStringLiteral("캡처 전에 모니터 구성이 바뀌었습니다. 다시 시도하세요.");
        else
            image = Capture::region(area.screen, area.rect, error);
        restoreWindows();
        if (image.isNull()) { resultStatus_->setText(QStringLiteral("캡처 실패")); finish(error); return; }
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
    cursorWidth_->setEnabled(!busy); cursorHeight_->setEnabled(!busy);
}
void MainWindow::finish(const QString& status) {
    setBusy(false); status_->setText(resultPlacementWarning_.isEmpty() ? status : resultPlacementWarning_);
    if (activeSource_ == ST_SCREEN_CAPTURE && inlineCaptions_->isChecked() && result_->toPlainText().isEmpty())
        showSubtitle(QStringLiteral("번역 없음 · ") + status);
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
    st_cancel(session_);
    ocr_.cancel(); translation_.cancel();
    restoreWindows();
    if (wasBusy) {
        subtitleOverlay_->hide();
        status_->setText(QStringLiteral("처리를 취소했습니다. 이미 전송된 요청의 비용은 취소되지 않을 수 있습니다."));
        resultStatus_->setText(QStringLiteral("처리 취소 · 번역 결과 없음"));
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
    resultPlacementWarning_ = QStringLiteral("결과 창을 놓을 빈 공간이 없어 자동 번역을 멈췄습니다. 자막 영역을 줄이거나 결과 창을 다른 모니터로 옮긴 뒤 Ctrl+Alt+P로 다시 시작하세요.");
    status_->setText(resultPlacementWarning_);
    resultStatus_->setText(resultPlacementWarning_);
}
void MainWindow::showResult() {
    resultWindow_->show();
    if (autoTranslating() && !moveResultOutside(region_.translated(selectedGeometry_.topLeft())))
        stopAutoForResultOverlap();
}
bool MainWindow::showSubtitle(const QString& text, bool selectionPreview) {
    const auto area = selectionPreview || captionArea_.rect.isEmpty() ? selectedArea() : captionArea_;
    if (!inlineCaptions_->isChecked() || !area.screen || area.rect.isEmpty() ||
        area.screen->geometry() != area.screenGeometry || area.screen->devicePixelRatio() != area.dpr) {
        subtitleOverlay_->hide(); return false;
    }
    return subtitleOverlay_->showText(text, area.rect.translated(area.screenGeometry.topLeft()),
                                      area.screen->availableGeometry());
}
void MainWindow::presentTranslation() {
    if (activeSource_ == ST_SCREEN_CAPTURE && showSubtitle(result_->toPlainText())) return;
    showResult();
}
void MainWindow::previewSubtitle() {
    if (busy_ || modal_) return;
    if (!selectedScreen_ || region_.isEmpty()) {
        status_->setText(QStringLiteral("먼저 원문 글자 부분만 자막 영역으로 선택하세요.")); return;
    }
    inlineCaptions_->setChecked(true);
    if (showSubtitle(QStringLiteral("번역 위치 미리보기 · 실제 번역 아님"), true))
        status_->setText(QStringLiteral("미리보기 글자가 번역 위치입니다. 앱을 최소화하고 Ctrl+Alt+T로 번역하세요."));
    else status_->setText(QStringLiteral("자막을 표시할 빈 공간이 없습니다. 원문 글자 부분만 영역으로 다시 선택하세요."));
}
bool MainWindow::nativeEvent(const QByteArray&, void* message, qintptr* result) {
    auto* msg = static_cast<MSG*>(message);
    if (msg->message != WM_HOTKEY) return false;
    *result = 0;
    switch (int(msg->wParam)) {
    case HotkeyManager::Capture:
        if (videoMode_) videoMode_->setWatching(false);
        if (mode_ != Mode::Game) setMode(Mode::Game);
        startCapture();
        return true;
    case HotkeyManager::Auto: // on/off of the current screen mode
        if (mode_ == Mode::Video && videoMode_) videoMode_->toggle();
        else { if (mode_ != Mode::Game) setMode(Mode::Game); setAutoTranslate(!autoTranslating()); }
        return true;
    case HotkeyManager::Mouse:
        if (videoMode_) videoMode_->setWatching(false);
        startCursorCapture();
        return true;
    case HotkeyManager::Hover:
        if (videoMode_) videoMode_->setWatching(false);
        setHoverTranslate(!hoverMode_->isChecked());
        return true;
    case HotkeyManager::ModeGame:
        setMode(Mode::Game);
        toast(QStringLiteral("게임 모드 · %1: 자동 번역").arg(hotkeys_.text(HotkeyManager::Auto)));
        return true;
    case HotkeyManager::ModeVideo:
        if (mode_ != Mode::Video) setMode(Mode::Video);
        if (videoMode_ && videoMode_->hasRegion()) {
            videoMode_->toggle();
            toast(videoMode_->watching() ? QStringLiteral("영상 자막 번역 켜짐") : QStringLiteral("영상 자막 번역 꺼짐"));
        } else {
            showNormal(); raise(); activateWindow();
            status_->setText(QStringLiteral("영상 모드 · 먼저 [영상 창에 맞추기]로 자막 위치를 정하세요."));
        }
        return true;
    case HotkeyManager::ModeText:
        if (mode_ != Mode::Text) setMode(Mode::Text);
        if (textMode_) textMode_->translateClipboard();
        return true;
    default:
        return false;
    }
}
void MainWindow::closeEvent(QCloseEvent* event) {
    hoverTimer_->stop();
    autoTimer_->stop();
    if (videoMode_) videoMode_->setWatching(false);
    cancel();
    if (tray_) tray_->hide();
    if (historyWindow_) historyWindow_->hide();
    resultWindow_->hide(); subtitleOverlay_->hide();
    event->accept();
    QCoreApplication::quit();
}

void MainWindow::resetOutput() {
    resultPlacementWarning_.clear();
    original_->clear(); result_->clear(); preview_->clear();
    subtitleOverlay_->hide(); restoreOverlay_ = false;
    resultStatus_->setText(QStringLiteral("새 이미지 처리 중 · 아직 번역 결과가 없습니다."));
}
void MainWindow::processImage(const QImage& image, int source) {
    resultPlacementWarning_.clear();
    activeSource_ = source;
    // Every image source enters here. The core sees only portable metadata and UTF-8 OCR.
    request_ = st_begin(session_, source, image.width(), image.height());
    if (!request_) { finish(QStringLiteral("이미지가 너무 크거나 이전 요청이 진행 중입니다.")); return; }
    lastImage_ = image; lastSource_ = source;
    if (source == ST_SCREEN_CAPTURE) lastCaptionArea_ = captionArea_;
    setBusy(true);
    preview_->setPixmap(QPixmap::fromImage(image).scaled(preview_->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    status_->setText(QStringLiteral("PC에서 OCR 처리 중…"));
    ocr_.recognize(image, executable_, tessdata_, psm_, ocrLanguages());
}
void MainWindow::importImage() {
    if (busy_ || modal_) return;
    setHoverTranslate(false);
    modal_ = true;
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("번역할 이미지 선택"), {},
        QStringLiteral("Images (*.png *.jpg *.jpeg *.bmp *.webp)"));
    modal_ = false;
    if (path.isEmpty()) return;
    resetOutput(); setBusy(true); activeSource_ = ST_IMAGE_FILE;
    status_->setText(QStringLiteral("이미지 읽는 중…"));
    const quint64 ticket = ++generation_;
    auto* watcher = new QFutureWatcher<QImage>(this);
    connect(watcher, &QFutureWatcher<QImage>::finished, this, [this, watcher, ticket] {
        const auto image = watcher->result();
        watcher->deleteLater();
        if (ticket != generation_ || !busy_) return;
        if (image.isNull()) {
            finish(QStringLiteral("이미지를 읽지 못했습니다. PNG/JPEG 형식과 최대 1,600만 픽셀 제한을 확인하세요."));
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
    QMetaObject::invokeMethod(this, [this] { updateTray(); }, Qt::QueuedConnection);
    if (enabled) setHoverTranslate(false);
    if (enabled && videoMode_) videoMode_->setWatching(false);
    const QSignalBlocker blocker(auto_);
    if (enabled && (busy_ || modal_)) {
        auto_->setChecked(autoTranslating());
        status_->setText(QStringLiteral("현재 처리를 마치거나 취소한 뒤 자동 번역을 켜세요."));
        return;
    }
    captureWorker_.reset();
    ++autoTicket_; // frames of an earlier run are ignored
    if (!enabled) {
        autoPending_ = false;
        auto_->setChecked(false);
        if (!autoTimer_->isActive()) return;
        autoTimer_->stop();
        subtitleOverlay_->hide();
        status_->setText(QStringLiteral("자동 번역을 껐습니다. Ctrl + Alt + P로 다시 켜거나 Ctrl + Alt + T로 한 번씩 번역하세요."));
        return;
    }
    if (!selectedScreen_ || region_.isEmpty()) {
        auto_->setChecked(false);
        autoPending_ = true;
        status_->setText(QStringLiteral("먼저 자막 영역을 선택하세요. 선택하면 자동 번역이 바로 시작됩니다."));
        return;
    }
    autoPending_ = false;
    resultPlacementWarning_.clear();
    auto_->setChecked(true);
    autoTimer_->start();
    minimizeForCapture(); // Keep the app window off the game's subtitle.
    status_->setText(QStringLiteral("자동 번역 중 · 대사가 바뀌면 번역합니다. Ctrl + Alt + P로 멈춥니다."));
    captionArea_ = selectedArea();
    showSubtitle(QStringLiteral("자동 번역 켜짐 · 대사가 바뀌면 여기에 번역이 나옵니다 (Ctrl+Alt+P 멈춤)"));
}
void MainWindow::autoTick() {
    if (busy_ || modal_) return;
    if (!selectedScreen_ || region_.isEmpty() || selectedScreen_->geometry() != selectedGeometry_ ||
        selectedScreen_->devicePixelRatio() != selectedDpr_) {
        setAutoTranslate(false);
        status_->setText(QStringLiteral("모니터 구성이 바뀌어 자동 번역을 멈췄습니다. 영역을 다시 선택하세요."));
        return;
    }
    if (!ocrReady()) {
        setAutoTranslate(false);
        status_->setText(QStringLiteral("OCR 엔진이 없습니다. 설정에서 tesseract.exe 경로를 지정하거나 PaddleOCR 모델을 확인하세요."));
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
        status_->setText(QStringLiteral("앱 창이 자막 영역을 가리고 있어 자동 번역을 잠시 건너뜁니다. 창을 옮기거나 최소화하세요."));
        return;
    }
    // Capturing, shrinking and comparing run on the capture thread (QA R-15); frameReady brings
    // back a settled line to read. A check still running is simply skipped this tick.
    const QRect physical = Capture::physicalRect(selectedScreen_, region_);
    if (physical.isEmpty()) {
        setAutoTranslate(false);
        status_->setText(QStringLiteral("선택 영역이 유효하지 않습니다. 모니터와 영역을 다시 선택하세요."));
        return;
    }
    captureWorker_.requestCheck(physical, selectedDpr_, autoClock_.elapsed(), autoTicket_);
}
bool MainWindow::coversRegion(const QRect& global) const {
    return (isVisible() && !isMinimized() && frameGeometry().intersects(global)) ||
           (resultWindow_->isVisible() && !resultWindow_->isMinimized() && resultWindow_->frameGeometry().intersects(global)) ||
           (subtitleOverlay_->isVisible() && subtitleOverlay_->geometry().intersects(global));
}
void MainWindow::saveRegion() {
    QSettings saved;
    saved.setValue("capture/screen", selectedScreen_->name());
    saved.setValue("capture/screenGeometry", selectedGeometry_);
    saved.setValue("capture/devicePixelRatio", selectedDpr_);
    saved.setValue("capture/region", region_);
}
void MainWindow::restoreRegion() {
    QSettings saved;
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
        regionLabel_->setText(describeRegion(screen, region_) + QStringLiteral(" · 저장된 영역"));
        return;
    }
    regionLabel_->setText(QStringLiteral("저장한 영역의 모니터 구성이 바뀌었습니다. 영역을 다시 선택하세요."));
}
QString MainWindow::ocrLanguages() const { return ocrLanguageBox_->currentData().toString(); }
QString MainWindow::targetLanguage() const { return targetLanguageBox_->currentData().toString(); }
void MainWindow::applyOcrEngine() {
    ocr_.setPaddleModelDir(paddleEngine_ && QDir(paddleDir_).exists() ? paddleDir_ : QString());
}
bool MainWindow::ocrReady() const {
    return (paddleEngine_ && PaddleOcr::available(paddleDir_, ocrLanguages())) ||
           (ocrLanguages() != QLatin1String("multi") && !executable_.isEmpty() && QFileInfo::exists(executable_));
}
