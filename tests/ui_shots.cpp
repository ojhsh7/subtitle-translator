// Screenshots for design and usability reviews, not a test: the main window in several states, every
// settings page, the dialogue history, the result window and a caption. Uses the test settings group,
// so the user's own settings and key are never read or changed. No OCR, no network.
//
//   ui_shots.exe <output folder>
#include "main_window.h"
#include "fluent_widgets.h"
#include "subtitle_overlay.h"
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QEventLoop>
#include <QGuiApplication>
#include <QListWidget>
#include <QMenu>
#include <QStyleHints>
#include <QPlainTextEdit>
#include <QScreen>
#include <QSettings>
#include <QTimer>
#include <cstdio>

namespace {
void wait(int ms) {
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}
}

// Named like the UI test class: MainWindow lets exactly that class read its members.
class WindowChecks {
public:
    static int run(const QString& folder) {
        QDir().mkpath(folder);
        int saved = 0;
        auto shoot = [&](QWidget* widget, const QString& name) {
            wait(250);
            const bool ok = widget->grab().save(QDir(folder).filePath(name + QStringLiteral(".png")));
            std::printf("%s %s\n", ok ? "saved" : "FAILED", qPrintable(name));
            saved += ok;
        };
        QSettings().clear();
        {
            MainWindow window;
            if (!qEnvironmentVariableIsEmpty("SUBTITLE_TEST_PADDLE")) { // the models the app ships with
                window.paddleDir_ = qEnvironmentVariable("SUBTITLE_TEST_PADDLE"); window.paddleEngine_ = true; window.applyOcrEngine();
            }
            window.show();
            shoot(&window, QStringLiteral("01-main-first-run"));

            // Ready: a region, a (fake) key and consent.
            QScreen* screen = QGuiApplication::primaryScreen();
            window.setRegion(screen, QRect(200, 700, 900, 160));
            window.apiKey_ = QStringLiteral("00000000-0000-0000-0000-000000000000:fx");
            window.consent_ = true;
            window.refreshUi();
            shoot(&window, QStringLiteral("02-main-ready"));

            // A line on the stage, as after a translation.
            window.original_->setPlainText(QStringLiteral("今日はいい天気ですね。散歩に行きましょう。"));
            window.result_->setPlainText(QStringLiteral("오늘은 날씨가 좋네요. 산책하러 가요."));
            window.stage_->setContent(window.original_->toPlainText(), window.result_->toPlainText());
            window.setStatus(QStringLiteral("번역했어요. 다음 대사를 기다리고 있어요."));
            shoot(&window, QStringLiteral("03-main-caption"));

            // The more (⋯) and tray menus, opened beside the window.
            int menuIndex = 0;
            for (QMenu* menu : window.findChildren<QMenu*>()) {
                if (menu->actions().isEmpty()) continue;
                menu->popup(window.geometry().topRight() + QPoint(16, 0));
                shoot(menu, QStringLiteral("04-menu-%1").arg(++menuIndex));
                menu->hide();
            }

            // Every settings page, shown without its modal loop.
            for (int page = 0; page < window.settingsPages_->count(); ++page) {
                window.settingsPages_->setCurrentRow(page);
                window.reloadSettings_();
                window.settingsWindow_->show();
                shoot(window.settingsWindow_, QStringLiteral("1%1-settings-%2").arg(page).arg(
                    window.settingsPages_->item(page)->text().simplified().replace(QLatin1Char(' '), QLatin1Char('-'))));
            }
            window.settingsWindow_->hide();

            // Dialogue history with a few lines.
            const char* lines[][2] = {{"今日はいい天気ですね。", "오늘은 날씨가 좋네요."},
                                      {"明日は学校に行きます。", "내일은 학교에 갈 거예요."},
                                      {"この本はとても面白い。", "이 책은 정말 재미있어요."}};
            for (const auto& line : lines) window.addToBacklog(QString::fromUtf8(line[0]), QString::fromUtf8(line[1]));
            window.backlogWindow_->show();
            shoot(window.backlogWindow_, QStringLiteral("20-backlog"));
            window.backlogWindow_->hide();

            window.showResult();
            shoot(window.resultWindow_, QStringLiteral("21-result-window"));
            window.resultWindow_->hide();

            window.inlineCaptions_->setChecked(true);
            window.previewSubtitle();
            if (window.subtitleOverlay_->isVisible()) shoot(window.subtitleOverlay_, QStringLiteral("22-caption-preview"));
            window.subtitleOverlay_->hide();
        }
        QSettings().clear();
        std::printf("%d screenshots in %s\n", saved, qPrintable(folder));
        return saved > 0 ? 0 : 1;
    }
};

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    app.setFont(QFont("Malgun Gothic", 10)); // as the app (main.cpp)
    app.setStyle(new AppStyle);
    app.setProperty("testMode", true);
    app.setApplicationVersion("0.1.0");
    app.setProperty("buildId", QStringLiteral("ui_shots"));
    app.setOrganizationName("SubtitleTranslatorChecks");
    app.setApplicationName("TemporaryUiShots");
    app.setQuitOnLastWindowClosed(false);
    if (argc < 2) { std::puts("usage: ui_shots <output folder> [--dark]"); return 2; }
    // --dark: as on a PC in dark mode (the app follows Windows through the palette).
    if (argc > 2 && QLatin1String(argv[2]) == QLatin1String("--dark")) QGuiApplication::styleHints()->setColorScheme(Qt::ColorScheme::Dark);
    return WindowChecks::run(QString::fromLocal8Bit(argv[1]));
}
