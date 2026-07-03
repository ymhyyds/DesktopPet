#pragma once

#include <QObject>
#include <QAbstractNativeEventFilter>
#include <QMap>

class QWidget;

/**
 * @brief 全局快捷键管理器（Windows 实现）
 *
 * 基于 Windows RegisterHotKey API + QAbstractNativeEventFilter，
 * 在后台注册全局热键，按下时发射 hotkeyPressed 信号。
 *
 * 使用方式：
 *   auto *mgr = new GlobalHotkeyManager(petWindow, this);
 *   mgr->registerHotkey(GlobalHotkeyManager::TogglePetVisibility,
 *                        MOD_CONTROL | MOD_ALT, 'P');
 *   connect(mgr, &GlobalHotkeyManager::hotkeyPressed, ...);
 */
class GlobalHotkeyManager : public QObject, public QAbstractNativeEventFilter
{
    Q_OBJECT

public:
    /// 预定义的快捷键动作
    enum HotkeyAction {
        TogglePetVisibility,   ///< 隐藏/显示宠物
        OpenChat,              ///< 唤出聊天窗口
        CycleCharacter,        ///< 循环切换下一个角色
        Screenshot,            ///< 截图
    };
    Q_ENUM(HotkeyAction)

    /**
     * @param window 用于注册热键的顶层窗口（取 winId 传给 RegisterHotKey）
     * @param parent QObject 父对象
     */
    explicit GlobalHotkeyManager(QWidget *window, QObject *parent = nullptr);
    ~GlobalHotkeyManager() override;

    /**
     * @brief 注册一个全局热键
     * @param action   动作标识
     * @param modifiers 修饰键组合（Windows MOD_* 宏，如 MOD_CONTROL | MOD_ALT）
     * @param key      虚拟键码（如 'P'、'C'、VK_F1 等）
     * @return 是否注册成功
     */
    bool registerHotkey(HotkeyAction action, quint32 modifiers, quint32 key);

    /// 注销指定动作的热键
    bool unregisterHotkey(HotkeyAction action);

    /// 注销所有已注册的热键
    void unregisterAll();

protected:
    bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) override;

signals:
    void hotkeyPressed(GlobalHotkeyManager::HotkeyAction action);

private:
#ifdef Q_OS_WIN
    void *m_hwnd = nullptr;             // 窗口句柄，用于 RegisterHotKey
#endif
    QMap<HotkeyAction, int> m_actionToId;   // action → 注册 ID
    QMap<int, HotkeyAction> m_idToAction;   // 注册 ID → action
    int m_nextId = 1;                       // 自增 ID 池
};
