#pragma once
#include "mode_profile.h"
#include <QWizard>
class QButtonGroup;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class OcrService;
class TranslationService;

// First launch: what to translate, which languages, DeepL (optional) and a test run on a sample
// picture, so the first translation is seen within a minute or two without reading a manual.
class FirstRunWizard final : public QWizard {
public:
    struct Result {
        Mode mode = Mode::Game;
        QString source, target;
        QString apiKey;
        bool rememberKey = true;
        bool consent = false;
    };
    // paddleDir/tesseract: the engines the app found, used for the local OCR test.
    FirstRunWizard(QWidget* parent, const QString& paddleDir, const QString& tesseract, const QString& tessdata);
    ~FirstRunWizard() override;
    Result choices() const;
    static QImage sample(const QString& sourceCode, QString* text);
private:
    void runOcrTest();
    void runTranslationTest();
    QButtonGroup* use_;
    QComboBox* source_;
    QComboBox* target_;
    QLineEdit* key_;
    QCheckBox* remember_;
    QCheckBox* consent_;
    QLabel* testImage_;
    QLabel* testResult_;
    OcrService* ocr_;
    TranslationService* translator_;
    QString paddleDir_, tesseract_, tessdata_;
    QString lastRead_;
};
