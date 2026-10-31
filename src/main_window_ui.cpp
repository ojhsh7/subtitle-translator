// MainWindow's windows, tray, settings pages and shortcuts. Capture, OCR and translation stay in
// main_window.cpp. Look and wording: docs/plan/2026-11-01-design-pass.ko.md (Windows 11 settings
// pattern, system palette and accent, one caption stage).
#include "main_window.h"
#include "credential_store.h"
#include "languages.h"
#include "paddle_ocr.h"
#include "subtitle_overlay.h"
#include "fluent_widgets.h"
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
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
    {MainWindow::AutoKey, "auto", "자동 번역 켜고 끄기", Qt::Key_P, 0},
    {MainWindow::CaptureKey, "capture", "지금 번역", Qt::Key_T, 0},
    {MainWindow::CursorKey, "cursor", "마우스 주변 번역", Qt::Key_M, VK_F8},
    {MainWindow::HoverKey, "hover", "마우스 올려두기 켜고 끄기", Qt::Key_H, VK_F9},
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
QFont weighted(QFont font, int pixelSize, int weight = QFont::Normal) {
    font.setPixelSize(pixelSize);
    font.setWeight(QFont::Weight(weight));
    return font;
}
// Sample line for the caption previews before anything was translated.
const char* const SampleSource = "今日はいい天気ですね。";
const char* const SampleCaption = "오늘은 날씨가 좋네요.";
}

QIcon MainWindow::appIcon() {
    // The stage in miniature: a dim original line above a white caption on a dark scene.
    QIcon icon;
    for (const int size : {16, 20, 24, 32, 48, 64, 128, 256}) {
        QPixmap pixmap(size, size);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor::fromRgba(Look::Scene));
        const qreal inset = size * 0.04;
        painter.drawRoundedRect(QRectF(inset, inset, size - 2 * inset, size - 2 * inset), size * 0.2, size * 0.2);
        const qreal bar = qMax(1.6, size * 0.1);
        painter.setBrush(QColor::fromRgba(Look::Source));
        painter.drawRoundedRect(QRectF(size * 0.2, size * 0.34, size * 0.36, bar * 0.8), bar * 0.4, bar * 0.4);
        painter.setBrush(QColor::fromRgba(Look::Caption));
        painter.drawRoundedRect(QRectF(size * 0.2, size * 0.54, size * 0.6, bar * 1.2), bar * 0.6, bar * 0.6);
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
    const QString state = busy_ ? QStringLiteral("읽는 중") : autoOn ? QStringLiteral("자동 번역 중")
        : hover ? QStringLiteral("마우스 올려두기") : QStringLiteral("대기");
    stateBadge_->setText(state);
    auto_->setText(QStringLiteral("자동 번역\n%1").arg(autoRegistered_ ? autoShortcut_ : QString()));
    static_cast<SwitchRow*>(auto_)->setDescription(autoOn ? QStringLiteral("대사가 바뀔 때마다 번역하고 있어요")
                                                          : QStringLiteral("대사가 바뀌면 바로 아래에 번역을 띄워요"));
    auto key = [](bool registered, const QString& shortcut) { return registered ? QStringLiteral(" (%1)").arg(shortcut) : QString(); };
    select_->setToolTip(QStringLiteral("자막이 나오는 부분을 드래그해서 골라요") + key(regionRegistered_, regionShortcut_));
    capture_->setToolTip(QStringLiteral("고른 영역을 지금 한 번 번역해요") + key(registered_, captureShortcut_));
    cursorCapture_->setToolTip(cursorRegistered_
        ? QStringLiteral("누르고 3초 안에 글자 위로 마우스를 옮기면 그 주변을 번역해요. %1은 바로 번역해요.").arg(cursorShortcut_)
        : QStringLiteral("누르고 3초 안에 글자 위로 마우스를 옮기면 그 주변을 번역해요."));
    static_cast<ToggleSwitch*>(hoverMode_)->setHint(hoverRegistered_ ? hoverShortcut_ : QString());
    cancel_->setVisible(busy_);
    if (tray_) {
        {
            const QSignalBlocker autoBlocker(trayAuto_), hoverBlocker(trayHover_);
            trayAuto_->setChecked(autoOn);
            trayHover_->setChecked(hover);
        }
        // A tab puts the shortcut in the menu's own shortcut column.
        trayAuto_->setText(QStringLiteral("자동 번역") + (autoRegistered_ ? QStringLiteral("\t") + autoShortcut_ : QString()));
        trayHover_->setText(QStringLiteral("마우스 올려두기") + (hoverRegistered_ ? QStringLiteral("\t") + hoverShortcut_ : QString()));
        tray_->setToolTip(QStringLiteral("자막 번역: %1").arg(state));
    }
}

void MainWindow::refreshIcons() {
    for (const auto& [button, glyph] : glyphButtons_) button->setIcon(Look::icon(glyph, button->palette()));
    if (!settingsPages_) return;
    for (int i = 0; i < settingsPages_->count(); ++i) {
        QListWidgetItem* item = settingsPages_->item(i);
        item->setIcon(Look::icon(char16_t(item->data(Qt::UserRole).toInt()), settingsPages_->palette()));
    }
}

void MainWindow::changeEvent(QEvent* event) {
    QMainWindow::changeEvent(event);
    // Icons are drawn in the text colour; draw them again when Windows switches light and dark.
    if (event->type() == QEvent::ThemeChange || event->type() == QEvent::ApplicationPaletteChange) refreshIcons();
}

void MainWindow::buildUi() {
    setWindowTitle(QStringLiteral("자막 번역"));
    setWindowIcon(appIcon());
    setAttribute(Qt::WA_ShowWithoutActivating);
    setMinimumWidth(440);
    auto* central = new QWidget(this);
    setCentralWidget(central);
    auto* layout = new QVBoxLayout(central);
    layout->setContentsMargins(20, 14, 20, 18);
    layout->setSpacing(12);
    auto iconButton = [this](QAbstractButton* button, char16_t glyph) {
        glyphButtons_.emplace_back(button, glyph);
        button->setIcon(Look::icon(glyph, button->palette()));
        return button;
    };

    // Title line: the name, what the app is doing, more, settings.
    auto* header = new QHBoxLayout;
    auto* title = new QLabel(QStringLiteral("자막 번역"));
    title->setFont(weighted(font(), 20, QFont::DemiBold));
    stateBadge_ = new Note(QStringLiteral("대기"));
    stateBadge_->setWordWrap(false);
    auto* more = new QToolButton;
    more->setAutoRaise(true);
    more->setToolTip(QStringLiteral("더 보기"));
    more->setAccessibleName(QStringLiteral("더 보기"));
    more->setPopupMode(QToolButton::InstantPopup);
    iconButton(more, Look::More);
    settings_ = new QPushButton;
    settings_->setFlat(true);
    settings_->setToolTip(QStringLiteral("설정"));
    settings_->setAccessibleName(QStringLiteral("설정"));
    iconButton(settings_, Look::Settings);
    header->addWidget(title);
    header->addStretch(1);
    header->addWidget(stateBadge_);
    header->addSpacing(6);
    header->addWidget(more);
    header->addWidget(settings_);
    layout->addLayout(header);

    // The stage: the last line as it looks over the game.
    stage_ = new CaptionStage;
    stage_->setPlaceholder(QStringLiteral("자막 영역을 고르고 자동 번역을 켜면 마지막 대사가 여기에 보여요."));
    stage_->setStyle(QSettings().value("display/captionScale", 100).toInt() / 100.0, QSettings().value("display/captionBox", false).toBool());
    layout->addWidget(stage_);

    ocrLanguageBox_ = new QComboBox;
    for (const auto& option : Languages::Ocr) ocrLanguageBox_->addItem(QString::fromUtf8(option.label), QString::fromLatin1(option.code));
    targetLanguageBox_ = new QComboBox;
    for (const auto& option : Languages::Target) targetLanguageBox_->addItem(QString::fromUtf8(option.label), QString::fromLatin1(option.code));
    {
        const QSettings saved;
        ocrLanguageBox_->setCurrentIndex(qMax(0, ocrLanguageBox_->findData(saved.value("ocr/languages", Languages::DefaultOcr).toString())));
        targetLanguageBox_->setCurrentIndex(qMax(0, targetLanguageBox_->findData(saved.value("translation/target", Languages::DefaultTarget).toString())));
    }
    ocrLanguageBox_->setToolTip(QStringLiteral("게임 화면에 나오는 언어"));
    ocrLanguageBox_->setAccessibleName(QStringLiteral("게임 언어"));
    targetLanguageBox_->setToolTip(QStringLiteral("번역해서 보여 줄 언어"));
    targetLanguageBox_->setAccessibleName(QStringLiteral("번역할 언어"));
    auto* languages = new QHBoxLayout;
    languages->addWidget(ocrLanguageBox_, 3);
    languages->addWidget(new GlyphLabel(Look::Forward, 14));
    languages->addWidget(targetLanguageBox_, 2);
    layout->addLayout(languages);

    auto_ = new SwitchRow;
    layout->addWidget(auto_);
    auto* actions = new QHBoxLayout;
    actions->setSpacing(8);
    select_ = new QPushButton(QStringLiteral("영역 선택"));
    capture_ = new QPushButton(QStringLiteral("지금 번역"));
    cursorCapture_ = new QPushButton(QStringLiteral("마우스 주변"));
    cancel_ = new QPushButton(QStringLiteral("취소"));
    cancel_->setEnabled(false);
    cancel_->setVisible(false);
    iconButton(select_, Look::SelectArea);
    iconButton(capture_, Look::Translate);
    iconButton(cursorCapture_, Look::Mouse);
    for (QPushButton* button : {select_, capture_, cursorCapture_}) actions->addWidget(button, 1);
    actions->addWidget(cancel_);
    layout->addLayout(actions);
    hoverMode_ = new ToggleSwitch(QStringLiteral("마우스 올려두기"));
    hoverMode_->setToolTip(QStringLiteral("글자 위에서 1초 멈추면 그 주변을 한 번 번역해요. 앱을 다시 열면 꺼져 있어요."));
    layout->addWidget(hoverMode_);
    regionLabel_ = new Note(QStringLiteral("아직 고른 영역이 없어요."));
    layout->addWidget(regionLabel_);
    status_ = new QLabel(QStringLiteral("준비 중"));
    status_->setWordWrap(true);
    status_->setFont(weighted(font(), 13));
    layout->addWidget(status_);
    layout->addStretch(1);

    // What was read, for checking a result: a separate window from the more menu.
    detailsWindow_ = new QWidget(nullptr, Qt::Window);
    detailsWindow_->setWindowTitle(QStringLiteral("인식 내용"));
    detailsWindow_->setWindowIcon(appIcon());
    detailsWindow_->resize(560, 420);
    auto* details = new QVBoxLayout(detailsWindow_);
    details->setContentsMargins(16, 16, 16, 16);
    preview_ = new QLabel(QStringLiteral("캡처한 화면이 여기에 보여요. 이 이미지는 저장하거나 보내지 않아요."));
    preview_->setAlignment(Qt::AlignCenter);
    preview_->setWordWrap(true);
    preview_->setMinimumHeight(110);
    preview_->setMaximumHeight(180);
    preview_->setAutoFillBackground(true);
    {
        QPalette scene = preview_->palette();
        scene.setColor(QPalette::Window, QColor::fromRgba(Look::Scene));
        scene.setColor(QPalette::WindowText, QColor::fromRgba(Look::Source));
        preview_->setPalette(scene);
    }
    details->addWidget(preview_);
    original_ = new QPlainTextEdit;
    original_->setReadOnly(true);
    original_->setPlaceholderText(QStringLiteral("OCR이 읽은 원문이 여기에 보여요."));
    details->addWidget(original_, 1);
    auto* detailActions = new QHBoxLayout;
    retry_ = new QPushButton(QStringLiteral("이 이미지 다시 읽기"));
    retry_->setEnabled(false);
    import_ = new QPushButton(QStringLiteral("이미지 파일 번역"));
    detailActions->addWidget(retry_);
    detailActions->addWidget(import_);
    detailActions->addStretch(1);
    details->addLayout(detailActions);

    // Options shown on the settings pages; created here because the flow code uses them directly.
    screens_ = new QComboBox;
    cursorSizeBox_ = new QComboBox;
    cursorSizeBox_->addItem(QStringLiteral("좁게 (360×100)"), QSize(360, 100));
    cursorSizeBox_->addItem(QStringLiteral("보통 (640×160)"), QSize(640, 160));
    cursorSizeBox_->addItem(QStringLiteral("넓게 (960×240)"), QSize(960, 240));
    const int cursorSizeIndex = cursorSizeBox_->findData(QSettings().value("cursor/size", QSize(640, 160)));
    cursorSizeBox_->setCurrentIndex(cursorSizeIndex < 0 ? 1 : cursorSizeIndex);
    inlineCaptions_ = new ToggleSwitch;
    inlineCaptions_->setChecked(QSettings().value("display/inlineCaptions", true).toBool());
    captionPreview_ = new QPushButton(QStringLiteral("미리보기"));
    followGame_ = new ToggleSwitch;
    followGame_->setChecked(QSettings().value("capture/followGame", true).toBool());
    showCaptureRange_ = new ToggleSwitch;
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
    resultWindow_->setWindowTitle(QStringLiteral("번역 결과"));
    resultWindow_->setWindowIcon(appIcon());
    resultWindow_->resize(640, 300);
    auto* resultLayout = new QVBoxLayout(resultWindow_);
    resultStatus_ = new Note(QStringLiteral("아직 번역한 대사가 없어요."));
    resultLayout->addWidget(resultStatus_);
    result_ = new QPlainTextEdit;
    result_->setReadOnly(true);
    result_->setFont(QFont("Malgun Gothic", 16));
    result_->setPlaceholderText(QStringLiteral("번역이 끝나면 여기에 보여요."));
    resultLayout->addWidget(result_);
    auto* copy = new QPushButton(QStringLiteral("전체 선택"));
    resultLayout->addWidget(copy, 0, Qt::AlignLeft);

    auto* moreMenu = new QMenu(more);
    moreMenu->addAction(QStringLiteral("인식 내용 보기"), this, [this] { detailsWindow_->show(); detailsWindow_->raise(); });
    moreMenu->addAction(QStringLiteral("번역 결과 창"), this, &MainWindow::showResult);
    moreMenu->addAction(QStringLiteral("이미지 파일 번역…"), this, &MainWindow::importImage);
    moreMenu->addSeparator();
    moreMenu->addAction(QStringLiteral("트레이로 숨기기"), this, [this] { if (tray_) hide(); else showMinimized(); });
    more->setMenu(moreMenu);

    connect(copy, &QPushButton::clicked, result_, &QPlainTextEdit::selectAll);
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
    resize(460, 0); // as tall as the controls
}

void MainWindow::buildSettings() {
    settingsWindow_ = new QDialog(this);
    settingsWindow_->setWindowTitle(QStringLiteral("설정"));
    settingsWindow_->setWindowIcon(appIcon());
    settingsWindow_->resize(860, 640);
    auto* outer = new QVBoxLayout(settingsWindow_);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);
    auto* body = new QHBoxLayout;
    body->setContentsMargins(12, 12, 0, 0);
    settingsPages_ = new QListWidget;
    settingsPages_->setFixedWidth(200);
    settingsPages_->setFrameShape(QFrame::NoFrame);
    settingsPages_->setIconSize(QSize(16, 16));
    settingsPages_->setFont(weighted(font(), 14));
    {
        QPalette clear = settingsPages_->palette();
        clear.setColor(QPalette::Base, Qt::transparent);
        settingsPages_->setPalette(clear);
    }
    auto* stack = new QStackedWidget;
    body->addWidget(settingsPages_);
    body->addWidget(stack, 1);
    outer->addLayout(body, 1);
    connect(settingsPages_, &QListWidget::currentRowChanged, stack, &QStackedWidget::setCurrentIndex);
    // One scrollable page per list entry: a large title, then rows stacked 2 px apart.
    auto page = [&](char16_t glyph, const QString& name) {
        auto* item = new QListWidgetItem(Look::icon(glyph, settingsPages_->palette()), name);
        item->setData(Qt::UserRole, int(glyph));
        item->setSizeHint(QSize(0, 38));
        settingsPages_->addItem(item);
        auto* scroll = new QScrollArea;
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        auto* content = new QWidget;
        auto* column = new QVBoxLayout(content);
        column->setContentsMargins(28, 8, 28, 20);
        column->setSpacing(2);
        auto* heading = new QLabel(name);
        heading->setFont(weighted(font(), 26, QFont::DemiBold));
        column->addWidget(heading);
        column->addSpacing(14);
        scroll->setWidget(content);
        stack->addWidget(scroll);
        return column;
    };
    auto row = [](QVBoxLayout* column, const QString& title, const QString& description, QWidget* control) {
        auto* line = new SettingRow(0, title, description, control);
        column->addWidget(line);
        return line;
    };
    auto group = [this](QVBoxLayout* column, const QString& title) {
        column->addSpacing(18);
        auto* label = new QLabel(title);
        label->setFont(weighted(font(), 14, QFont::DemiBold));
        column->addWidget(label);
        column->addSpacing(6);
    };
    auto fieldWithButton = [](QLineEdit* field, QPushButton* button, int width) {
        auto* box = new QWidget;
        auto* line = new QHBoxLayout(box);
        line->setContentsMargins(0, 0, 0, 0);
        field->setMinimumWidth(width);
        line->addWidget(field);
        line->addWidget(button);
        return box;
    };

    auto* general = page(Look::System, QStringLiteral("일반"));
    auto* closeToTray = new ToggleSwitch;
    auto* startup = new ToggleSwitch;
    row(general, QStringLiteral("닫기 버튼을 누르면 트레이로"), QStringLiteral("앱은 계속 실행되고, 단축키와 자동 번역도 그대로 동작해요"), closeToTray);
    row(general, QStringLiteral("Windows를 시작할 때 실행"), QStringLiteral("로그인하면 트레이에서 자동 번역 모드로 시작해요"), startup);
    general->addStretch(1);
    connect(closeToTray, &QCheckBox::toggled, this, [](bool on) { QSettings().setValue("ui/closeToTray", on); });
    connect(startup, &QCheckBox::toggled, this, [](bool on) {
        QSettings run(QString::fromLatin1(RunKey), QSettings::NativeFormat);
        if (on) run.setValue("SubtitleTranslator", startupCommand());
        else run.remove("SubtitleTranslator");
    });

    auto* deepl = page(Look::Globe, QStringLiteral("번역"));
    auto* key = new QLineEdit;
    key->setEchoMode(QLineEdit::Password);
    key->setPlaceholderText(QStringLiteral("DeepL API 키"));
    key->setMaxLength(512);
    auto* checkKey = new QPushButton(QStringLiteral("확인"));
    checkKey->setToolTip(QStringLiteral("키만 DeepL에 보내 이번 달 사용량을 확인해요. 번역 글자 수는 줄지 않아요."));
    const QString keyHint = QStringLiteral("끝이 :fx면 Free 키예요.");
    SettingRow* keyRow = row(deepl, QStringLiteral("API 키"), keyHint, fieldWithButton(key, checkKey, 280));
    auto* plan = new QComboBox;
    plan->addItem(QStringLiteral("DeepL API Free"));
    plan->addItem(QStringLiteral("DeepL API Pro"));
    plan->setToolTip(QStringLiteral("Free는 api-free.deepl.com, Pro는 api.deepl.com으로 보내요"));
    row(deepl, QStringLiteral("요금제"), QString(), plan);
    auto* remember = new ToggleSwitch;
    row(deepl, QStringLiteral("키 기억하기"), QStringLiteral("Windows 자격 증명 관리자에 저장해요. 끄면 앱을 닫을 때 지워져요."), remember);
    auto* consent = new ToggleSwitch;
    row(deepl, QStringLiteral("DeepL로 글자 보내기"), QStringLiteral("번역할 때 인식한 글자가 DeepL로 가요. 요금제에 따라 비용이 생길 수 있어요."), consent);
    // A link, not a button: it only shows more text.
    auto* privacyToggle = new Note(QStringLiteral("<a href='more'>무엇이 전송되나요?</a>"));
    privacyToggle->setContentsMargins(6, 0, 0, 0);
    auto* privacy = new Note(QStringLiteral(
        "화면 이미지는 이 PC 메모리에서만 읽고, 파일로 저장하거나 서버로 보내지 않아요.\n"
        "번역할 때 인식한 원문 글자와 API 키가 HTTPS로 DeepL에 전송돼요.\n"
        "자동 번역 중에는 대사가 바뀔 때마다 새 원문을 보내요. 바로 앞과 같은 원문은 다시 보내지 않아요.\n"
        "요금제와 사용량에 따라 비용이 생기거나 무료 한도가 줄 수 있어요.\n"
        "동의하지 않거나 키가 없으면 OCR까지만 하고 아무것도 보내지 않아요.\n"
        "번역 요청을 취소해도 서버가 이미 처리한 양은 청구될 수 있어요."));
    privacy->setContentsMargins(16, 6, 16, 6);
    privacy->setVisible(false);
    deepl->addSpacing(6);
    deepl->addWidget(privacyToggle, 0, Qt::AlignLeft);
    deepl->addWidget(privacy);
    auto* links = new Note(QStringLiteral("<a href='https://www.deepl.com/your-account/keys'>DeepL API 키 발급</a>"));
    links->setOpenExternalLinks(true);
    links->setContentsMargins(6, 4, 0, 0);
    deepl->addWidget(links);
    deepl->addStretch(1);
    connect(privacyToggle, &QLabel::linkActivated, privacy, [privacy] { privacy->setVisible(!privacy->isVisible()); });
    auto* keyChecker = new TranslationService(settingsWindow_);
    // DeepL API Free keys end with ":fx"; a mismatched endpoint fails with 403.
    connect(key, &QLineEdit::textEdited, settingsWindow_, [plan, keyRow](const QString& text) {
        if (text.trimmed().isEmpty()) return;
        const int wanted = TranslationService::isFreeKey(text) ? 0 : 1;
        if (plan->currentIndex() == wanted) return;
        plan->setCurrentIndex(wanted);
        keyRow->setDescription(wanted == 0 ? QStringLiteral("끝이 :fx라서 요금제를 Free로 바꿨어요.")
            : QStringLiteral("끝에 :fx가 없어 요금제를 Pro로 바꿨어요. Free 키라면 키 전체를 다시 복사해 주세요."));
    });
    connect(checkKey, &QPushButton::clicked, settingsWindow_, [key, plan, checkKey, keyRow, keyChecker] {
        if (key->text().trimmed().isEmpty()) { keyRow->setDescription(QStringLiteral("먼저 API 키를 넣어 주세요.")); return; }
        checkKey->setEnabled(false);
        keyRow->setDescription(QStringLiteral("DeepL에서 확인하는 중…"));
        keyChecker->checkUsage(key->text(), plan->currentIndex() == 0);
    });
    connect(keyChecker, &TranslationService::usageChecked, settingsWindow_, [checkKey, keyRow](qint64 used, qint64 limit) {
        checkKey->setEnabled(true);
        const QLocale locale;
        keyRow->setDescription(limit > 0
            ? QStringLiteral("사용할 수 있는 키예요. 이번 달 %1자 / %2자 사용, %3자 남았어요.")
                .arg(locale.toString(used), locale.toString(limit), locale.toString(qMax<qint64>(0, limit - used)))
            : QStringLiteral("사용할 수 있는 키예요. 이번 달 %1자 사용했어요.").arg(locale.toString(used)));
    });
    connect(keyChecker, &TranslationService::failed, settingsWindow_, [checkKey, keyRow](const QString& error) {
        checkKey->setEnabled(true);
        keyRow->setDescription(QStringLiteral("확인하지 못했어요. ") + error);
    });

    auto* recognition = page(Look::Scan, QStringLiteral("인식"));
    auto* engine = new QComboBox;
    engine->addItem(QStringLiteral("PaddleOCR (권장)"), true);
    engine->addItem(QStringLiteral("Tesseract"), false);
    row(recognition, QStringLiteral("OCR 엔진"), QStringLiteral("게임 글씨는 PaddleOCR이 더 정확해요. 모두 이 PC 안에서 읽어요."), engine);
    auto* mode = new QComboBox;
    mode->addItem(QStringLiteral("자동"), 0);
    mode->addItem(QStringLiteral("여러 줄"), 6);
    mode->addItem(QStringLiteral("한 줄"), 7);
    row(recognition, QStringLiteral("Tesseract 줄 배치"), QStringLiteral("Tesseract로 읽을 때만 써요"), mode);
    auto* exe = new QLineEdit;
    auto* tessdataEdit = new QLineEdit;
    auto browse = [this](QLineEdit* edit, bool directory) {
        auto* button = new QPushButton(QStringLiteral("찾아보기"));
        connect(button, &QPushButton::clicked, settingsWindow_, [this, edit, directory] {
            const QString path = directory
                ? QFileDialog::getExistingDirectory(settingsWindow_, QStringLiteral("tessdata 폴더"), edit->text())
                : QFileDialog::getOpenFileName(settingsWindow_, QStringLiteral("Tesseract 실행 파일"), edit->text(), "Executable (*.exe)");
            if (!path.isEmpty()) edit->setText(path);
        });
        return button;
    };
    row(recognition, QStringLiteral("Tesseract 실행 파일"), QString(), fieldWithButton(exe, browse(exe, false), 300));
    row(recognition, QStringLiteral("tessdata 폴더"), QString(), fieldWithButton(tessdataEdit, browse(tessdataEdit, true), 300));
    auto* probe = new QPushButton(QStringLiteral("확인"));
    SettingRow* probeRow = row(recognition, QStringLiteral("설치 확인"), QString(), probe);
    auto* tesseractLink = new Note(QStringLiteral("<a href='https://tesseract-ocr.github.io/tessdoc/Installation.html'>Tesseract 설치 안내</a>"));
    tesseractLink->setOpenExternalLinks(true);
    tesseractLink->setContentsMargins(6, 8, 0, 0);
    recognition->addWidget(tesseractLink);
    recognition->addStretch(1);
    auto* checker = new OcrService(settingsWindow_);
    connect(probe, &QPushButton::clicked, settingsWindow_, [this, engine, exe, tessdataEdit, probe, probeRow, checker] {
        if (engine->currentData().toBool() && ocrLanguages() != QLatin1String("vie")) {
            probeRow->setDescription(PaddleOcr::available(paddleDir_, ocrLanguages())
                ? QStringLiteral("고른 언어의 PaddleOCR 모델이 있어요.")
                : QStringLiteral("고른 언어의 PaddleOCR 모델이 없어요. 앱 폴더의 ocr\\paddle을 확인해 주세요."));
            return;
        }
        if (ocrLanguages() == QLatin1String("multi")) {
            probeRow->setDescription(QStringLiteral("다국어 자동 감지는 PaddleOCR에서만 돼요. 게임 언어를 하나 골라 주세요."));
            return;
        }
        probe->setEnabled(false);
        probeRow->setDescription(QStringLiteral("언어 모델을 확인하는 중…"));
        checker->probe(exe->text().trimmed(), tessdataEdit->text().trimmed(), ocrLanguages());
    });
    connect(checker, &OcrService::probeFinished, settingsWindow_, [probe, probeRow](bool, const QString& message) {
        probeRow->setDescription(message); probe->setEnabled(true);
    });
    connect(checker, &OcrService::failed, settingsWindow_, [probe, probeRow](const QString& message) {
        probeRow->setDescription(message); probe->setEnabled(true);
    });

    auto* captions = page(Look::Captions, QStringLiteral("자막 표시"));
    auto* sample = new CaptionStage;
    sample->setContent(QString::fromUtf8(SampleSource), QString::fromUtf8(SampleCaption));
    sample->setStyle(QSettings().value("display/captionScale", 100).toInt() / 100.0, QSettings().value("display/captionBox", false).toBool());
    captions->addWidget(sample);
    captions->addSpacing(12);
    row(captions, QStringLiteral("원문 아래에 자막 띄우기"), QStringLiteral("자리가 없으면 원문 위에, 그래도 없으면 결과 창에 보여요"), inlineCaptions_);
    auto* size = new QComboBox;
    size->addItem(QStringLiteral("작게"), 80);
    size->addItem(QStringLiteral("원문과 같게"), 100);
    size->addItem(QStringLiteral("크게"), 125);
    size->addItem(QStringLiteral("아주 크게"), 150);
    size->setCurrentIndex(qMax(0, size->findData(QSettings().value("display/captionScale", 100).toInt())));
    row(captions, QStringLiteral("글자 크기"), QString(), size);
    auto* background = new QComboBox;
    background->addItem(QStringLiteral("테두리만"), false);
    background->addItem(QStringLiteral("반투명 상자"), true);
    background->setCurrentIndex(QSettings().value("display/captionBox", false).toBool() ? 1 : 0);
    row(captions, QStringLiteral("배경"), QStringLiteral("밝은 장면에서 글자가 잘 안 보이면 반투명 상자를 써 보세요"), background);
    row(captions, QStringLiteral("위치 미리보기"), QStringLiteral("고른 영역 아래에 예시 자막을 잠깐 띄워요"), captionPreview_);
    row(captions, QStringLiteral("읽은 범위 표시"), QStringLiteral("캡처한 사각형을 잠깐 테두리로 보여 줘요"), showCaptureRange_);
    captions->addStretch(1);
    auto applyLook = [this, size, background, sample] {
        const int scale = size->currentData().toInt();
        const bool box = background->currentData().toBool();
        QSettings().setValue("display/captionScale", scale);
        QSettings().setValue("display/captionBox", box);
        subtitleOverlay_->setStyle(scale / 100.0, box);
        stage_->setStyle(scale / 100.0, box);
        sample->setStyle(scale / 100.0, box);
        if (subtitleOverlay_->isVisible() && !result_->toPlainText().isEmpty()) showSubtitle(result_->toPlainText());
    };
    connect(size, &QComboBox::currentIndexChanged, this, applyLook);
    connect(background, &QComboBox::currentIndexChanged, this, applyLook);

    auto* capture = page(Look::Camera, QStringLiteral("캡처"));
    row(capture, QStringLiteral("게임 창 따라가기"), QStringLiteral("창을 옮기거나 크기를 바꿔도 같은 자리를 읽고, 게임마다 영역을 따로 기억해요"), followGame_);
    row(capture, QStringLiteral("영역 선택 버튼이 쓸 모니터"), QString(), screens_);
    row(capture, QStringLiteral("마우스 주변 범위"), QStringLiteral("마우스 주변 번역과 마우스 올려두기가 읽는 크기예요"), cursorSizeBox_);
    auto* fullscreen = new Note(QStringLiteral("전체 화면 독점 모드는 캡처되지 않을 수 있어요. 게임을 창 모드나 테두리 없는 창 모드로 실행해 주세요."));
    fullscreen->setContentsMargins(6, 10, 0, 0);
    capture->addWidget(fullscreen);
    capture->addStretch(1);

    auto* shortcuts = page(Look::Keyboard, QStringLiteral("단축키"));
    QList<QKeySequenceEdit*> keyEdits;
    QList<SettingRow*> keyRows;
    for (const auto& spec : hotkeySpecs) {
        auto* edit = new QKeySequenceEdit;
        edit->setMaximumSequenceLength(1);
        edit->setClearButtonEnabled(true);
        edit->setMinimumWidth(200);
        keyEdits << edit;
        keyRows << row(shortcuts, QString::fromUtf8(spec.label), QString(), edit);
    }
    auto* defaults = new QPushButton(QStringLiteral("기본값으로 되돌리기"));
    shortcuts->addSpacing(10);
    shortcuts->addWidget(defaults, 0, Qt::AlignLeft);
    auto* keyRule = new Note(QStringLiteral("글자나 숫자 키는 Ctrl이나 Alt와 함께 눌러야 해요. 게임 입력과 겹치지 않게 하려는 거예요. F1~F24는 혼자 써도 돼요."));
    keyRule->setContentsMargins(6, 8, 0, 0);
    shortcuts->addWidget(keyRule);
    shortcuts->addStretch(1);
    connect(defaults, &QPushButton::clicked, settingsWindow_, [keyEdits] {
        for (int i = 0; i < keyEdits.size(); ++i) keyEdits[i]->setKeySequence(defaultSequence(hotkeySpecs[i]));
    });

    auto* about = page(Look::Info, QStringLiteral("정보"));
    auto* version = new QLabel(QStringLiteral("자막 번역 %1").arg(QCoreApplication::applicationVersion()));
    version->setFont(weighted(font(), 14, QFont::DemiBold));
    about->addWidget(version);
    auto* aboutText = new Note(QStringLiteral(
        "게임 화면의 글자를 이 PC에서 읽고, 외국어 부분만 DeepL로 번역해요.\n"
        "화면 이미지는 저장하지도, 어디로 보내지도 않아요.\n\n"
        "처음이라면 게임 언어를 고르고, 영역 선택으로 대사창을 드래그한 다음 자동 번역을 켜 보세요."));
    aboutText->setContentsMargins(0, 6, 0, 0);
    about->addWidget(aboutText);
    about->addStretch(1);

    auto* bottom = new QHBoxLayout;
    bottom->setContentsMargins(24, 12, 20, 16);
    bottom->addWidget(new Note(QStringLiteral("API 키, OCR 엔진, 단축키는 저장해야 적용돼요. 나머지는 바로 적용돼요.")), 1);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Close);
    buttons->button(QDialogButtonBox::Save)->setText(QStringLiteral("저장"));
    buttons->button(QDialogButtonBox::Save)->setDefault(true);
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
        keyRow->setDescription(keyHint);
        remember->setChecked(remember_);
        consent->setChecked(consent_);
        engine->setCurrentIndex(paddleEngine_ ? 0 : 1);
        mode->setCurrentIndex(qMax(0, mode->findData(psm_)));
        exe->setText(executable_);
        tessdataEdit->setText(tessdata_);
        probeRow->setDescription(ocrLanguages() == QLatin1String("vie")
            ? QStringLiteral("베트남어는 성조를 읽는 Tesseract vie 모델로 읽어요.")
            : PaddleOcr::available(paddleDir_, ocrLanguages())
            ? QStringLiteral("PaddleOCR 모델이 앱에 들어 있어요. 키가 없어도 OCR은 쓸 수 있어요.")
            : QStringLiteral("PaddleOCR 모델이 없어 Tesseract로 읽어요. 고른 언어의 traineddata가 필요해요."));
        const bool states[] = {regionRegistered_, autoRegistered_, registered_, cursorRegistered_, hoverRegistered_};
        for (int i = 0; i < keyEdits.size(); ++i) {
            keyEdits[i]->setKeySequence(storedSequence(hotkeySpecs[i]));
            keyRows[i]->setDescription(states[i] ? QString() : QStringLiteral("다른 앱이 이 키를 쓰고 있어요. 다른 키를 골라 주세요."));
        }
    };
    saveSettings_ = [=]() -> bool {
        const QString enteredKey = key->text().trimmed();
        if (enteredKey.contains('\r') || enteredKey.contains('\n')) {
            settingsPages_->setCurrentRow(1);
            QMessageBox::warning(settingsWindow_, QStringLiteral("API 키"), QStringLiteral("API 키에 줄바꿈이 들어 있어요. 키만 다시 붙여 넣어 주세요."));
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
                    ? QStringLiteral("%1에 다른 동작과 같은 키를 골랐어요.").arg(QString::fromUtf8(hotkeySpecs[i].label))
                    : QStringLiteral("%1: 글자나 숫자 키는 Ctrl이나 Alt와 함께 눌러 주세요. F1~F24는 혼자 써도 돼요.").arg(QString::fromUtf8(hotkeySpecs[i].label)));
                return false;
            }
            chosen << sequence;
        }
        QString error;
        if (remember->isChecked() && !enteredKey.isEmpty()) {
            if (!Credentials::save(enteredKey, error)) { QMessageBox::warning(settingsWindow_, QStringLiteral("키 저장"), error); return false; }
        } else if (!Credentials::remove(error)) {
            QMessageBox::warning(settingsWindow_, QStringLiteral("키 삭제"), error); return false;
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
            QMessageBox::warning(settingsWindow_, QStringLiteral("설정"), QStringLiteral("Windows 사용자 설정에 저장하지 못했어요."));
            return false;
        }
        registerHotkeys();
        reloadSettings_();
        const bool states[] = {regionRegistered_, autoRegistered_, registered_, cursorRegistered_, hoverRegistered_};
        if (std::find(std::begin(states), std::end(states), false) != std::end(states)) {
            settingsPages_->setCurrentRow(5);
            QMessageBox::information(settingsWindow_, QStringLiteral("단축키"),
                QStringLiteral("설정은 저장했어요. 다른 앱이 쓰고 있는 단축키가 있어요. 표시된 줄의 키를 바꿔 주세요."));
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
    menu->addAction(QStringLiteral("열기"), this, &MainWindow::showFromTray);
    menu->addSeparator();
    trayAuto_ = menu->addAction(QStringLiteral("자동 번역"));
    trayAuto_->setCheckable(true);
    trayHover_ = menu->addAction(QStringLiteral("마우스 올려두기"));
    trayHover_->setCheckable(true);
    menu->addSeparator();
    menu->addAction(QStringLiteral("영역 선택"), this, [this] { chooseRegionUnderCursor(); });
    menu->addAction(QStringLiteral("지금 번역"), this, &MainWindow::startCapture);
    menu->addSeparator();
    menu->addAction(QStringLiteral("설정"), this, [this] { showFromTray(); settings(); });
    menu->addAction(QStringLiteral("끝내기"), this, &MainWindow::quitApp);
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
