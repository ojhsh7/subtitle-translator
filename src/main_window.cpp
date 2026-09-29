#include "main_window.h"
#include "capture_service.h"
#include "credential_store.h"
#include "languages.h"
#include "paddle_ocr.h"
#include "region_selector.h"
#include "subtitle/text.h"
#include "subtitle_overlay.h"
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
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
#include <QStandardPaths>
#include <QTimer>
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
}

MainWindow::MainWindow() {
    setWindowTitle(QStringLiteral("자막 번역"));
    resize(720, 760);
    setMinimumSize(600, 620);
    setAttribute(Qt::WA_ShowWithoutActivating);
    auto* central = new QWidget(this);
    setCentralWidget(central);
    auto* layout = new QVBoxLayout(central);
    layout->setContentsMargins(24, 20, 24, 20);
    layout->setSpacing(12);
    auto* title = new QLabel(QStringLiteral("게임 자막 번역"));
    title->setStyleSheet("font-size:24px; font-weight:600;");
    layout->addWidget(title);
    auto* intro = new QLabel(QStringLiteral("설정 → 자막 영역 선택 → 자동 번역 켜기 (Ctrl + Alt + P)\n"
        "대사가 바뀔 때마다 바로 아래에 번역이 나옵니다. 한 번만 번역하려면 Ctrl + Alt + T."));
    intro->setWordWrap(true);
    layout->addWidget(intro);
    auto* row = new QHBoxLayout;
    screens_ = new QComboBox;
    settings_ = new QPushButton(QStringLiteral("설정"));
    row->addWidget(new QLabel(QStringLiteral("모니터")));
    row->addWidget(screens_, 1);
    row->addWidget(settings_);
    layout->addLayout(row);
    auto* languageRow = new QHBoxLayout;
    ocrLanguageBox_ = new QComboBox;
    for (const auto& option : Languages::Ocr) ocrLanguageBox_->addItem(QString::fromUtf8(option.label), QString::fromLatin1(option.code));
    targetLanguageBox_ = new QComboBox;
    for (const auto& option : Languages::Target) targetLanguageBox_->addItem(QString::fromUtf8(option.label), QString::fromLatin1(option.code));
    {
        const QSettings saved;
        const int ocrIndex = ocrLanguageBox_->findData(saved.value("ocr/languages", Languages::DefaultOcr).toString());
        ocrLanguageBox_->setCurrentIndex(qMax(0, ocrIndex));
        const int targetIndex = targetLanguageBox_->findData(saved.value("translation/target", Languages::DefaultTarget).toString());
        targetLanguageBox_->setCurrentIndex(qMax(0, targetIndex));
    }
    languageRow->addWidget(new QLabel(QStringLiteral("원문 언어")));
    languageRow->addWidget(ocrLanguageBox_, 3);
    languageRow->addWidget(new QLabel(QStringLiteral("→ 번역 언어")));
    languageRow->addWidget(targetLanguageBox_, 2);
    layout->addLayout(languageRow);
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
    autoTimer_ = new QTimer(this);
    autoTimer_->setInterval(400);
    autoClock_.start();
    auto* imageActions = new QHBoxLayout;
    import_ = new QPushButton(QStringLiteral("이미지 파일 선택"));
    retry_ = new QPushButton(QStringLiteral("최근 이미지 재시도"));
    retry_->setEnabled(false);
    imageActions->addWidget(import_); imageActions->addWidget(retry_);
    layout->addLayout(imageActions);
    connect(import_, &QPushButton::clicked, this, &MainWindow::importImage);
    connect(retry_, &QPushButton::clicked, this, [this] {
        if (!busy_ && !lastImage_.isNull()) { resetOutput(); processImage(lastImage_, lastSource_); }
    });
    regionLabel_ = new QLabel(QStringLiteral("선택한 영역이 없습니다."));
    layout->addWidget(regionLabel_);
    subtitleOverlay_ = new SubtitleOverlay;
    auto* captionRow = new QHBoxLayout;
    inlineCaptions_ = new QCheckBox(QStringLiteral("원문 아래에 번역 글자만 표시"));
    inlineCaptions_->setChecked(QSettings().value("display/inlineCaptions", true).toBool());
    captionPreview_ = new QPushButton(QStringLiteral("위치 미리보기"));
    captionRow->addWidget(inlineCaptions_, 1); captionRow->addWidget(captionPreview_);
    layout->addLayout(captionRow);
    connect(captionPreview_, &QPushButton::clicked, this, &MainWindow::previewSubtitle);
    connect(inlineCaptions_, &QCheckBox::toggled, this, [this](bool enabled) {
        QSettings().setValue("display/inlineCaptions", enabled);
        if (!enabled) { subtitleOverlay_->hide(); restoreOverlay_ = false; }
        else if (!busy_ && lastSource_ == ST_SCREEN_CAPTURE && !result_->toPlainText().isEmpty())
            showSubtitle(result_->toPlainText());
    });
    status_ = new QLabel(QStringLiteral("준비 중"));
    status_->setWordWrap(true);
    status_->setStyleSheet("background:#edf4fa; color:#152c47; padding:12px; border-radius:6px;");
    layout->addWidget(status_);
    layout->addWidget(new QLabel(QStringLiteral("최근 캡처 · 이 이미지는 외부로 전송하지 않습니다")));
    preview_ = new QLabel(QStringLiteral("영역을 선택하고 캡처하면 이미지가 표시됩니다."));
    preview_->setAlignment(Qt::AlignCenter);
    preview_->setMinimumHeight(110);
    preview_->setMaximumHeight(150);
    preview_->setStyleSheet("background:#202733; color:#d7dee9; border-radius:6px;");
    layout->addWidget(preview_);
    layout->addWidget(new QLabel(QStringLiteral("OCR 원문")));
    original_ = new QPlainTextEdit;
    original_->setReadOnly(true);
    original_->setPlaceholderText(QStringLiteral("실제 OCR 결과가 여기에 표시됩니다."));
    layout->addWidget(original_, 1);
    auto* resultButton = new QPushButton(QStringLiteral("번역 결과 창 열기"));
    layout->addWidget(resultButton);
    auto* note = new QLabel(QStringLiteral("자막 글자 부분만 선택하면 바로 아래에 번역 글자가 표시됩니다. 선택한 영역은 다음 실행에도 사용합니다.\n"
        "자동 번역은 0.4초마다 자막 영역만 확인하고, 대사 출력이 멈추면 한 번 번역합니다. 창 모드·테두리 없는 창 모드를 권장합니다."));
    note->setWordWrap(true);
    layout->addWidget(note);
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
    connect(resultButton, &QPushButton::clicked, this, &MainWindow::showResult);
    connect(settings_, &QPushButton::clicked, this, &MainWindow::settings);
    connect(select_, &QPushButton::clicked, this, &MainWindow::chooseRegion);
    connect(capture_, &QPushButton::clicked, this, &MainWindow::startCapture);
    connect(auto_, &QPushButton::toggled, this, &MainWindow::setAutoTranslate);
    // A new language changes what the current line means: forget the cache and read it again.
    connect(ocrLanguageBox_, &QComboBox::currentIndexChanged, this, [this] {
        QSettings().setValue("ocr/languages", ocrLanguages());
        st_reset_cache(session_); detector_.reset();
    });
    connect(targetLanguageBox_, &QComboBox::currentIndexChanged, this, [this] {
        QSettings().setValue("translation/target", targetLanguage());
        st_reset_cache(session_); detector_.reset();
    });
    connect(autoTimer_, &QTimer::timeout, this, &MainWindow::autoTick);
    connect(cancel_, &QPushButton::clicked, this, &MainWindow::cancel);
    connect(qApp, &QGuiApplication::screenAdded, this, [this] { refreshScreens(); });
    connect(qApp, &QGuiApplication::screenRemoved, this, [this] {
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
                status_->setText(QStringLiteral("자동 번역 중 · 자막이 없어 다음 대사를 기다립니다."));
                return;
            }
            resultStatus_->setText(QStringLiteral("OCR 결과를 번역할 수 없습니다."));
            finish(decision == ST_EMPTY ? QStringLiteral("인식한 글자가 없습니다.")
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
        QStringList pieces;
        unsigned scripts = 0;
        for (const auto& segment : segments_)
            if (segment.translate) { pieces << QString::fromStdString(segment.utf8); scripts |= subtitle::scriptsIn(segment.utf8); }
        if (pieces.isEmpty()) {
            st_finish_ocr(session_, request_);
            resultStatus_->setText(QStringLiteral("원문이 이미 %1입니다 · 번역하지 않았습니다.").arg(target));
            if (autoTranslating() && activeSource_ == ST_SCREEN_CAPTURE) {
                subtitleOverlay_->hide(); setBusy(false);
                status_->setText(QStringLiteral("자동 번역 중 · 원문이 이미 %1라 번역하지 않았습니다.").arg(target));
                return;
            }
            finish(QStringLiteral("원문이 이미 %1라 번역 요청을 보내지 않았습니다.").arg(target));
            return;
        }
        if (apiKey_.isEmpty() || !consent_) {
            st_finish_ocr(session_, request_);
            resultStatus_->setText(QStringLiteral("OCR 완료 · 번역 미실행. 설정에서 API 키와 전송 동의를 확인하세요."));
            finish(QStringLiteral("OCR 완료. API 키·전송 동의가 없어 번역 요청은 보내지 않았습니다."));
            return;
        }
        status_->setText(QStringLiteral("DeepL에서 %1(으)로 번역 중…").arg(target));
        resultStatus_->setText(QStringLiteral("번역 중…"));
        translation_.translate(pieces, apiKey_, freeApi_, targetLanguage(), Languages::sourceForScripts(scripts, ocrLanguages()));
    });
    connect(&ocr_, &OcrService::failed, this, [this](const QString& error) {
        if (!busy_) return;
        st_fail(session_, request_);
        resultStatus_->setText(QStringLiteral("OCR 실패 · 번역 없음"));
        finish(error);
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
        const QString text = QString::fromStdString(joined);
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
                                 : QStringLiteral("번역 완료. 다음 자막에서 Ctrl + Alt + T를 누르세요."));
    });
    connect(&translation_, &TranslationService::failed, this, [this](const QString& error) {
        if (!busy_) return;
        st_fail(session_, request_);
        resultStatus_->setText(QStringLiteral("번역 실패 · 원문은 앱에서 확인할 수 있습니다."));
        finish(error);
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
    const HWND handle = reinterpret_cast<HWND>(winId());
    registered_ = RegisterHotKey(handle, 1, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 'T') != 0;
    autoRegistered_ = RegisterHotKey(handle, 2, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 'P') != 0;
    QString ready = registered_ ? QStringLiteral("준비됨 · 먼저 설정을 확인하고 자막 영역을 선택하세요.")
        : QStringLiteral("Ctrl+Alt+T 등록 실패: 다른 앱에서 사용 중일 수 있습니다. 캡처 버튼을 사용하세요.");
    if (!autoRegistered_) ready += QStringLiteral("\nCtrl+Alt+P 등록 실패: 자동 번역 버튼을 사용하세요.");
    status_->setText(ready);
    const bool setupComplete = saved.value("setupComplete", false).toBool();
    // The desktop shortcut passes --auto: the user chose to start translating right away.
    if (QCoreApplication::arguments().contains("--auto")) {
        if (setupComplete && !region_.isEmpty()) setAutoTranslate(true);
        else {
            autoPending_ = true;
            status_->setText(QStringLiteral("설정을 마치고 자막 영역을 선택하면 자동 번역이 바로 시작됩니다."));
        }
    }
    if (!credentialError.isEmpty()) status_->setText(credentialError);
    if (!setupComplete &&
        !QCoreApplication::arguments().contains("--smoke-test") && !qApp->property("testMode").toBool())
        QTimer::singleShot(0, this, &MainWindow::settings);
}
MainWindow::~MainWindow() {
    autoTimer_->stop();
    cancel();
    if (registered_) UnregisterHotKey(reinterpret_cast<HWND>(winId()), 1);
    if (autoRegistered_) UnregisterHotKey(reinterpret_cast<HWND>(winId()), 2);
    delete subtitleOverlay_;
    delete resultWindow_;
    st_destroy(session_);
}
void MainWindow::refreshScreens() {
    screens_->clear();
    const auto list = QGuiApplication::screens();
    for (int i = 0; i < list.size(); ++i)
        screens_->addItem(QStringLiteral("%1 · %2 (%3 × %4)").arg(i + 1).arg(list[i]->name())
            .arg(list[i]->size().width()).arg(list[i]->size().height()));
}
void MainWindow::settings() {
    if (busy_ || modal_) return;
    modal_ = true;
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("최초 설정 · OCR와 번역"));
    dialog.resize(700, 700);
    auto* layout = new QVBoxLayout(&dialog);
    auto* heading = new QLabel(QStringLiteral("1. OCR 준비 · 원문 언어: %1").arg(ocrLanguageBox_->currentText()));
    layout->addWidget(heading);
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
    form->addRow(QStringLiteral("tesseract.exe"), pathRow(exe, false));
    form->addRow(QStringLiteral("tessdata 폴더"), pathRow(tessdataEdit, true));
    auto* engine = new QComboBox;
    engine->addItem(QStringLiteral("PaddleOCR · 게임 자막용 (권장)"), true);
    engine->addItem(QStringLiteral("Tesseract · 기존 방식"), false);
    engine->setCurrentIndex(paddleEngine_ ? 0 : 1);
    form->addRow(QStringLiteral("OCR 엔진"), engine);
    auto* mode = new QComboBox;
    mode->addItem(QStringLiteral("자동 · 선택 영역 높이로 판단"), 0);
    mode->addItem(QStringLiteral("여러 줄 자막 (PSM 6)"), 6);
    mode->addItem(QStringLiteral("한 줄 자막 (PSM 7)"), 7);
    mode->setCurrentIndex(qMax(0, mode->findData(psm_)));
    form->addRow(QStringLiteral("자막 배치"), mode);
    layout->addLayout(form);
    auto* probe = new QPushButton(QStringLiteral("OCR 설치·언어 모델 확인"));
    auto* probeStatus = new QLabel(ocrLanguages() == QLatin1String("vie")
        ? QStringLiteral("베트남어는 성조 문자를 읽는 Tesseract vie 모델을 사용합니다. OCR은 로컬에서 처리합니다.")
        : PaddleOcr::available(paddleDir_, ocrLanguages())
        ? QStringLiteral("PaddleOCR 모델이 앱에 들어 있습니다. PaddleOCR이 읽지 못하면 Tesseract가 대신 읽습니다. 키 없이 OCR만 사용할 수 있습니다.")
        : QStringLiteral("PaddleOCR 모델이 없어 Tesseract로 읽습니다. Tesseract 5와 선택한 원문 언어의 traineddata가 필요합니다."));
    probeStatus->setWordWrap(true);
    layout->addWidget(probe); layout->addWidget(probeStatus);
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
    layout->addSpacing(12);
    layout->addWidget(new QLabel(QStringLiteral("2. DeepL API 연결 (선택)")));
    auto* apiForm = new QFormLayout;
    auto* plan = new QComboBox;
    plan->addItem(QStringLiteral("DeepL API Free · api-free.deepl.com"));
    plan->addItem(QStringLiteral("DeepL API Pro · api.deepl.com"));
    plan->setCurrentIndex(freeApi_ ? 0 : 1);
    auto* key = new QLineEdit(apiKey_);
    key->setEchoMode(QLineEdit::Password);
    key->setPlaceholderText(QStringLiteral("API 키 입력 · 채팅이나 소스 코드에 적지 마세요"));
    key->setMaxLength(512);
    apiForm->addRow(QStringLiteral("API 요금제"), plan);
    apiForm->addRow(QStringLiteral("API 키"), key);
    layout->addLayout(apiForm);
    auto* checkKey = new QPushButton(QStringLiteral("키 확인"));
    auto* keyStatus = new QLabel(QStringLiteral("키 확인은 API 키만 DeepL에 보내 이번 달 사용량을 조회합니다. 번역 글자는 차감되지 않습니다."));
    keyStatus->setWordWrap(true);
    layout->addWidget(checkKey); layout->addWidget(keyStatus);
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
    layout->addWidget(remember);
    auto* privacy = new QLabel(QStringLiteral(
        "화면 이미지는 PC 메모리에서 OCR로 처리하며 파일로 저장하거나 서버에 보내지 않습니다.\n"
        "번역 시 인식한 원문 텍스트와 API 인증 정보가 HTTPS로 DeepL에 전송됩니다.\n"
        "자동 번역을 켜면 대사가 바뀔 때마다 새 원문이 전송됩니다. 바로 앞과 같은 원문은 다시 보내지 않습니다.\n"
        "요금제와 사용량에 따라 비용이 발생하거나 무료 한도가 소진될 수 있습니다. 계정의 한도를 확인하세요.\n"
        "동의하지 않거나 키가 없으면 로컬 OCR까지만 실행합니다. 저장을 선택하지 않은 키는 종료 시 사라집니다.\n"
        "번역 요청을 취소해도 이미 서버가 처리한 사용량은 청구될 수 있습니다."));
    privacy->setWordWrap(true);
    privacy->setStyleSheet("background:#edf4fa; color:#152c47; padding:12px;");
    layout->addWidget(privacy);
    auto* consent = new QCheckBox(QStringLiteral("인식 텍스트의 DeepL 전송과 비용 가능성을 확인했습니다"));
    consent->setChecked(consent_);
    layout->addWidget(consent);
    auto* links = new QLabel(QStringLiteral("<a href='https://www.deepl.com/your-account/keys'>DeepL API 키 발급·확인</a> · "
        "<a href='https://developers.deepl.com/docs/getting-started/auth'>DeepL API 안내</a> · "
        "<a href='https://tesseract-ocr.github.io/tessdoc/Installation.html'>Tesseract 설치 안내</a>"));
    links->setOpenExternalLinks(true);
    layout->addWidget(links);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Save)->setText(QStringLiteral("설정 저장"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("취소"));
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        const QString enteredKey = key->text().trimmed();
        if (enteredKey.contains('\r') || enteredKey.contains('\n')) {
            QMessageBox::warning(&dialog, QStringLiteral("키 확인"), QStringLiteral("API 키에 줄바꿈을 넣을 수 없습니다.")); return;
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
        saved.sync();
        if (saved.status() != QSettings::NoError) {
            QMessageBox::warning(&dialog, QStringLiteral("설정 저장 실패"), QStringLiteral("Windows 사용자 설정을 저장할 수 없습니다.")); return;
        }
        dialog.accept();
    });
    dialog.exec();
    checker.cancel();
    keyChecker.cancel();
    modal_ = false;
}
void MainWindow::chooseRegion() {
    if (busy_ || modal_) return;
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
            detector_.reset();
            status_->setText(QStringLiteral("영역 선택 완료. 자동 번역을 켜거나 Ctrl + Alt + T를 누르세요."));
        }
        modal_ = false;
        restoreWindows();
        if (selected && autoPending_) setAutoTranslate(true);
        else if (selected && inlineCaptions_->isChecked()) previewSubtitle();
    });
}
void MainWindow::startCapture() {
    if (busy_ || modal_) return;
    if (!selectedScreen_ || region_.isEmpty()) {
        status_->setText(QStringLiteral("먼저 자막 영역을 선택하세요.")); return;
    }
    if (selectedScreen_->geometry() != selectedGeometry_ || selectedScreen_->devicePixelRatio() != selectedDpr_) {
        subtitleOverlay_->hide();
        status_->setText(QStringLiteral("모니터 해상도나 배율이 바뀌었습니다. 영역을 다시 선택하세요.")); return;
    }
    if (!ocrReady()) {
        status_->setText(QStringLiteral("OCR 엔진이 없습니다. 설정에서 tesseract.exe 경로를 지정하거나 PaddleOCR 모델을 확인하세요.")); return;
    }
    setBusy(true);
    resetOutput();
    activeSource_ = ST_SCREEN_CAPTURE;
    resultStatus_->setText(QStringLiteral("새 자막 처리 중 · 아직 번역 결과가 없습니다."));
    status_->setText(QStringLiteral("선택 영역 캡처 중…"));
    const quint64 ticket = ++generation_;
    restoreMain_ = isVisible() && !isMinimized(); restoreResult_ = resultWindow_->isVisible();
    restoreOverlay_ = subtitleOverlay_->isVisible();
    if (!isMinimized()) hide();
    resultWindow_->hide(); subtitleOverlay_->hide();
    QTimer::singleShot(180, this, [this, ticket] {
        if (!busy_ || ticket != generation_) return;
        QString error;
        const auto image = Capture::region(selectedScreen_, region_, error);
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
}
void MainWindow::finish(const QString& status) {
    setBusy(false); status_->setText(status);
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
void MainWindow::showResult() { resultWindow_->show(); }
bool MainWindow::showSubtitle(const QString& text) {
    if (!inlineCaptions_->isChecked() || !selectedScreen_ || region_.isEmpty() ||
        selectedScreen_->geometry() != selectedGeometry_ ||
        selectedScreen_->devicePixelRatio() != selectedDpr_) {
        subtitleOverlay_->hide(); return false;
    }
    return subtitleOverlay_->showText(text, region_.translated(selectedGeometry_.topLeft()),
                                      selectedScreen_->availableGeometry());
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
    if (showSubtitle(QStringLiteral("번역 위치 미리보기 · 실제 번역 아님")))
        status_->setText(QStringLiteral("미리보기 글자가 번역 위치입니다. 앱을 최소화하고 Ctrl+Alt+T로 번역하세요."));
    else status_->setText(QStringLiteral("자막을 표시할 빈 공간이 없습니다. 원문 글자 부분만 영역으로 다시 선택하세요."));
}
bool MainWindow::nativeEvent(const QByteArray&, void* message, qintptr* result) {
    auto* msg = static_cast<MSG*>(message);
    if (msg->message == WM_HOTKEY && msg->wParam == 1) {
        startCapture(); *result = 0; return true;
    }
    if (msg->message == WM_HOTKEY && msg->wParam == 2) {
        setAutoTranslate(!autoTranslating()); *result = 0; return true;
    }
    return false;
}
void MainWindow::closeEvent(QCloseEvent* event) {
    autoTimer_->stop();
    cancel();
    resultWindow_->hide(); subtitleOverlay_->hide();
    event->accept();
    QCoreApplication::quit();
}

void MainWindow::resetOutput() {
    original_->clear(); result_->clear(); preview_->clear();
    subtitleOverlay_->hide(); restoreOverlay_ = false;
    resultStatus_->setText(QStringLiteral("새 이미지 처리 중 · 아직 번역 결과가 없습니다."));
}
void MainWindow::processImage(const QImage& image, int source) {
    activeSource_ = source;
    // Every image source enters here. The core sees only portable metadata and UTF-8 OCR.
    request_ = st_begin(session_, source, image.width(), image.height());
    if (!request_) { finish(QStringLiteral("이미지가 너무 크거나 이전 요청이 진행 중입니다.")); return; }
    lastImage_ = image; lastSource_ = source;
    setBusy(true);
    preview_->setPixmap(QPixmap::fromImage(image).scaled(preview_->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    status_->setText(QStringLiteral("PC에서 OCR 처리 중…"));
    ocr_.recognize(image, executable_, tessdata_, psm_, ocrLanguages());
}
void MainWindow::importImage() {
    if (busy_ || modal_) return;
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
    const QSignalBlocker blocker(auto_);
    detector_.reset();
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
    auto_->setChecked(true);
    autoTimer_->start();
    showMinimized(); // Keep the app window off the game's subtitle.
    status_->setText(QStringLiteral("자동 번역 중 · 대사가 바뀌면 번역합니다. Ctrl + Alt + P로 멈춥니다."));
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
    // Polling does not hide windows (that would flicker), so skip frames our own windows cover.
    if (coversRegion(region_.translated(selectedGeometry_.topLeft()))) {
        status_->setText(QStringLiteral("앱 창이 자막 영역을 가리고 있어 자동 번역을 잠시 건너뜁니다. 창을 옮기거나 최소화하세요."));
        return;
    }
    QString error;
    const QImage image = Capture::region(selectedScreen_, region_, error);
    if (image.isNull()) {
        setAutoTranslate(false);
        status_->setText(error);
        return;
    }
    if (detector_.feed(image, autoClock_.elapsed()) != ChangeDetector::Decision::Process) return;
    // Keep the previous caption on screen until the new line has a result.
    original_->clear(); result_->clear();
    processImage(image, ST_SCREEN_CAPTURE);
}
bool MainWindow::coversRegion(const QRect& global) const {
    return (isVisible() && !isMinimized() && frameGeometry().intersects(global)) ||
           (resultWindow_->isVisible() && resultWindow_->frameGeometry().intersects(global)) ||
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
