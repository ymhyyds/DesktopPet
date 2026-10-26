#pragma once

#include <QObject>
#include <QString>
#include <QList>
#include <QTimer>
#include <memory>

#include "../ai/PromptManager.h" // for ChatMessage

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
class MemoryExtractor;
class InteractionManager;
class TrayManager;
class ChatWindow;
class SettingsWindow;
class ParameterWindow;
class EvidenceManager;
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
    void checkBedtimeReminder();
    bool canRemindNow() const;
    void performBedtimeReminder();
    static QString sanitizeBedtimeMessage(const QString &msg);
    /// 截取桌宠（和聊天窗口）截图，保存并复制到剪贴板
    void takeScreenshot();

    /// characters/ 目录变更时自动重新扫描角色
    void doAutoScanCharacters();

    /// 把旧角色目录名下的历史消息归并到当前目录名下（目录改名后的一次性迁移）。
    /// 只改 messages 表的归属，不动角色目录；幂等，可每次启动调用。
    void migrateLegacyCharacterNames(const QStringList &availableNames);

    /// 从数据库恢复最近若干轮对话到 PromptManager（启动 / 切换角色）。
    /// 同时刷新聊天窗口的历史记录显示。
    void restoreCharacterContext(const QString &characterName);

    // ──── 长期记忆（P1+P2） ────

    /// 从 QSettings 读取总开关 / 自动抽取开关。
    void loadMemorySettings();

    /// 生成注入用的 [长期记忆] 文本块（固定画像 + 动态检索），并回写 hit_count。
    /// seedText 为空时只取固定画像（挂机搭话场景可传最近话题）。
    QString buildMemoryBlock(const QString &seedText, int dynamicLimit = 6);

    /// 触发一次自动抽取；成功发出请求返回 true。
    bool triggerMemoryExtraction(const QString &reason);

    /// 处理 /记住、/忘掉；命中指令返回 true（不发送给 AI）。
    bool handleMemoryCommand(const QString &text);

    /// 把当前记忆列表推送到设置页。
    void refreshSettingsMemoryList(const QString &keyword = QString());

    /// 启动九月历史回填（手动按钮 / 测试）。
    void startMemoryBackfill(const QString &afterIso, const QString &beforeIso);

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

    /// 开机自启切换：安全操作（备份 → 写入/删除 → sync → 验证 → 失败恢复）。
    void handleAutoStartToggle(bool enabled);

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
    std::unique_ptr<MemoryExtractor>   m_memoryExtractor;
    std::unique_ptr<PromptManager>     m_promptManager;
    std::unique_ptr<EmotionAnalyzer>   m_emotionAnalyzer;
    std::unique_ptr<DeepSeekClient>    m_aiClient;
    std::unique_ptr<TtsEngine>         m_ttsEngine;
    std::unique_ptr<WeatherManager>    m_weatherManager;
    std::unique_ptr<EvidenceManager>   m_evidenceManager;

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
    QTimer *m_bedtimeTimer               = nullptr;
    QString m_bedtimeMessage;
    bool    m_bedtimeReminderEnabled      = false;
    bool    m_bedtimeReminderPending      = false;
    bool    m_bedtimeUseRandom            = true;
    int     m_bedtimeLastMessageIndex      = -1;
    QMetaObject::Connection m_bedtimePlaybackConnection;

    // --- 自动扫描新模型 ---
    QFileSystemWatcher *m_fsWatcher = nullptr;
    QString m_charactersPath;   // characters/ 的绝对路径（基于 exe 目录）

    // --- AI 请求繁忙标志（防止挂机主动请求覆盖流式 buffer） ---
    bool m_aiBusy = false;

    // ──── 长期记忆状态 ────
    bool    m_memoryEnabled = true;         // 总开关（默认开）
    bool    m_autoExtractEnabled = true;    // 自动抽取（默认开）
    int     m_memoryTurnsSinceExtract = 0;  // 自上次抽取以来的成功对话轮数
    bool    m_lastReplyWasProactive = false; // 当前在途回复是否由挂机搭话触发
    QTimer *m_memoryIdleTimer = nullptr;    // 闲置 3 分钟触发抽取

    /// 最近一次发送请求时的 conversationGeneration。
    /// 回复到达时与之比较，若已变化则放弃将回复写入 API history
    /// （防止风格切换后旧请求返回污染新上下文）。
    int m_lastSentGeneration = -1;

    // ──── 异步风格切换状态机 ────
    QString m_pendingStylePrompt;                         // 最新的目标风格（归一化后）
    int m_activeSummaryRequestId = 0;                     // 最近一次摘要请求的 ID（单调递增）
    bool m_styleSummaryPending = false;                   // 在途聊天结束后需启动摘要
    bool m_styleSummaryInProgress = false;                // 摘要请求已发出，等待回复
    QList<ChatMessage> m_immutableHistorySnapshot;         // beginStyleTransition 时的快照
    QTimer *m_summaryTimer = nullptr;                     // 15s 超时计时器

    void requestStyleTransition(const QString &stylePrompt);
    void actuallyStartSummary();
    void finishStyleTransition();
    QString parseSummaryJson(const QString &rawJson) const;
    QStringList extractFallbackUserMessages(const QList<ChatMessage> &snapshot) const;

    // --- Edge TTS 测试语音状态 ────
    bool m_testSpeechInProgress = false;
};
