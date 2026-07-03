#include "GlobalHotkeyManager.h"

#include <QCoreApplication>
#include <QWidget>
#include <QDebug>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

GlobalHotkeyManager::GlobalHotkeyManager(QWidget *window, QObject *parent)
    : QObject(parent)
{
#ifdef Q_OS_WIN
    if (window) {
        m_hwnd = reinterpret_cast<void *>(window->winId());
    } else {
        qWarning() << "[GlobalHotkeyManager] 未提供窗口句柄，热键可能无法正常接收";
    }
    qApp->installNativeEventFilter(this);
#else
    Q_UNUSED(window);
    qWarning() << "[GlobalHotkeyManager] 全局热键仅在 Windows 平台可用";
#endif
}

GlobalHotkeyManager::~GlobalHotkeyManager()
{
    unregisterAll();

#ifdef Q_OS_WIN
    if (qApp) {
        qApp->removeNativeEventFilter(this);
    }
#endif
}

bool GlobalHotkeyManager::registerHotkey(HotkeyAction action, quint32 modifiers, quint32 key)
{
#ifdef Q_OS_WIN
    HWND hwnd = static_cast<HWND>(m_hwnd);
    if (!hwnd) {
        qWarning() << "[GlobalHotkeyManager] 无有效窗口句柄，无法注册热键";
        return false;
    }

    // 如果该动作已注册，先注销旧的
    if (m_actionToId.contains(action)) {
        UnregisterHotKey(hwnd, m_actionToId[action]);
        m_idToAction.remove(m_actionToId.take(action));
    }

    const int id = m_nextId++;
    if (!RegisterHotKey(hwnd, id, modifiers, key)) {
        qWarning() << "[GlobalHotkeyManager] 注册热键失败"
                     << "action:" << action
                     << "modifiers:" << modifiers
                     << "key:" << key
                     << "error:" << GetLastError();
        return false;
    }

    m_actionToId[action] = id;
    m_idToAction[id] = action;

    qInfo() << "[GlobalHotkeyManager] 已注册热键 action:" << action << "id:" << id;
    return true;
#else
    Q_UNUSED(action);
    Q_UNUSED(modifiers);
    Q_UNUSED(key);
    return false;
#endif
}

bool GlobalHotkeyManager::unregisterHotkey(HotkeyAction action)
{
#ifdef Q_OS_WIN
    if (!m_actionToId.contains(action)) {
        return false;
    }
    const int id = m_actionToId.take(action);
    m_idToAction.remove(id);
    const bool ok = (UnregisterHotKey(static_cast<HWND>(m_hwnd), id) != FALSE);
    if (ok) {
        qInfo() << "[GlobalHotkeyManager] 已注销热键 action:" << action;
    }
    return ok;
#else
    Q_UNUSED(action);
    return false;
#endif
}

void GlobalHotkeyManager::unregisterAll()
{
#ifdef Q_OS_WIN
    HWND hwnd = static_cast<HWND>(m_hwnd);
    for (auto it = m_idToAction.begin(); it != m_idToAction.end(); ++it) {
        UnregisterHotKey(hwnd, it.key());
    }
    m_actionToId.clear();
    m_idToAction.clear();
    qInfo() << "[GlobalHotkeyManager] 已注销所有热键";
#endif
}

bool GlobalHotkeyManager::nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result)
{
    Q_UNUSED(result);

#ifdef Q_OS_WIN
    // Qt 6 Windows 下 eventType 为 "windows_generic_MSG"
    if (eventType != QByteArrayLiteral("windows_generic_MSG")
        && eventType != QByteArrayLiteral("windows_dispatcher_MSG")) {
        return false;
    }

    MSG *msg = static_cast<MSG *>(message);
    if (msg->message == WM_HOTKEY) {
        const int id = static_cast<int>(msg->wParam);
        auto it = m_idToAction.constFind(id);
        if (it != m_idToAction.constEnd()) {
            emit hotkeyPressed(it.value());
            return true;
        }
    }
#else
    Q_UNUSED(eventType);
    Q_UNUSED(message);
#endif

    return false;
}
