#include "first_run_wizard.h"
#include "languages.h"
#include "ocr_service.h"
#include "translation_service.h"
#include "widget_helpers.h"
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPushButton>
#include <QRadioButton>
#include <QVBoxLayout>
#include <QWizardPage>

namespace {
QWizardPage* makePage(const QString& title, const QString& subtitle) {
    auto* p = new QWizardPage;
    p->setTitle(title);
    p->setSubTitle(subtitle);
    return p;
}
}

FirstRunWizard::FirstRunWizard(QWidget* parent, const QString& paddleDir, const QString& tesseract, const QString& tessdata)
    : QWizard(parent), ocr_(new OcrService(this)), translator_(new TranslationService(this)),
      paddleDir_(paddleDir), tesseract_(tesseract), tessdata_(tessdata) {
    setWindowTitle(QStringLiteral("자막 번역 시작하기"));
    setWizardStyle(QWizard::ModernStyle);
    setButtonText(QWizard::NextButton, QStringLiteral("다음"));
    setButtonText(QWizard::BackButton, QStringLiteral("이전"));
    setButtonText(QWizard::FinishButton, QStringLiteral("시작하기"));
    setButtonText(QWizard::CancelButton, QStringLiteral("자세한 설정으로"));
    resize(640, 520);
    ocr_->setPaddleModelDir(paddleDir_);

    auto* usePage = makePage(QStringLiteral("무엇을 번역할까요?"), QStringLiteral("나중에 창 위쪽 탭이나 Ctrl+Alt+1~3으로 언제든 바꿀 수 있습니다."));
    auto* useLayout = new QVBoxLayout(usePage);
    use_ = new QButtonGroup(this);
    const std::pair<Mode, const char*> uses[] = {
        {Mode::Game, "게임 대사 · 대사창을 읽어 바로 아래에 번역을 띄웁니다"},
        {Mode::Video, "영상 자막 · 유튜브·뉴스·트레일러의 화면 자막을 번역합니다"},
        {Mode::Text, "글·기사 · 복사한 글이나 화면의 글을 문단별로 번역합니다"},
        {Mode::File, "자막 파일 · .srt·.vtt·.ass 파일을 새 자막 파일로 번역합니다"},
    };
    for (const auto& [mode, text] : uses) {
        auto* option = new QRadioButton(QString::fromUtf8(text));
        use_->addButton(option, int(mode));
        useLayout->addWidget(option);
    }
    use_->button(int(Mode::Game))->setChecked(true);
    useLayout->addStretch(1);
    addPage(usePage);

    auto* languagePage = makePage(QStringLiteral("어떤 언어를 어떤 언어로?"), QStringLiteral("원문 언어를 모르면 자동 감지를 고르세요."));
    auto* languageForm = new QFormLayout(languagePage);
    source_ = new QComboBox;
    for (const auto& option : Languages::Ocr) source_->addItem(QString::fromUtf8(option.label), QString::fromLatin1(option.code));
    target_ = new QComboBox;
    for (const auto& option : Languages::Target) target_->addItem(QString::fromUtf8(option.label), QString::fromLatin1(option.code));
    languageForm->addRow(QStringLiteral("원문 언어"), source_);
    languageForm->addRow(QStringLiteral("번역 언어"), target_);
    addPage(languagePage);

    auto* keyPage = makePage(QStringLiteral("DeepL 연결 (선택)"),
        QStringLiteral("키가 없어도 글자 읽기(OCR)는 됩니다. 번역은 키를 넣은 뒤 시작됩니다."));
    auto* keyLayout = new QVBoxLayout(keyPage);
    key_ = new QLineEdit;
    key_->setEchoMode(QLineEdit::Password);
    key_->setPlaceholderText(QStringLiteral("DeepL API 키 (비워 두면 나중에 설정)"));
    key_->setAccessibleName(QStringLiteral("DeepL API 키"));
    keyLayout->addWidget(key_);
    auto* link = new QLabel(QStringLiteral("<a href='https://www.deepl.com/your-account/keys'>DeepL API 키 발급·확인</a>"));
    link->setOpenExternalLinks(true);
    keyLayout->addWidget(link);
    remember_ = new QCheckBox(QStringLiteral("이 Windows 계정에 키 저장 (자격 증명 관리자)"));
    remember_->setChecked(true);
    keyLayout->addWidget(remember_);
    auto* privacy = new QLabel(QStringLiteral("번역할 때 인식한 글자만 HTTPS로 DeepL에 보냅니다. 화면 이미지는 보내지 않습니다.\n"
                                              "요금제에 따라 비용이 들거나 무료 한도가 줄 수 있습니다. 같은 문장은 다시 보내지 않습니다."));
    Ui::panel(privacy);
    keyLayout->addWidget(privacy);
    consent_ = new QCheckBox(QStringLiteral("인식한 글자를 DeepL로 보내는 것과 비용 가능성을 확인했습니다"));
    keyLayout->addWidget(consent_);
    keyLayout->addStretch(1);
    addPage(keyPage);

    auto* testPage = makePage(QStringLiteral("시험해 보기"), QStringLiteral("예시 그림을 이 PC에서 읽어 봅니다. 화면 캡처나 전송은 하지 않습니다."));
    auto* testLayout = new QVBoxLayout(testPage);
    testImage_ = new QLabel;
    testImage_->setAlignment(Qt::AlignCenter);
    testImage_->setMinimumHeight(90);
    testLayout->addWidget(testImage_);
    auto* readButton = new QPushButton(QStringLiteral("① 글자 읽기 시험 (무료, 이 PC에서)"));
    auto* translateButton = new QPushButton(QStringLiteral("② 번역 시험 (DeepL, 약 20자 사용)"));
    testLayout->addWidget(readButton);
    testLayout->addWidget(translateButton);
    testResult_ = new QLabel(QStringLiteral("① 버튼을 눌러 보세요."));
    Ui::panel(testResult_);
    testLayout->addWidget(testResult_);
    testLayout->addStretch(1);
    connect(readButton, &QPushButton::clicked, this, &FirstRunWizard::runOcrTest);
    connect(translateButton, &QPushButton::clicked, this, &FirstRunWizard::runTranslationTest);
    connect(this, &QWizard::currentIdChanged, this, [this, translateButton](int) {
        QString text;
        testImage_->setPixmap(QPixmap::fromImage(sample(source_->currentData().toString(), &text)));
        const bool canTranslate = !key_->text().trimmed().isEmpty() && consent_->isChecked();
        translateButton->setEnabled(canTranslate);
        translateButton->setToolTip(canTranslate ? QString() : QStringLiteral("이전 단계에서 키를 넣고 전송에 동의하면 쓸 수 있습니다."));
    });
    connect(ocr_, &OcrService::recognized, this, [this](const QString& text) {
        lastRead_ = text;
        testResult_->setText(text.isEmpty() ? QStringLiteral("글자를 찾지 못했습니다 · 설정 → OCR에서 엔진을 확인하세요.")
                                            : QStringLiteral("읽은 글자: %1\n잘 읽혔다면 [시작하기]를 누르세요.").arg(text));
    });
    connect(ocr_, &OcrService::failed, this, [this](const QString& error) {
        testResult_->setText(QStringLiteral("읽지 못했습니다 · %1").arg(error));
    });
    connect(translator_, &TranslationService::translated, this, [this](const QStringList& texts, const QString&) {
        testResult_->setText(QStringLiteral("원문: %1\n번역: %2\n번역까지 됩니다. [시작하기]를 누르세요.").arg(lastRead_, texts.value(0)));
    });
    connect(translator_, &TranslationService::failed, this, [this](const QString& error) {
        testResult_->setText(QStringLiteral("번역하지 못했습니다 · %1").arg(error));
    });
    addPage(testPage);
}
FirstRunWizard::~FirstRunWizard() { ocr_->cancel(); translator_->cancel(); }
QImage FirstRunWizard::sample(const QString& sourceCode, QString* text) {
    QString line = QStringLiteral("Press any key to continue.");
    QString font = QStringLiteral("Segoe UI");
    if (sourceCode.startsWith(QLatin1String("jpn")) || sourceCode == QLatin1String("multi")) {
        line = QStringLiteral("今日はいい天気です。"); font = QStringLiteral("Yu Gothic");
    } else if (sourceCode.startsWith(QLatin1String("chi"))) {
        line = QStringLiteral("今天天气很好。"); font = QStringLiteral("Microsoft YaHei");
    } else if (sourceCode == QLatin1String("kor")) {
        line = QStringLiteral("오늘은 날씨가 좋습니다."); font = QStringLiteral("Malgun Gothic");
    }
    if (text) *text = line;
    QImage image(560, 90, QImage::Format_RGB32);
    image.fill(QColor(32, 39, 51));
    QPainter p(&image);
    QFont f(font); f.setPixelSize(34); p.setFont(f);
    p.setPen(Qt::white);
    p.drawText(image.rect(), Qt::AlignCenter, line);
    return image;
}
void FirstRunWizard::runOcrTest() {
    if (ocr_->isBusy()) return;
    testResult_->setText(QStringLiteral("읽는 중… 처음에는 모델을 불러오느라 몇 초 걸립니다."));
    QString text;
    ocr_->recognize(sample(source_->currentData().toString(), &text), tesseract_, tessdata_, 0, source_->currentData().toString());
}
void FirstRunWizard::runTranslationTest() {
    if (lastRead_.isEmpty()) { testResult_->setText(QStringLiteral("먼저 ① 글자 읽기 시험을 하세요.")); return; }
    if (translator_->isBusy()) return;
    testResult_->setText(QStringLiteral("DeepL에 보내는 중…"));
    const QString key = key_->text().trimmed();
    translator_->translate({lastRead_}, key, TranslationService::isFreeKey(key), target_->currentData().toString());
}
FirstRunWizard::Result FirstRunWizard::choices() const {
    Result r;
    r.mode = Mode(std::max(0, use_->checkedId()));
    r.source = source_->currentData().toString();
    r.target = target_->currentData().toString();
    r.apiKey = key_->text().trimmed();
    r.rememberKey = remember_->isChecked();
    r.consent = consent_->isChecked();
    return r;
}
