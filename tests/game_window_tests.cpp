#include "game_window.h"
#include <QCoreApplication>
#include <QGuiApplication>
#include <QScreen>
#include <QWidget>
#include <QtTest>
#define NOMINMAX
#include <windows.h>

// A real top-level window of this test process plays the game: tests pass ignoreProcess = 0.
class GameWindowChecks : public QObject {
    Q_OBJECT
private slots:
    void keyIsCaseAndSeparatorInsensitive() {
        QCOMPARE(GameWindow::normalizedKey(QStringLiteral("C:\\Games\\Some Game\\Game.EXE")),
                 QStringLiteral("c:/games/some game/game.exe"));
        QCOMPARE(GameWindow::normalizedKey(QStringLiteral("C:/Games/./Some Game/../Some Game/game.exe")),
                 QStringLiteral("c:/games/some game/game.exe"));
    }
    void regionFollowsTheWindow() {
        const QRect client(1000, 200, 1280, 720);
        const QRect subtitle(1100, 750, 900, 140);
        const QRect relative = GameWindow::relativeTo(subtitle, client);
        QCOMPARE(relative, QRect(100, 550, 900, 140));
        // Moved: same size, new place.
        QCOMPARE(GameWindow::place(relative, client.size(), QRect(-300, 50, 1280, 720)), QRect(-200, 600, 900, 140));
    }
    void regionScalesWithAResizedWindow() {
        const QRect relative(100, 550, 900, 140);
        // 1280x720 -> 1920x1080 is x1.5 on both axes.
        QCOMPARE(GameWindow::place(relative, QSize(1280, 720), QRect(0, 0, 1920, 1080)), QRect(150, 825, 1350, 210));
        // Parts outside the game's client area are cut off.
        QCOMPARE(GameWindow::place(QRect(1200, 700, 200, 50), QSize(1280, 720), QRect(0, 0, 1280, 720)), QRect(1200, 700, 80, 20));
        QVERIFY(GameWindow::place(relative, QSize(), QRect(0, 0, 10, 10)).isEmpty());
        QVERIFY(GameWindow::place(relative, QSize(1280, 720), QRect()).isEmpty());
    }
    void physicalAndLogicalRoundTrip() {
        QScreen* screen = QGuiApplication::primaryScreen();
        QVERIFY(screen);
        const QRect logical(screen->geometry().topLeft() + QPoint(40, 60), QSize(300, 80));
        const QRect physical = GameWindow::toPhysical(logical);
        QVERIFY(!physical.isEmpty());
        QCOMPARE(physical.size(), (QSizeF(logical.size()) * screen->devicePixelRatio()).toSize());
        const GameWindow::Area area = GameWindow::toScreen(physical);
        QCOMPARE(area.screen, screen);
        QCOMPARE(area.rect, QRect(QPoint(40, 60), QSize(300, 80)));
    }
    void findsTheWindowUnderARegionAndAfterItMoves() {
        QWidget game;
        game.setWindowTitle(QStringLiteral("Subtitle test game"));
        const QRect available = QGuiApplication::primaryScreen()->availableGeometry();
        game.setGeometry(QRect(available.topLeft() + QPoint(60, 60), QSize(640, 360)));
        game.show();
        game.raise(); game.activateWindow();
        QVERIFY(QTest::qWaitForWindowExposed(&game));
        const QPoint inside = game.mapToGlobal(QPoint(320, 300));
        // Another program's window may be on top in an interactive session; retry briefly.
        GameWindow::Info info;
        QTRY_VERIFY_WITH_TIMEOUT((info = GameWindow::at(inside, 0)).handle == quintptr(game.winId()), 3000);
        QCOMPARE(info.key, GameWindow::normalizedKey(QCoreApplication::applicationFilePath()));
        QCOMPARE(info.title, QStringLiteral("Subtitle test game"));
        QCOMPARE(info.client.size(), (QSizeF(game.size()) * game.devicePixelRatioF()).toSize());
        // Our own windows are skipped when the app looks for a game.
        QVERIFY(GameWindow::at(inside, GameWindow::currentProcess()).handle != quintptr(game.winId()));
        game.move(game.pos() + QPoint(80, 40));
        QTRY_COMPARE_WITH_TIMEOUT(GameWindow::find(info.key, info.handle).client.topLeft(),
                                  info.client.topLeft() + (QPointF(80, 40) * game.devicePixelRatioF()).toPoint(), 3000);
        game.showMinimized();
        QTRY_VERIFY_WITH_TIMEOUT(GameWindow::find(info.key, info.handle).handle != quintptr(game.winId()), 3000);
    }
};
QTEST_MAIN(GameWindowChecks)
#include "game_window_tests.moc"
