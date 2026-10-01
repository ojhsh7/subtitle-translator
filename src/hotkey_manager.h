#pragma once
#include <QMap>
#include <QKeySequence>
#include <QList>
#include <QString>

// Global hotkeys of the Windows app: defaults, user changes (saved under hotkeys/<name>),
// duplicate checks and RegisterHotKey. The ids are the WM_HOTKEY wParam values.
class HotkeyManager {
public:
    enum Action { Capture = 1, Auto = 2, Mouse = 3, Hover = 4, ModeGame = 11, ModeVideo = 12, ModeText = 13 };
    struct Binding {
        Action action;
        const char* key;       // settings name
        const char* label;     // Korean label for the settings page
        QKeySequence standard;
        QKeySequence fallback; // tried once when the default is taken by another app (M -> F8, H -> F9)
    };
    static const QList<Binding>& bindings();
    static const Binding* binding(Action action);
    HotkeyManager();
    // Saved sequence or the default. An empty sequence means "no hotkey" (the button still works).
    QKeySequence sequence(Action action) const;
    // Checks the format and duplicates, then saves. Registration happens on the next registerAll().
    bool setSequence(Action action, const QKeySequence& sequence, QString* problem);
    void resetToDefaults();
    // Registers every action on the window; uses the fallback key when the default is taken.
    void registerAll(quintptr hwnd);
    void unregisterAll();
    bool isRegistered(Action action) const { return active_.contains(action); }
    QKeySequence active(Action action) const { return active_.value(action); }
    // Readable "Ctrl+Alt+T" for status texts; "단축키 없음" when unregistered.
    QString text(Action action) const;
    QList<Action> failed() const { return failed_; }
    // Ctrl/Alt/Shift/Win + letter, digit or F1-F24, as RegisterHotKey arguments.
    static bool toNative(const QKeySequence& sequence, unsigned* modifiers, unsigned* virtualKey);
    // Whether another program already holds this combination (tries a thread hotkey and releases it).
    static bool availableSystemWide(const QKeySequence& sequence);
private:
    quintptr hwnd_ = 0;
    QMap<Action, QKeySequence> active_;
    QList<Action> failed_;
};
