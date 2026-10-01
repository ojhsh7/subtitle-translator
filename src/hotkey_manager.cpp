#include "hotkey_manager.h"
#include <QSettings>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

const QList<HotkeyManager::Binding>& HotkeyManager::bindings() {
    static const QList<Binding> list{
        {Capture, "capture", "한 번 번역 (선택 영역)", QKeySequence(QStringLiteral("Ctrl+Alt+T")), {}},
        {Auto, "auto", "자동 번역 켜기·끄기", QKeySequence(QStringLiteral("Ctrl+Alt+P")), {}},
        {Mouse, "mouse", "마우스 주변 번역", QKeySequence(QStringLiteral("Ctrl+Alt+M")), QKeySequence(QStringLiteral("Ctrl+Alt+F8"))},
        {Hover, "hover", "마우스 올려두기 켜기·끄기", QKeySequence(QStringLiteral("Ctrl+Alt+H")), QKeySequence(QStringLiteral("Ctrl+Alt+F9"))},
        {ModeGame, "modeGame", "게임 모드로 전환", QKeySequence(QStringLiteral("Ctrl+Alt+1")), {}},
        {ModeVideo, "modeVideo", "영상 모드로 전환·켜기", QKeySequence(QStringLiteral("Ctrl+Alt+2")), {}},
        {ModeText, "modeText", "글 모드 · 클립보드 번역", QKeySequence(QStringLiteral("Ctrl+Alt+3")), {}},
    };
    return list;
}
const HotkeyManager::Binding* HotkeyManager::binding(Action action) {
    for (const auto& b : bindings()) if (b.action == action) return &b;
    return nullptr;
}
HotkeyManager::HotkeyManager() = default;
QKeySequence HotkeyManager::sequence(Action action) const {
    const Binding* b = binding(action);
    if (!b) return {};
    const QVariant saved = QSettings().value(QStringLiteral("hotkeys/") + QLatin1String(b->key));
    return saved.isValid() ? QKeySequence(saved.toString(), QKeySequence::PortableText) : b->standard;
}
bool HotkeyManager::setSequence(Action action, const QKeySequence& wanted, QString* problem) {
    const Binding* b = binding(action);
    if (!b) return false;
    const QKeySequence sequence = wanted.isEmpty() ? QKeySequence() : QKeySequence(wanted[0]);
    unsigned modifiers = 0, key = 0;
    if (!sequence.isEmpty() && !toNative(sequence, &modifiers, &key)) {
        if (problem) *problem = QStringLiteral("Ctrl·Alt 중 하나 이상과 글자·숫자·F1~F24 키를 함께 누르세요.");
        return false;
    }
    if (!sequence.isEmpty())
        for (const auto& other : bindings())
            if (other.action != action && this->sequence(other.action) == sequence) {
                if (problem) *problem = QStringLiteral("%1에 이미 쓰는 키입니다.").arg(QString::fromUtf8(other.label));
                return false;
            }
    QSettings().setValue(QStringLiteral("hotkeys/") + QLatin1String(b->key), sequence.toString(QKeySequence::PortableText));
    return true;
}
void HotkeyManager::resetToDefaults() { QSettings().remove(QStringLiteral("hotkeys")); }
bool HotkeyManager::toNative(const QKeySequence& sequence, unsigned* modifiers, unsigned* virtualKey) {
    if (sequence.isEmpty()) return false;
    const QKeyCombination combination = sequence[0];
    const Qt::KeyboardModifiers mods = combination.keyboardModifiers();
    const int key = combination.key();
    unsigned m = MOD_NOREPEAT;
    if (mods & Qt::ControlModifier) m |= MOD_CONTROL;
    if (mods & Qt::AltModifier) m |= MOD_ALT;
    if (mods & Qt::ShiftModifier) m |= MOD_SHIFT;
    if (mods & Qt::MetaModifier) m |= MOD_WIN;
    if (!(m & (MOD_CONTROL | MOD_ALT))) return false; // a plain or Shift-only key would block typing
    unsigned vk = 0;
    if (key >= Qt::Key_A && key <= Qt::Key_Z) vk = 'A' + (key - Qt::Key_A);
    else if (key >= Qt::Key_0 && key <= Qt::Key_9) vk = '0' + (key - Qt::Key_0);
    else if (key >= Qt::Key_F1 && key <= Qt::Key_F24) vk = VK_F1 + (key - Qt::Key_F1);
    else return false;
    if (modifiers) *modifiers = m;
    if (virtualKey) *virtualKey = vk;
    return true;
}
bool HotkeyManager::availableSystemWide(const QKeySequence& sequence) {
    unsigned m = 0, vk = 0;
    if (!toNative(sequence, &m, &vk)) return false;
    constexpr int ProbeId = 0xBF00;
    if (!RegisterHotKey(nullptr, ProbeId, m, vk)) return false;
    UnregisterHotKey(nullptr, ProbeId);
    return true;
}
void HotkeyManager::registerAll(quintptr hwnd) {
    unregisterAll();
    hwnd_ = hwnd;
    failed_.clear();
    const HWND handle = reinterpret_cast<HWND>(hwnd);
    for (const auto& b : bindings()) {
        const QKeySequence wanted = sequence(b.action);
        if (wanted.isEmpty()) continue; // the user removed this hotkey on purpose
        bool ok = false;
        unsigned m = 0, vk = 0;
        if (toNative(wanted, &m, &vk)) ok = RegisterHotKey(handle, b.action, m, vk) != 0;
        if (ok) { active_.insert(b.action, wanted); continue; }
        // Only the shipped default falls back; a key the user picked is reported as taken instead.
        if (wanted == b.standard && !b.fallback.isEmpty() && toNative(b.fallback, &m, &vk) &&
            RegisterHotKey(handle, b.action, m, vk)) { active_.insert(b.action, b.fallback); continue; }
        failed_ << b.action;
    }
}
void HotkeyManager::unregisterAll() {
    const HWND handle = reinterpret_cast<HWND>(hwnd_);
    for (auto it = active_.cbegin(); it != active_.cend(); ++it) UnregisterHotKey(handle, it.key());
    active_.clear();
}
QString HotkeyManager::text(Action action) const {
    return isRegistered(action) ? active(action).toString(QKeySequence::NativeText) : QStringLiteral("단축키 없음");
}
