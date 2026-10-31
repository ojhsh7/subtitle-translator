// MainWindow's windows, tray, settings pages and shortcuts (docs/plan/2026-11-01-app-ui.ko.md).
// Capture, OCR and translation flow stays in main_window.cpp.
#include "main_window.h"
#include "credential_store.h"
#include "languages.h"
#include "paddle_ocr.h"
#include "subtitle_overlay.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QStyle>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
#include <iterator>
#include <windows.h>

namespace {
struct HotkeySpec { MainWindow::Hotkey id; const char* name; const char* label; Qt::Key key; unsigned fallback; };
// Order of the shortcut page. fallback: used when another app holds the default (earlier releases did the same).
constexpr HotkeySpec hotkeySpecs[] = {
    {MainWindow::RegionKey, "region", "자막 영역 선택", Qt::Key_R, VK_F7},
    {MainWindow::AutoKey, "auto", "자동 번역 켜기·끄기", Qt::Key_P, 0},
    {MainWindow::CaptureKey, "capture", "지금 한 번 번역", Qt::Key_T, 0},
    {MainWindow::CursorKey, "cursor", "마우스 주변 번역", Qt::Key_M, VK_F8},
    {MainWindow::HoverKey, "hover", "마우스 올려두기 켜기·끄기", Qt::Key_H, VK_F9},
};
QKeySequence ctrlAlt(Qt::Key key) { return QKeySequence(QKeyCombination(Qt::ControlModifier | Qt::AltModifier, key)); }
QKeySequence defaultSequence(const HotkeySpec& spec) { return ctrlAlt(spec.key); }
QKeySequence storedSequence(const HotkeySpec& spec) {
    const QString stored = QSettings().value(QStringLiteral("hotkeys/%1").arg(QLatin1String(spec.name))).toString();
    return stored.isEmpty() ? defaultSequence(spec) : QKeySequence::fromString(stored, QKeySequence::PortableText);
}
QString keyText(const QKeySequence& sequence) { return sequence.toString(QKeySequence::NativeText); }

const char* const RunKey = "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run";
QString startupCommand() {
    return QStringLiteral("\"%1\" --auto").arg(QDir::toNativeSeparators(QCoreApplication::applicationFilePath()));
}

const char* const MainStyle = R"(
QLabel#title { font-size: 18px; font-weight: 600; }
QLabel#badge { padding: 3px 10px; border-radius: 10px; background: #e5e7eb; color: #374151; }
QLabel#badge[state="on"] { background: #dcfce7; color: #166534; }
QLabel#badge[state="busy"] { background: #dbeafe; color: #1e40af; }
QPushButton#primary { background: #2563eb; color: white; border: none; border-radius: 8px; font-size: 15px; font-weight: 600; padding: 10px; }
QPushButton#primary:hover { background: #1d4ed8; }
QPushButton#primary:checked { background: #dc2626; }
QPushButton#primary:checked:hover { background: #b91c1c; }
QLabel#status { background: #eef4fb; color: #152c47; padding: 10px; border-radius: 8px; }
QLabel#hint { color: #6b7280; }
QLabel#pageTitle { font-size: 16px; font-weight: 600; }
QListWidget#pages { border: none; background: transparent; font-size: 13px; }
QListWidget#pages::item { padding: 8px 10px; border-radius: 6px; }
QListWidget#pages::item:selected { background: #dbeafe; color: #1e3a8a; }
)";
}

QIcon MainWindow::appIcon() {
    // A blue tile with a glyph above a caption bar: "text on screen, translated below".
    QIcon icon;
    for (const int size : {16, 20, 24, 32, 48, 64, 128, 256}) {
        QPixmap pixmap(size, size);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing);
        QLinearGradient fill(0, 0, size, size);
        fill.setColorAt(0, QColor(0x3b, 0x82, 0xf6));
        fill.setColorAt(1, QColor(0x1d, 0x4e, 0xd8));
        painter.setPen(Qt::NoPen);
        painter.setBrush(fill);
        const QRectF tile = QRectF(0, 0, size, size).adjusted(size * 0.04, size * 0.04, -size * 0.04, -size * 0.04);
        painter.drawRoundedRect(tile, size * 0.22, size * 0.22);
        painter.setBrush(QColor(255, 255, 255, 235));
        painter.drawRoundedRect(QRectF(size * 0.22, size * 0.7, size * 0.56, size * 0.09), size * 0.045, size * 0.045);
        QFont font(QStringLiteral("Malgun Gothic"));
        font.setBold(true);
        font.setPixelSize(qMax(8, int(size * 0.46)));
        painter.setFont(font);
        painter.setPen(Qt::white);
        painter.drawText(QRectF(0, size * 0.1, size, size * 0.56), Qt::AlignCenter, QStringLiteral("자"));
        icon.addPixmap(pixmap);
    }
    return icon;
}

bool MainWindow::nativeKey(const QKeySequence& sequence, unsigned& modifiers, unsigned& virtualKey) {
    if (sequence.count() != 1) return false;
    const QKeyCombination combination = sequence[0];
    const Qt::KeyboardModifiers held = combination.keyboardModifiers();
    const int key = combination.key();
    modifiers = MOD_NOREPEAT;
    if (held & Qt::ControlModifier) modifiers |= MOD_CONTROL;
    if (held & Qt::AltModifier) modifiers |= MOD_ALT;
    if (held & Qt::ShiftModifier) modifiers |= MOD_SHIFT;
    if (held & Qt::MetaModifier) modifiers |= MOD_WIN;
    if (key >= Qt::Key_F1 && key <= Qt::Key_F24) { virtualKey = VK_F1 + unsigned(key - Qt::Key_F1); return true; }
    const bool letter = key >= Qt::Key_A && key <= Qt::Key_Z, digit = key >= Qt::Key_0 && key <= Qt::Key_9;
    if (!letter && !digit) return false;
    if (!(held & (Qt::ControlModifier | Qt::AltModifier))) return false;
    virtualKey = unsigned(key); // 'A'-'Z' and '0'-'9' have the same codes in Qt and Win32
    return true;
}

void MainWindow::registerHotkeys() {
    const HWND handle = reinterpret_cast<HWND>(winId());
    for (const auto& spec : hotkeySpecs) UnregisterHotKey(handle, spec.id);
    for (const auto& spec : hotkeySpecs) {
        const QKeySequence wanted = storedSequence(spec);
        const bool custom = wanted != defaultSequence(spec);
        unsigned modifiers = 0, key = 0;
        bool ok = nativeKey(wanted, modifiers, key) && RegisterHotKey(handle, spec.id, modifiers, key);
        QString text = keyText(wanted);
        // A key the user chose is reported as taken, never silently swapped for another.
        if (!ok && !custom && spec.fallback) {
            ok = RegisterHotKey(handle, spec.id, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, spec.fallback);
            if (ok) text = keyText(ctrlAlt(Qt::Key(Qt::Key_F1 + int(spec.fallback - VK_F1))));
        }
        switch (spec.id) {
        case CaptureKey: registered_ = ok; captureShortcut_ = text; break;
        case AutoKey: autoRegistered_ = ok; autoShortcut_ = text; break;
        case CursorKey: cursorRegistered_ = ok; cursorShortcut_ = text; break;
        case HoverKey: hoverRegistered_ = ok; hoverShortcut_ = text; break;
        case RegionKey: regionRegistered_ = ok; regionShortcut_ = text; break;
        }
    }
    refreshUi();
}

QString MainWindow::withShortcuts(QString text) const {
    // Messages name the default keys; show the keys in use instead. Placeholders first, so swapped
    // keys (T for P and P for T) are not replaced twice.
    const std::pair<char, QString> keys[] = {{'T', captureShortcut_}, {'P', autoShortcut_}, {'R', regionShortcut_},
                                             {'M', cursorShortcut_}, {'H', hoverShortcut_}};
    for (int i = 0; i < 5; ++i) {
        const QChar letter = QLatin1Char(keys[i].first);
        const QString mark = QStringLiteral("\x01%1\x01").arg(i);
        text.replace(QStringLiteral("Ctrl+Alt+%1").arg(letter), mark);
        text.replace(QStringLiteral("Ctrl + Alt + %1").arg(letter), mark);
    }
    for (int i = 0; i < 5; ++i) text.replace(QStringLiteral("\x01%1\x01").arg(i), keys[i].second);
    return text;
}

void MainWindow::setStatus(const QString& text) {
    status_->setText(withShortcuts(text));
    refreshUi();
}

void MainWindow::refreshUi() {
    if (!stateBadge_) return; // still building the window
    const bool autoOn = autoTranslating(), hover = hoverMode_->isChecked();
    const QString state = busy_ ? QStringLiteral("처리 중") : autoOn ? QStringLiteral("자동 번역 중")
        : hover ? QStringLiteral("마우스 올려두기") : QStringLiteral("대기");
    stateBadge_->setText(state);
    stateBadge_->setProperty("state", busy_ ? "busy" : (autoOn || hover) ? "on" : "idle");
    stateBadge_->style()->unpolish(stateBadge_);
    stateBadge_->style()->polish(stateBadge_);
    auto withKey = [](const QString& label, bool registered, const QString& key) {
        return registered ? QStringLiteral("%1   %2").arg(label, key) : label;
    };
    auto_->setText(withKey(autoOn ? QStringLiteral("■  자동 번역 멈추기") : QStringLiteral("▶  자동 번역 시작"),
                           autoRegistered_, autoShortcut_));
    select_->setText(withKey(QStringLiteral("영역 선택"), regionRegistered_, regionShortcut_));
    select_->setToolTip(regionRegistered_ ? QStringLiteral("%1: 게임 중에도 마우스가 있는 모니터에서 바로 영역을 고릅니다.").arg(regionShortcut_)
                                          : QStringLiteral("자막이 나오는 부분을 드래그해 고릅니다."));
    capture_->setText(withKey(QStringLiteral("지금 번역"), registered_, captureShortcut_));
    cursorCapture_->setToolTip(cursorRegistered_
        ? QStringLiteral("버튼을 누른 뒤 3초 안에 글자 위로 마우스를 옮기세요. %1은 바로 실행합니다.").arg(cursorShortcut_)
        : QStringLiteral("버튼을 누른 뒤 3초 안에 글자 위로 마우스를 옮기세요."));
    hoverMode_->setText(withKey(QStringLiteral("마우스 올려두기 번역"), hoverRegistered_, hoverShortcut_));
    if (tray_) {
        {
            const QSignalBlocker autoBlocker(trayAuto_), hoverBlocker(trayHover_);
            trayAuto_->setChecked(autoOn);
            trayHover_->setChecked(hover);
        }
        trayAuto_->setText(withKey(QStringLiteral("자동 번역"), autoRegistered_, autoShortcut_));
        trayHover_->setText(withKey(QStringLiteral("마우스 올려두기 번역"), hoverRegistered_, hoverShortcut_));
        tray_->setToolTip(QStringLiteral("자막 번역 · %1").arg(state));
    }
}

void MainWindow::buildUi() {
    setWindowTitle(QStringLiteral("자막 번역"));
    setWindowIcon(appIcon());
    resize(500, 600);
    setMinimumWidth(460);
    setAttribute(Qt::WA_ShowWithoutActivating);
    auto* central = new QWidget(this);
    central->setStyleSheet(QString::fromLatin1(MainStyle));
    setCentralWidget(central);
    auto* layout = new QVBoxLayout(central);
    layout->setContentsMargins(20, 16, 20, 16);
    layout->setSpacing(10);

    // Header: name, what the app is doing, settings.
    auto* header = new QHBoxLayout;
    auto* logo = new QLabel;
    logo->setPixmap(appIcon().pixmap(28, 28));
    auto* title = new QLabel(QStringLiteral("자막 번역"));
    title->setObjectName(QStringLiteral("title"));
    stateBadge_ = new QLabel(QStringLiteral("대기"));
    stateBadge_->setObjectName(QStringLiteral("badge"));
    settings_ = new QPushButton(QStringLiteral("설정"));
    settings_->setToolTip(QStringLiteral("OCR · DeepL · 자막 모양 · 캡처 · 단축키"));
    header->addWidget(logo);
    header->addWidget(title);
    header->addStretch(1);
    header->addWidget(stateBadge_);
    header->addWidget(settings_);
    layout->addLayout(header);

    // Languages: the one choice that changes with every game.
    ocrLanguageBox_ = new QComboBox;
    for (const auto& option : Languages::Ocr) ocrLanguageBox_->addItem(QString::fromUtf8(option.label), QString::fromLatin1(option.code));
    targetLanguageBox_ = new QComboBox;
    for (const auto& option : Languages::Target) targetLanguageBox_->addItem(QString::fromUtf8(option.label), QString::fromLatin1(option.code));
    {
        const QSettings saved;
        ocrLanguageBox_->setCurrentIndex(qMax(0, ocrLanguageBox_->findData(saved.value("ocr/languages", Languages::DefaultOcr).toString())));
        targetLanguageBox_->setCurrentIndex(qMax(0, targetLanguageBox_->findData(saved.value("translation/target", Languages::DefaultTarget).toString())));
    }
    auto* languages = new QGridLayout;
    auto* fromLabel = new QLabel(QStringLiteral("게임 언어"));
    auto* toLabel = new QLabel(QStringLiteral("번역할 언어"));
    fromLabel->setObjectName(QStringLiteral("hint"));
    toLabel->setObjectName(QStringLiteral("hint"));
    languages->addWidget(fromLabel, 0, 0);
    languages->addWidget(toLabel, 0, 2);
    languages->addWidget(ocrLanguageBox_, 1, 0);
    languages->addWidget(new QLabel(QStringLiteral("→")), 1, 1);
    languages->addWidget(targetLanguageBox_, 1, 2);
    languages->setColumnStretch(0, 3);
    languages->setColumnStretch(2, 2);
    layout->addLayout(languages);

    // Main action, then the occasional ones.
    auto_ = new QPushButton;
    auto_->setObjectName(QStringLiteral("primary"));
    auto_->setCheckable(true);
    auto_->setMinimumHeight(46);
    auto_->setToolTip(QStringLiteral("대사가 바뀔 때마다 원문 바로 아래에 번역을 띄웁니다."));
    layout->addWidget(auto_);
    auto* actions = new QHBoxLayout;
    select_ = new QPushButton;
    capture_ = new QPushButton;
    capture_->setToolTip(QStringLiteral("선택한 영역을 지금 한 번 읽고 번역합니다."));
    cancel_ = new QPushButton(QStringLiteral("취소"));
    cancel_->setEnabled(false);
    actions->addWidget(select_, 1);
    actions->addWidget(capture_, 1);
    actions->addWidget(cancel_);
    layout->addLayout(actions);
    auto* mouse = new QHBoxLayout;
    cursorCapture_ = new QPushButton(QStringLiteral("마우스 주변 번역 · 3초 뒤"));
    hoverMode_ = new QCheckBox(QStringLiteral("마우스 올려두기 번역"));
    hoverMode_->setToolTip(QStringLiteral("글자 위에서 1초 멈추면 한 번 번역합니다. 이동하면 다시 준비합니다. 앱을 다시 열면 꺼진 상태로 시작합니다."));
    mouse->addWidget(cursorCapture_, 1);
    mouse->addWidget(hoverMode_, 1);
    layout->addLayout(mouse);
    regionLabel_ = new QLabel(QStringLiteral("선택한 영역이 없습니다."));
    regionLabel_->setObjectName(QStringLiteral("hint"));
    regionLabel_->setWordWrap(true);
    layout->addWidget(regionLabel_);
    status_ = new QLabel(QStringLiteral("준비 중"));
    status_->setObjectName(QStringLiteral("status"));
    status_->setWordWrap(true);
    layout->addWidget(status_);

    // Recent result: folded away unless the user wants to check what was read.
    auto* resultsToggle = new QToolButton;
    resultsToggle->setText(QStringLiteral("최근 결과 · 캡처 이미지와 OCR 원문"));
    resultsToggle->setCheckable(true);
    resultsToggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    resultsToggle->setAutoRaise(true);
    layout->addWidget(resultsToggle);
    auto* results = new QWidget;
    auto* resultsLayout = new QVBoxLayout(results);
    resultsLayout->setContentsMargins(0, 0, 0, 0);
    preview_ = new QLabel(QStringLiteral("영역을 선택하고 캡처하면 이미지가 표시됩니다. 이 이미지는 외부로 전송하지 않습니다."));
    preview_->setAlignment(Qt::AlignCenter);
    preview_->setWordWrap(true);
    preview_->setMinimumHeight(90);
    preview_->setMaximumHeight(150);
    preview_->setStyleSheet("background:#202733; color:#d7dee9; border-radius:6px; padding:6px;");
    resultsLayout->addWidget(preview_);
    original_ = new QPlainTextEdit;
    original_->setReadOnly(true);
    original_->setPlaceholderText(QStringLiteral("OCR이 읽은 원문이 여기에 표시됩니다."));
    resultsLayout->addWidget(original_, 1);
    auto* resultActions = new QHBoxLayout;
    auto* resultButton = new QPushButton(QStringLiteral("번역 결과 창"));
    import_ = new QPushButton(QStringLiteral("이미지 파일 번역"));
    retry_ = new QPushButton(QStringLiteral("최근 이미지 다시"));
    retry_->setEnabled(false);
    resultActions->addWidget(resultButton);
    resultActions->addWidget(import_);
    resultActions->addWidget(retry_);
    resultsLayout->addLayout(resultActions);
    layout->addWidget(results, 10);
    layout->addStretch(1);
    auto showResults = [resultsToggle, results](bool shown) {
        results->setVisible(shown);
        resultsToggle->setArrowType(shown ? Qt::DownArrow : Qt::RightArrow);
    };
    const bool resultsShown = QSettings().value("ui/showResults", false).toBool();
    resultsToggle->setChecked(resultsShown);
    showResults(resultsShown);
    // The window fits the controls, and grows only while the results are open.
    auto fit = [this](bool shown) { resize(width(), shown ? qMax(height(), 640) : 0); };
    connect(resultsToggle, &QToolButton::toggled, this, [this, showResults, fit](bool shown) {
        showResults(shown);
        QSettings().setValue("ui/showResults", shown);
        QTimer::singleShot(0, this, [fit, shown] { fit(shown); });
    });
    fit(resultsShown);

    // Options shown on the settings pages; created here because the flow code uses them directly.
    screens_ = new QComboBox;
    cursorSizeBox_ = new QComboBox;
    cursorSizeBox_->addItem(QStringLiteral("좁게 · 360×100"), QSize(360, 100));
    cursorSizeBox_->addItem(QStringLiteral("보통 · 640×160"), QSize(640, 160));
    cursorSizeBox_->addItem(QStringLiteral("넓게 · 960×240"), QSize(960, 240));
    const int cursorSizeIndex = cursorSizeBox_->findData(QSettings().value("cursor/size", QSize(640, 160)));
    cursorSizeBox_->setCurrentIndex(cursorSizeIndex < 0 ? 1 : cursorSizeIndex);
    cursorSizeBox_->setToolTip(QStringLiteral("마우스 주변 캡처 범위 (화면 배율과 무관한 논리 픽셀)"));
    inlineCaptions_ = new QCheckBox(QStringLiteral("원문 바로 아래에 번역 자막 표시"));
    inlineCaptions_->setChecked(QSettings().value("display/inlineCaptions", true).toBool());
    captionPreview_ = new QPushButton(QStringLiteral("위치 미리보기"));
    followGame_ = new QCheckBox(QStringLiteral("게임 창을 따라 영역 이동"));
    followGame_->setToolTip(QStringLiteral("영역을 고른 게임 창을 기억합니다. 창을 옮기거나 크기를 바꿔도 같은 자리를 읽고, "
                                           "게임마다 영역을 따로 저장합니다."));
    followGame_->setChecked(QSettings().value("capture/followGame", true).toBool());
    showCaptureRange_ = new QCheckBox(QStringLiteral("읽은 범위 잠깐 표시"));
    showCaptureRange_->setToolTip(QStringLiteral("마우스 번역과 지금 번역이 캡처한 사각형을 0.6초 동안 테두리로 보여 줍니다."));
    showCaptureRange_->setChecked(QSettings().value("display/captureFlash", true).toBool());

    hoverTimer_ = new QTimer(this);
    hoverTimer_->setInterval(150);
    autoTimer_ = new QTimer(this);
    autoTimer_->setInterval(400);
    autoClock_.start();
    subtitleOverlay_ = new SubtitleOverlay;
    subtitleOverlay_->setStyle(QSettings().value("display/captionScale", 100).toInt() / 100.0,
                               QSettings().value("display/captionBox", false).toBool());
    captureFlash_ = new CaptureFlash;

    resultWindow_ = new QWidget(nullptr, Qt::Window | Qt::WindowStaysOnTopHint);
    resultWindow_->setAttribute(Qt::WA_ShowWithoutActivating);
    resultWindow_->setWindowTitle(QStringLiteral("자막 번역 결과"));
    resultWindow_->setWindowIcon(appIcon());
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
    connect(resultButton, &QPushButton::clicked, this, &MainWindow::showResult);
    connect(settings_, &QPushButton::clicked, this, [this] { settings(); });
    connect(select_, &QPushButton::clicked, this, [this] { chooseRegion(); });
    connect(capture_, &QPushButton::clicked, this, &MainWindow::startCapture);
    connect(auto_, &QPushButton::toggled, this, &MainWindow::setAutoTranslate);
    connect(cancel_, &QPushButton::clicked, this, [this] {
        if (hoverMode_->isChecked()) setHoverTranslate(false);
        else cancel();
    });
    connect(hoverMode_, &QCheckBox::toggled, this, &MainWindow::setHoverTranslate);
    connect(hoverTimer_, &QTimer::timeout, this, &MainWindow::hoverTick);
    connect(cursorCapture_, &QPushButton::clicked, this, &MainWindow::scheduleCursorCapture);
    connect(cursorSizeBox_, &QComboBox::currentIndexChanged, this, [this] {
        QSettings().setValue("cursor/size", cursorSizeBox_->currentData());
        hoverSince_ = -1; hoverConsumed_ = false;
    });
    connect(import_, &QPushButton::clicked, this, &MainWindow::importImage);
    connect(retry_, &QPushButton::clicked, this, [this] {
        if (!busy_ && !lastImage_.isNull()) {
            setHoverTranslate(false);
            captionArea_ = lastCaptionArea_;
            resetOutput(); processImage(lastImage_, lastSource_);
        }
    });
    connect(showCaptureRange_, &QCheckBox::toggled, this, [this](bool enabled) {
        QSettings().setValue("display/captureFlash", enabled);
        if (!enabled) captureFlash_->hide();
    });
    connect(captionPreview_, &QPushButton::clicked, this, &MainWindow::previewSubtitle);
    connect(inlineCaptions_, &QCheckBox::toggled, this, [this](bool enabled) {
        QSettings().setValue("display/inlineCaptions", enabled);
        if (!enabled) { subtitleOverlay_->hide(); restoreOverlay_ = false; }
        else if (!busy_ && lastSource_ == ST_SCREEN_CAPTURE && !result_->toPlainText().isEmpty())
            showSubtitle(result_->toPlainText());
    });
    buildSettings();
    buildTray();
}

void MainWindow::buildSettings() {
    settingsWindow_ = new QDialog(this);
    settingsWindow_->setWindowTitle(QStringLiteral("설정"));
    settingsWindow_->setWindowIcon(appIcon());
    settingsWindow_->resize(780, 600);
    settingsWindow_->setStyleSheet(QString::fromLatin1(MainStyle));
    auto* outer = new QVBoxLayout(settingsWindow_);
    auto* body = new QHBoxLayout;
    settingsPages_ = new QListWidget;
    settingsPages_->setObjectName(QStringLiteral("pages"));
    settingsPages_->setFixedWidth(150);
    auto* stack = new QStackedWidget;
    body->addWidget(settingsPages_);
    body->addWidget(stack, 1);
    outer->addLayout(body, 1);
    connect(settingsPages_, &QListWidget::currentRowChanged, stack, &QStackedWidget::setCurrentIndex);
    // One scrollable page per list entry: a title, a one-line summary, then its options.
    auto page = [&](const QString& name, const QString& summary) {
        settingsPages_->addItem(name);
        auto* scroll = new QScrollArea;
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        auto* content = new QWidget;
        auto* column = new QVBoxLayout(content);
        column->setContentsMargins(16, 4, 12, 4);
        column->setSpacing(10);
        auto* heading = new QLabel(name);
        heading->setObjectName(QStringLiteral("pageTitle"));
        auto* lead = new QLabel(summary);
        lead->setObjectName(QStringLiteral("hint"));
        lead->setWordWrap(true);
        column->addWidget(heading);
        column->addWidget(lead);
        scroll->setWidget(content);
        stack->addWidget(scroll);
        return column;
    };
    auto note = [](const QString& text) {
        auto* label = new QLabel(text);
        label->setObjectName(QStringLiteral("hint"));
        label->setWordWrap(true);
        return label;
    };

    // 0 General
    auto* general = page(QStringLiteral("일반"), QStringLiteral("앱을 닫고 여는 방식"));
    auto* closeToTray = new QCheckBox(QStringLiteral("닫기(X)를 누르면 트레이로 숨기기 · 단축키와 자동 번역은 계속 동작"));
    auto* startup = new QCheckBox(QStringLiteral("Windows에 로그인하면 자동으로 시작 (자동 번역 모드)"));
    general->addWidget(closeToTray);
    general->addWidget(startup);
    general->addWidget(note(QStringLiteral("트레이 아이콘을 클릭하면 이 앱 창이 열립니다. 오른쪽 클릭 메뉴에서 자동 번역을 켜고 끄거나 앱을 종료합니다.")));
    general->addStretch(1);
    connect(closeToTray, &QCheckBox::toggled, this, [](bool on) { QSettings().setValue("ui/closeToTray", on); });
    connect(startup, &QCheckBox::toggled, this, [](bool on) {
        QSettings run(QString::fromLatin1(RunKey), QSettings::NativeFormat);
        if (on) run.setValue("SubtitleTranslator", startupCommand());
        else run.remove("SubtitleTranslator");
    });

    // 1 Translation (DeepL)
    auto* deepl = page(QStringLiteral("번역"), QStringLiteral("DeepL API 키를 넣으면 인식한 글자를 번역합니다. 키가 없으면 OCR까지만 합니다."));
    auto* apiForm = new QFormLayout;
    auto* plan = new QComboBox;
    plan->addItem(QStringLiteral("DeepL API Free · api-free.deepl.com"));
    plan->addItem(QStringLiteral("DeepL API Pro · api.deepl.com"));
    auto* key = new QLineEdit;
    key->setEchoMode(QLineEdit::Password);
    key->setPlaceholderText(QStringLiteral("API 키 입력 · 채팅이나 소스 코드에 적지 마세요"));
    key->setMaxLength(512);
    apiForm->addRow(QStringLiteral("API 요금제"), plan);
    apiForm->addRow(QStringLiteral("API 키"), key);
    deepl->addLayout(apiForm);
    auto* checkKey = new QPushButton(QStringLiteral("키 확인 · 사용량 조회"));
    auto* keyStatus = note(QStringLiteral("키 확인은 API 키만 DeepL에 보내 이번 달 사용량을 조회합니다. 번역 글자는 차감되지 않습니다."));
    deepl->addWidget(checkKey);
    deepl->addWidget(keyStatus);
    auto* remember = new QCheckBox(QStringLiteral("이 Windows 계정의 자격 증명 관리자에 키 저장"));
    deepl->addWidget(remember);
    auto* privacy = new QLabel(QStringLiteral(
        "화면 이미지는 PC 메모리에서 OCR로 처리하며 파일로 저장하거나 서버에 보내지 않습니다.\n"
        "번역 시 인식한 원문 텍스트와 API 인증 정보가 HTTPS로 DeepL에 전송됩니다.\n"
        "자동 번역을 켜면 대사가 바뀔 때마다 새 원문이 전송됩니다. 바로 앞과 같은 원문은 다시 보내지 않습니다.\n"
        "요금제와 사용량에 따라 비용이 발생하거나 무료 한도가 소진될 수 있습니다. 계정의 한도를 확인하세요.\n"
        "동의하지 않거나 키가 없으면 로컬 OCR까지만 실행합니다. 저장을 선택하지 않은 키는 종료 시 사라집니다.\n"
        "번역 요청을 취소해도 이미 서버가 처리한 사용량은 청구될 수 있습니다."));
    privacy->setWordWrap(true);
    privacy->setObjectName(QStringLiteral("status"));
    deepl->addWidget(privacy);
    auto* consent = new QCheckBox(QStringLiteral("인식 텍스트의 DeepL 전송과 비용 가능성을 확인했습니다"));
    deepl->addWidget(consent);
    auto* links = new QLabel(QStringLiteral("<a href='https://www.deepl.com/your-account/keys'>DeepL API 키 발급·확인</a> · "
        "<a href='https://developers.deepl.com/docs/getting-started/auth'>DeepL API 안내</a>"));
    links->setOpenExternalLinks(true);
    deepl->addWidget(links);
    deepl->addStretch(1);
    auto* keyChecker = new TranslationService(settingsWindow_);
    // DeepL API Free keys end with ":fx"; a mismatched endpoint fails with 403.
    connect(key, &QLineEdit::textEdited, settingsWindow_, [plan, keyStatus](const QString& text) {
        if (text.trimmed().isEmpty()) return;
        const int wanted = TranslationService::isFreeKey(text) ? 0 : 1;
        if (plan->currentIndex() == wanted) return;
        plan->setCurrentIndex(wanted);
        keyStatus->setText(wanted == 0 ? QStringLiteral("키 끝이 :fx라서 DeepL API Free로 맞췄습니다.")
            : QStringLiteral("키 끝에 :fx가 없어 DeepL API Pro로 맞췄습니다. Free 키라면 키 전체를 다시 복사하세요."));
    });
    connect(checkKey, &QPushButton::clicked, settingsWindow_, [key, plan, checkKey, keyStatus, keyChecker] {
        if (key->text().trimmed().isEmpty()) { keyStatus->setText(QStringLiteral("먼저 API 키를 입력하세요.")); return; }
        checkKey->setEnabled(false);
        keyStatus->setText(QStringLiteral("DeepL에서 키 확인 중…"));
        keyChecker->checkUsage(key->text(), plan->currentIndex() == 0);
    });
    connect(keyChecker, &TranslationService::usageChecked, settingsWindow_, [checkKey, keyStatus](qint64 used, qint64 limit) {
        checkKey->setEnabled(true);
        const QLocale locale;
        keyStatus->setText(limit > 0
            ? QStringLiteral("키 확인 완료 · 이번 청구 기간 %1 / %2자 사용 (남은 글자 %3자)")
                .arg(locale.toString(used), locale.toString(limit), locale.toString(qMax<qint64>(0, limit - used)))
            : QStringLiteral("키 확인 완료 · 이번 청구 기간 %1자 사용 · 한도는 DeepL 계정에서 확인하세요.").arg(locale.toString(used)));
    });
    connect(keyChecker, &TranslationService::failed, settingsWindow_, [checkKey, keyStatus](const QString& error) {
        checkKey->setEnabled(true);
        keyStatus->setText(QStringLiteral("키 확인 실패 · ") + error);
    });

    // 2 Recognition (OCR)
    auto* recognition = page(QStringLiteral("인식"), QStringLiteral("화면 글자를 읽는 엔진. 게임 언어는 메인 창에서 고릅니다. OCR은 모두 이 PC 안에서 처리합니다."));
    auto* ocrForm = new QFormLayout;
    auto* engine = new QComboBox;
    engine->addItem(QStringLiteral("PaddleOCR · 게임 자막용 (권장)"), true);
    engine->addItem(QStringLiteral("Tesseract · 기존 방식"), false);
    auto* mode = new QComboBox;
    mode->addItem(QStringLiteral("자동 · 선택 영역 높이로 판단"), 0);
    mode->addItem(QStringLiteral("여러 줄 자막 (PSM 6)"), 6);
    mode->addItem(QStringLiteral("한 줄 자막 (PSM 7)"), 7);
    auto* exe = new QLineEdit;
    auto* tessdataEdit = new QLineEdit;
    auto pathRow = [this](QLineEdit* edit, bool directory) {
        auto* widget = new QWidget;
        auto* line = new QHBoxLayout(widget);
        line->setContentsMargins(0, 0, 0, 0);
        auto* browse = new QPushButton(QStringLiteral("찾기"));
        line->addWidget(edit); line->addWidget(browse);
        connect(browse, &QPushButton::clicked, settingsWindow_, [this, edit, directory] {
            const QString path = directory
                ? QFileDialog::getExistingDirectory(settingsWindow_, QStringLiteral("tessdata 폴더"), edit->text())
                : QFileDialog::getOpenFileName(settingsWindow_, QStringLiteral("Tesseract 실행 파일"), edit->text(), "Executable (*.exe)");
            if (!path.isEmpty()) edit->setText(path);
        });
        return widget;
    };
    ocrForm->addRow(QStringLiteral("OCR 엔진"), engine);
    ocrForm->addRow(QStringLiteral("자막 배치 (Tesseract)"), mode);
    ocrForm->addRow(QStringLiteral("tesseract.exe"), pathRow(exe, false));
    ocrForm->addRow(QStringLiteral("tessdata 폴더"), pathRow(tessdataEdit, true));
    recognition->addLayout(ocrForm);
    auto* probe = new QPushButton(QStringLiteral("OCR 설치·언어 모델 확인"));
    auto* probeStatus = note(QString());
    recognition->addWidget(probe);
    recognition->addWidget(probeStatus);
    auto* tesseractLink = note(QStringLiteral("<a href='https://tesseract-ocr.github.io/tessdoc/Installation.html'>Tesseract 설치 안내</a>"));
    tesseractLink->setOpenExternalLinks(true);
    recognition->addWidget(tesseractLink);
    recognition->addStretch(1);
    auto* checker = new OcrService(settingsWindow_);
    connect(probe, &QPushButton::clicked, settingsWindow_, [this, engine, exe, tessdataEdit, probe, probeStatus, checker] {
        if (engine->currentData().toBool() && ocrLanguages() != QLatin1String("vie")) {
            probeStatus->setText(PaddleOcr::available(paddleDir_, ocrLanguages())
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
        checker->probe(exe->text().trimmed(), tessdataEdit->text().trimmed(), ocrLanguages());
    });
    connect(checker, &OcrService::probeFinished, settingsWindow_, [probe, probeStatus](bool, const QString& message) {
        probeStatus->setText(message); probe->setEnabled(true);
    });
    connect(checker, &OcrService::failed, settingsWindow_, [probe, probeStatus](const QString& message) {
        probeStatus->setText(message); probe->setEnabled(true);
    });

    // 3 Captions
    auto* captions = page(QStringLiteral("자막 표시"), QStringLiteral("번역이 화면에 나오는 모양. 바로 적용됩니다."));
    captions->addWidget(inlineCaptions_);
    auto* look = new QFormLayout;
    auto* size = new QComboBox;
    size->addItem(QStringLiteral("작게"), 80);
    size->addItem(QStringLiteral("원문과 같은 크기 (기본)"), 100);
    size->addItem(QStringLiteral("크게"), 125);
    size->addItem(QStringLiteral("아주 크게"), 150);
    size->setCurrentIndex(qMax(0, size->findData(QSettings().value("display/captionScale", 100).toInt())));
    auto* background = new QComboBox;
    background->addItem(QStringLiteral("테두리만 · 게임 화면이 보임 (기본)"), false);
    background->addItem(QStringLiteral("반투명 상자 · 밝은 배경에서 잘 보임"), true);
    background->setCurrentIndex(QSettings().value("display/captionBox", false).toBool() ? 1 : 0);
    look->addRow(QStringLiteral("글자 크기"), size);
    look->addRow(QStringLiteral("배경"), background);
    captions->addLayout(look);
    captions->addWidget(captionPreview_);
    captions->addWidget(showCaptureRange_);
    captions->addWidget(note(QStringLiteral("자막은 클릭이 통과하고 게임 창의 포커스를 빼앗지 않습니다. 원문 아래에 자리가 없으면 위에, 그래도 없으면 번역 결과 창에 표시합니다.")));
    captions->addStretch(1);
    auto applyLook = [this, size, background] {
        QSettings().setValue("display/captionScale", size->currentData().toInt());
        QSettings().setValue("display/captionBox", background->currentData().toBool());
        subtitleOverlay_->setStyle(size->currentData().toInt() / 100.0, background->currentData().toBool());
        if (subtitleOverlay_->isVisible() && !result_->toPlainText().isEmpty()) showSubtitle(result_->toPlainText());
    };
    connect(size, &QComboBox::currentIndexChanged, this, applyLook);
    connect(background, &QComboBox::currentIndexChanged, this, applyLook);

    // 4 Capture
    auto* capture = page(QStringLiteral("캡처"), QStringLiteral("어디를 읽을지. 바로 적용됩니다."));
    capture->addWidget(followGame_);
    auto* captureForm = new QFormLayout;
    captureForm->addRow(QStringLiteral("영역 선택 버튼의 모니터"), screens_);
    captureForm->addRow(QStringLiteral("마우스 주변 번역 범위"), cursorSizeBox_);
    capture->addLayout(captureForm);
    capture->addWidget(note(QStringLiteral("자동 번역은 0.4초마다 선택한 영역만 확인하고, 대사가 멈추면 읽습니다. "
        "게임은 창 모드나 테두리 없는 창 모드를 권장합니다. 전체 화면 독점 모드는 캡처되지 않을 수 있습니다.")));
    capture->addStretch(1);

    // 5 Shortcuts
    auto* shortcuts = page(QStringLiteral("단축키"), QStringLiteral("게임 중에도 동작하는 전역 단축키. 저장을 누르면 적용됩니다."));
    auto* grid = new QGridLayout;
    QList<QKeySequenceEdit*> keyEdits;
    QList<QLabel*> keyStates;
    int row = 0;
    for (const auto& spec : hotkeySpecs) {
        auto* edit = new QKeySequenceEdit;
        edit->setMaximumSequenceLength(1);
        edit->setClearButtonEnabled(true);
        auto* state = new QLabel;
        grid->addWidget(new QLabel(QString::fromUtf8(spec.label)), row, 0);
        grid->addWidget(edit, row, 1);
        grid->addWidget(state, row, 2);
        keyEdits << edit; keyStates << state;
        ++row;
    }
    grid->setColumnStretch(1, 1);
    shortcuts->addLayout(grid);
    auto* defaults = new QPushButton(QStringLiteral("기본값으로 되돌리기"));
    shortcuts->addWidget(defaults, 0, Qt::AlignLeft);
    shortcuts->addWidget(note(QStringLiteral("글자·숫자 키는 Ctrl이나 Alt와 함께 눌러야 합니다(게임 입력을 가로채지 않도록). F1–F24는 단독으로 쓸 수 있습니다. "
        "다른 앱이 이미 쓰는 조합은 등록되지 않으니 다른 키를 고르세요.")));
    shortcuts->addStretch(1);
    connect(defaults, &QPushButton::clicked, settingsWindow_, [keyEdits] {
        for (int i = 0; i < keyEdits.size(); ++i) keyEdits[i]->setKeySequence(defaultSequence(hotkeySpecs[i]));
    });

    // 6 About
    auto* about = page(QStringLiteral("정보"), QStringLiteral("자막 번역 %1 · 게임 화면의 자막을 이 PC에서 읽고 DeepL로 번역합니다.")
        .arg(QCoreApplication::applicationVersion()));
    about->addWidget(note(QStringLiteral(
        "사용 순서: 게임 언어 고르기 → 영역 선택 → 자동 번역 시작. 한 번만 번역하려면 지금 번역.\n"
        "마우스 번역: 글자 위에 마우스를 두고 마우스 주변 번역 단축키를 누르면 주변 사각형을 한 번 읽습니다.\n"
        "마우스 올려두기: 켜 두면 글자 위에서 1초 멈출 때마다 한 번 번역합니다.\n"
        "화면 이미지는 저장하거나 전송하지 않습니다. DeepL에는 인식한 글자 중 외국어 부분만 보냅니다.")));
    about->addStretch(1);

    auto* bottom = new QHBoxLayout;
    bottom->addWidget(note(QStringLiteral("체크·선택 항목은 바로 적용됩니다. API 키·OCR 엔진·단축키는 저장을 눌러야 적용됩니다.")), 1);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Close);
    buttons->button(QDialogButtonBox::Save)->setText(QStringLiteral("저장"));
    buttons->button(QDialogButtonBox::Close)->setText(QStringLiteral("닫기"));
    bottom->addWidget(buttons);
    outer->addLayout(bottom);
    settingsPages_->setCurrentRow(0);

    reloadSettings_ = [=] {
        {
            const QSignalBlocker trayBlocker(closeToTray), startupBlocker(startup);
            closeToTray->setChecked(QSettings().value("ui/closeToTray", true).toBool());
            startup->setChecked(QSettings(QString::fromLatin1(RunKey), QSettings::NativeFormat)
                                    .value("SubtitleTranslator").toString() == startupCommand());
        }
        plan->setCurrentIndex(freeApi_ ? 0 : 1);
        key->setText(apiKey_);
        remember->setChecked(remember_);
        consent->setChecked(consent_);
        engine->setCurrentIndex(paddleEngine_ ? 0 : 1);
        mode->setCurrentIndex(qMax(0, mode->findData(psm_)));
        exe->setText(executable_);
        tessdataEdit->setText(tessdata_);
        probeStatus->setText(ocrLanguages() == QLatin1String("vie")
            ? QStringLiteral("베트남어는 성조 문자를 읽는 Tesseract vie 모델을 사용합니다.")
            : PaddleOcr::available(paddleDir_, ocrLanguages())
            ? QStringLiteral("PaddleOCR 모델이 앱에 들어 있습니다. PaddleOCR이 읽지 못하면 Tesseract가 대신 읽습니다. 키 없이 OCR만 사용할 수 있습니다.")
            : QStringLiteral("PaddleOCR 모델이 없어 Tesseract로 읽습니다. Tesseract 5와 선택한 원문 언어의 traineddata가 필요합니다."));
        const bool states[] = {regionRegistered_, autoRegistered_, registered_, cursorRegistered_, hoverRegistered_};
        for (int i = 0; i < keyEdits.size(); ++i) {
            keyEdits[i]->setKeySequence(storedSequence(hotkeySpecs[i]));
            keyStates[i]->setText(states[i] ? QStringLiteral("사용 중") : QStringLiteral("등록 안 됨 · 다른 앱이 쓰는 중"));
            keyStates[i]->setStyleSheet(states[i] ? "color:#166534;" : "color:#b91c1c;");
        }
    };
    saveSettings_ = [=]() -> bool {
        const QString enteredKey = key->text().trimmed();
        if (enteredKey.contains('\r') || enteredKey.contains('\n')) {
            settingsPages_->setCurrentRow(1);
            QMessageBox::warning(settingsWindow_, QStringLiteral("키 확인"), QStringLiteral("API 키에 줄바꿈을 넣을 수 없습니다."));
            return false;
        }
        // Shortcuts first: nothing is saved while one of them cannot be used.
        QList<QKeySequence> chosen;
        for (int i = 0; i < keyEdits.size(); ++i) {
            QKeySequence sequence = keyEdits[i]->keySequence();
            if (sequence.isEmpty()) sequence = defaultSequence(hotkeySpecs[i]);
            unsigned modifiers = 0, virtualKey = 0;
            if (!nativeKey(sequence, modifiers, virtualKey) || chosen.contains(sequence)) {
                settingsPages_->setCurrentRow(5);
                QMessageBox::warning(settingsWindow_, QStringLiteral("단축키"), chosen.contains(sequence)
                    ? QStringLiteral("%1: 다른 동작과 같은 키입니다.").arg(QString::fromUtf8(hotkeySpecs[i].label))
                    : QStringLiteral("%1: Ctrl이나 Alt와 함께 누르는 글자·숫자 키, 또는 F1–F24를 쓰세요.").arg(QString::fromUtf8(hotkeySpecs[i].label)));
                return false;
            }
            chosen << sequence;
        }
        QString error;
        if (remember->isChecked() && !enteredKey.isEmpty()) {
            if (!Credentials::save(enteredKey, error)) { QMessageBox::warning(settingsWindow_, QStringLiteral("키 저장 실패"), error); return false; }
        } else if (!Credentials::remove(error)) {
            QMessageBox::warning(settingsWindow_, QStringLiteral("기존 키 삭제 실패"), error); return false;
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
        for (int i = 0; i < chosen.size(); ++i) {
            const QString name = QStringLiteral("hotkeys/%1").arg(QLatin1String(hotkeySpecs[i].name));
            if (chosen[i] == defaultSequence(hotkeySpecs[i])) saved.remove(name);
            else saved.setValue(name, chosen[i].toString(QKeySequence::PortableText));
        }
        saved.setValue("setupComplete", true);
        saved.sync();
        if (saved.status() != QSettings::NoError) {
            QMessageBox::warning(settingsWindow_, QStringLiteral("설정 저장 실패"), QStringLiteral("Windows 사용자 설정을 저장할 수 없습니다."));
            return false;
        }
        registerHotkeys();
        reloadSettings_();
        const bool states[] = {regionRegistered_, autoRegistered_, registered_, cursorRegistered_, hoverRegistered_};
        if (std::find(std::begin(states), std::end(states), false) != std::end(states)) {
            settingsPages_->setCurrentRow(5);
            QMessageBox::information(settingsWindow_, QStringLiteral("단축키"),
                QStringLiteral("설정은 저장했습니다. 빨간색으로 표시된 단축키는 다른 앱이 쓰고 있어 등록하지 못했습니다. 다른 키로 바꿔 주세요."));
            return false;
        }
        return true;
    };
    connect(buttons, &QDialogButtonBox::accepted, settingsWindow_, [this] { if (saveSettings_()) settingsWindow_->accept(); });
    connect(buttons, &QDialogButtonBox::rejected, settingsWindow_, &QDialog::reject);
    connect(settingsWindow_, &QDialog::finished, this, [checker, keyChecker] { checker->cancel(); keyChecker->cancel(); });
}

void MainWindow::settings(int page) {
    if (busy_ || modal_) return;
    modal_ = true;
    if (page >= 0) settingsPages_->setCurrentRow(page);
    reloadSettings_();
    settingsWindow_->exec();
    modal_ = false;
    refreshUi();
}

void MainWindow::buildTray() {
    if (!QSystemTrayIcon::isSystemTrayAvailable()) return;
    tray_ = new QSystemTrayIcon(appIcon(), this);
    auto* menu = new QMenu(this);
    menu->addAction(QStringLiteral("창 열기"), this, &MainWindow::showFromTray);
    menu->addSeparator();
    menu->addAction(QStringLiteral("자막 영역 선택"), this, [this] { chooseRegionUnderCursor(); });
    menu->addAction(QStringLiteral("지금 번역"), this, &MainWindow::startCapture);
    trayAuto_ = menu->addAction(QStringLiteral("자동 번역"));
    trayAuto_->setCheckable(true);
    trayHover_ = menu->addAction(QStringLiteral("마우스 올려두기 번역"));
    trayHover_->setCheckable(true);
    menu->addSeparator();
    menu->addAction(QStringLiteral("설정"), this, [this] { showFromTray(); settings(); });
    menu->addAction(QStringLiteral("종료"), this, &MainWindow::quitApp);
    tray_->setContextMenu(menu);
    connect(trayAuto_, &QAction::triggered, this, [this](bool on) { setAutoTranslate(on); refreshUi(); });
    connect(trayHover_, &QAction::triggered, this, [this](bool on) { setHoverTranslate(on); refreshUi(); });
    connect(tray_, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) showFromTray();
    });
    tray_->show();
}

void MainWindow::showFromTray() {
    showNormal();
    raise();
    activateWindow();
}

void MainWindow::quitApp() {
    quitting_ = true;
    close();
}
