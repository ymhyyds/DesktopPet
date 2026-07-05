#pragma once

#include <QObject>
#include <QList>
#include <memory>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

#include "../pet/CharacterLoader.h" // for CharacterConfig

// 前置声明，避免头文件互相 include 造成编译依赖爆炸。
// 具体实现会在后续模块中给出（pet/ai/memory/interaction/tray/chat）。
class WindowManager;
class GlobalHotkeyManager;
class PetWindow;
class CharacterLoader;
class InputProxyWindow;
class DeepSeekClient;
class PromptManager;
class EmotionAnalyzer;
class TtsEngine;
class WeatherManager;
class MemoryDB;
class InteractionManager;
class TrayManager;
class ChatWindow;
class SettingsWindow;
class ParameterWindow;
class QFileSystemWatcher;

/**
 * @brief App 是整个桌宠程序的应用层入口（不是 QApplication 本身）。
 *
 * 职责：
 *  1. 按依赖顺序创建各模块（角色加载 -> AI -> 记忆 -> 窗口 -> 交互 -> 托盘 -> 聊天）
 *  2. 把模块之间用信号/槽连接起来（解耦：模块互相之间不直接 new 对方）
 *  3. 提供启动 / 退出 / 切换角色 等顶层操作
 *
 * App 本身不写具体业务逻辑（比如怎么发 HTTP 请求），
 * 这些都下放到各自模块里，App 只负责"组装"和"调度"。
 */
class App : public QObject
{
    Q_OBJECT

public:
    explicit App(QObject *parent = nullptr);
    ~App() override;

    /**
     * @brief 初始化整个应用：扫描角色、连接数据库、创建窗口、显示托盘。
     * @return 初始化是否成功（比如找不到任何角色包就会失败）。
     */
    bool initialize();

    /**
     * @brief 切换当前角色（character 包目录名，例如 "default"）。
     *        切换后会重新加载动画资源和人格 Prompt，但保留聊天记忆。
     */
    bool switchCharacter(const QString &characterName);

public slots:
    /// 由托盘菜单"退出"触发，做清理工作后退出事件循环。
    void quit();

private:
    void setupConnections();
    void setupBedtimeReminder();
    /// 截取桌宠（和聊天窗口）截图，保存并复制到剪贴板
    void takeScreenshot();

    /// characters/ 目录变更时自动重新扫描角色
    void doAutoScanCharacters();

    /// 统一的双窗口 Z-order 策略
    enum class PairedWindowPolicy {
        FullscreenOverlay,  ///< 两个窗口同步 HWND_TOPMOST，Proxy 在 Pet 上方
        Normal,             ///< 两个窗口始终 HWND_TOPMOST，不被普通窗口遮挡
        Hidden              ///< 两个窗口同步隐藏
    };

    void applyPairedWindowPolicy(PairedWindowPolicy policy);
    void syncPairedWindowZOrder();

    /// 统一打开设置窗口 — 在显示前捕获外部全屏状态，避免 SettingsWindow 前台干扰
    void openSettingsWindow();

    /// 刷新 external fullscreen 权威状态（由定时器信号触发）
    void refreshExternalFullscreenState();
    /// 根据当前 m_effectiveExternalFullscreenActive 选择并执行策略
    void applyCurrentPairedWindowPolicy();

    /// 设置全屏时是否显示桌宠（App 权威状态，替代 PetWindow 旧方法）
    void setFullscreenDisplayEnabled(bool enabled);
    bool fullscreenDisplayEnabled() const { return m_fullscreenDisplayEnabled; }

#ifdef Q_OS_WIN
    /// 判断 foreground 窗口是否属于 fullscreenHost 的全屏会话
    ///（同进程 / owner 链 / rootOwner 关联）
    bool belongsToFullscreenSession(HWND foreground, HWND fullscreenHost) const;

    /// 判断 foreground 是否是在 fullscreenHost 全屏显示器上的普通小窗口
    ///（同显示器、非最大化、尺寸明显小于显示器、有交集）
    bool isSmallWindowOverFullscreenHost(HWND foreground, HWND fullscreenHost) const;
#endif

private:
    // ──── 外部全屏权威状态（App 唯一管理） ────
#ifdef Q_OS_WIN
    HWND  m_foregroundHwnd                      = nullptr;
    HWND  m_lastExternalForegroundHwnd          = nullptr;
    HWND  m_lastExternalFullscreenHwnd          = nullptr;
    HWND  m_effectiveExternalFullscreenHwnd     = nullptr;
#endif
    bool  m_effectiveExternalFullscreenActive   = false;

    // 进入全屏时保存的可见性
    bool  m_savedPetVisibleForPolicy            = true;
    bool  m_savedProxyVisibleForPolicy          = true;
    bool  m_policyVisibilitySaved               = false;

    // ──── App 权威全屏显示设置 ────
    bool    m_fullscreenDisplayEnabled          = true;
    QTimer *m_fullscreenCheckTimer              = nullptr;

    // ──── 打开设置窗口前捕获的外部 foreground HWND ────
#ifdef Q_OS_WIN
    HWND    m_auxiliaryHostForSettings          = nullptr;
#endif

    std::unique_ptr<CharacterLoader>   m_characterLoader;
    std::unique_ptr<MemoryDB>          m_memoryDb;
    std::unique_ptr<PromptManager>     m_promptManager;
    std::unique_ptr<EmotionAnalyzer>   m_emotionAnalyzer;
    std::unique_ptr<DeepSeekClient>    m_aiClient;
    std::unique_ptr<TtsEngine>         m_ttsEngine;
    std::unique_ptr<WeatherManager>    m_weatherManager;

    std::unique_ptr<WindowManager>     m_windowManager;
    std::unique_ptr<PetWindow>         m_petWindow;
    std::unique_ptr<InputProxyWindow>  m_inputProxyWindow;
    std::unique_ptr<InteractionManager> m_interactionManager;
    std::unique_ptr<TrayManager>       m_trayManager;
    std::unique_ptr<ChatWindow>        m_chatWindow;
    std::unique_ptr<SettingsWindow>    m_settingsWindow;
    std::unique_ptr<ParameterWindow>   m_parameterWindow;
    std::unique_ptr<GlobalHotkeyManager> m_hotkeyManager;

    QString m_currentCharacter;

    // --- 深夜就寝提醒 ---
    QTimer *m_bedtimeTimer   = nullptr;
    bool    m_bedtimeReminded = false;

    // --- 自动扫描新模型 ---
    QFileSystemWatcher *m_fsWatcher = nullptr;
    QString m_charactersPath;   // characters/ 的绝对路径（基于 exe 目录）

    // --- AI 请求繁忙标志（防止挂机主动请求覆盖流式 buffer） ---
    bool m_aiBusy = false;

    // --- Edge TTS 测试语音状态 ────
    bool m_testSpeechInProgress = false;
};
