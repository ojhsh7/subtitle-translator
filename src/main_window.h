#pragma once
#include "ocr_service.h"
#include "translation_service.h"
#include <QMainWindow>
#include <QImage>
#include "subtitle/core.h"
#include <QPointer>
#include <QRect>
class QComboBox;
class QLabel;
class QPushButton;
class QPlainTextEdit;
class QCheckBox;
class QScreen;
class QCloseEvent;
class SubtitleOverlay;
class MainWindow : public QMainWindow {
public:
    friend class WindowChecks;
    MainWindow();
    ~MainWindow() override;
protected:
    bool nativeEvent(const QByteArray& eventType, void* message, qintptr* result) override;
    void closeEvent(QCloseEvent*) override;
private:
    void settings();
    void chooseRegion();
    void startCapture();
    void importImage();
    void processImage(const QImage& image, int source);
    void resetOutput();
    void cancel();
    void finish(const QString& status);
    void setBusy(bool busy);
    void refreshScreens();
    void restoreWindows();
    void showResult();
    void presentTranslation();
    void previewSubtitle();
    bool showSubtitle(const QString& text);
    QComboBox* screens_;
    QLabel* status_;
    QLabel* regionLabel_;
    QLabel* preview_;
    QPushButton* select_;
    QPushButton* capture_;
    QPushButton* settings_;
    QPushButton* cancel_;
    QPushButton* import_;
    QPushButton* retry_;
    QImage lastImage_;
    int lastSource_ = ST_IMAGE_FILE;
    int activeSource_ = ST_IMAGE_FILE;
    st_session* session_ = st_create();
    uint64_t request_ = 0;
    QPlainTextEdit* original_;
    QWidget* resultWindow_;
    SubtitleOverlay* subtitleOverlay_;
    QCheckBox* inlineCaptions_;
    QPushButton* captionPreview_;
    QLabel* resultStatus_;
    QPlainTextEdit* result_;
    QPointer<QScreen> selectedScreen_;
    QRect selectedGeometry_;
    qreal selectedDpr_ = 1;
    QRect region_;
    OcrService ocr_;
    TranslationService translation_;
    QString executable_;
    QString tessdata_;
    QString apiKey_;
    bool freeApi_ = true;
    bool consent_ = false;
    bool remember_ = false;
    bool busy_ = false;
    bool modal_ = false;
    bool registered_ = false;
    bool restoreMain_ = false;
    bool restoreResult_ = false;
    bool restoreOverlay_ = false;
    int psm_ = 6;
    quint64 generation_ = 0;
};
