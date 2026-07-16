#include "App.h"
#include "WindowManager.h"

#include "../pet/PetWindow.h"
#include "../pet/CharacterLoader.h"

#include "../ai/DeepSeekClient.h"
#include "../ai/PromptManager.h"
#include "../ai/EvidenceManager.h"
#include "../ai/EmotionAnalyzer.h"
#include "../ai/TtsEngine.h"

#include "../memory/MemoryDB.h"

#include "../interaction/InteractionManager.h"
#include "../interaction/InputProxyWindow.h"
#include "../tray/TrayManager.h"
#include "../chat/ChatWindow.h"
#include "../pet/Live2DView.h"
#include "../settings/SettingsWindow.h"
#include "../settings/ParameterWindow.h"
#include "../settings/CropDialog.h"
#include "WeatherManager.h"
#include "GlobalHotkeyManager.h"
#include "BedtimeHelper.h"
#include "AutoStartManager.h"

#include <memory>

#include <QApplication>
#include <QMessageBox>

#ifdef Q_OS_WIN
#include <windows.h>
#endif
#include <QDebug>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonParseError>
#include <QPainter>
#include <QPointer>
#include <QStandardPaths>
#include <QSettings>
#include <QClipboard>
#include <QFileSystemWatcher>
#include <QMimeData>
#include <QUrl>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QTime>
#include <QRegularExpression>

App::App(QObject *parent)
    : QObject(parent)
{
}

App::~App() = default;

// ---------------------------------------------------------------------------
// 深夜就寝提醒：
//   - 生效时段 00:00～06:00
//   - 每 5 分钟检查一次，启动后立即检查
//   - 每次成功提醒后冷却 60 分钟（QSettings 持久化，重启仍遵守）
//   - 用户空闲超过 10 分钟不提醒
//   - 全屏逻辑复用现有 fullscreenDisplayEnabled 和外部全屏状态
//   - AI 忙碌 / TTS 忙碌时保留 pending，忙碌结束后再检查
//   - TTS 失败后不得重复提醒
// ---------------------------------------------------------------------------

void App::setupBedtimeReminder()
{
    // ── 从 QSettings 加载设置 ──
    QSettings ss(QStringLiteral("DesktopPet"), QStringLiteral("App"));
    m_bedtimeReminderEnabled = ss.value(QStringLiteral("BedtimeReminder/enabled"), true).toBool();
    const QString saved = ss.value(QStringLiteral("BedtimeReminder/message")).toString();
    m_bedtimeMessage = sanitizeBedtimeMessage(saved);
    m_bedtimeUseRandom = ss.value(QStringLiteral("BedtimeReminder/useRandomMessages"), true).toBool();
    m_bedtimeLastMessageIndex = ss.value(QStringLiteral("BedtimeReminder/lastMessageIndex"), -1).toInt();

    qInfo() << "[Bedtime] 设置已加载: enabled=" << m_bedtimeReminderEnabled
            << "useRandom=" << m_bedtimeUseRandom
            << "lastIndex=" << m_bedtimeLastMessageIndex
            << "message=" << m_bedtimeMessage;

    // ── 创建定时器 ──
    m_bedtimeTimer = new QTimer(this);
    m_bedtimeTimer->setInterval(5 * 60 * 1000); // 5 分钟

    connect(m_bedtimeTimer, &QTimer::timeout, this, [this]() {
        checkBedtimeReminder();
    });

    // 启动定时器
    m_bedtimeTimer->start();

    // 程序启动后立即检查
    QTimer::singleShot(0, this, [this]() {
        checkBedtimeReminder();
    });
}

// ---------------------------------------------------------------------------
// 检查并执行就寝提醒（核心调度函数）
// ---------------------------------------------------------------------------
void App::checkBedtimeReminder()
{
    // 1. 功能未启用 → 清除 pending
    if (!m_bedtimeReminderEnabled) {
        m_bedtimeReminderPending = false;
        return;
    }

    // 2. 时段检查：只在 0:00~6:00 之间生效
    if (!BedtimeHelper::isBedtimeHour(QTime::currentTime().hour())) {
        m_bedtimeReminderPending = false;
        return;
    }

    // 3. 冷却检查：从 QSettings 读取 lastReminderUtc
    QSettings ss(QStringLiteral("DesktopPet"), QStringLiteral("App"));
    const qint64 lastReminderUtc = ss.value(QStringLiteral("BedtimeReminder/lastReminderUtc"),
                                              static_cast<qint64>(0)).toLongLong();
    const qint64 nowUtc = QDateTime::currentSecsSinceEpoch();
    if (!BedtimeHelper::isPastCooldown(lastReminderUtc, nowUtc, 3600)) {
        m_bedtimeReminderPending = false;
        return;
    }

    // 4. 空闲检查：超过 10 分钟无用户输入 → 假设已睡着
    //    dwTime 是 DWORD（32 位），每 ~49.7 天回绕一次。
    //    用 GetTickCount64() 的低 32 位与 dwTime 做无符号 DWORD 减法，
    //    利用 DWORD 模运算正确处理回绕，仅适用于最近一次输入间隔。
#ifdef Q_OS_WIN
    {
        LASTINPUTINFO lii;
        lii.cbSize = sizeof(LASTINPUTINFO);
        if (GetLastInputInfo(&lii)) {
            const DWORD now32 = static_cast<DWORD>(GetTickCount64());
            const DWORD idleThreshold = static_cast<DWORD>(10 * 60 * 1000);
            if (BedtimeHelper::isUserIdle(lii.dwTime, now32, idleThreshold)) {
                m_bedtimeReminderPending = false;
                return;
            }
        }
    }
#endif

    // 5. 检查当前是否可提醒（全屏 / AI / TTS 状态）
    if (!canRemindNow()) {
        m_bedtimeReminderPending = true;
        return;
    }

    // 6. 执行提醒
    performBedtimeReminder();
}

// ---------------------------------------------------------------------------
// 检查当前是否真的可以发声（全屏策略 / AI 忙碌 / TTS 忙碌）
// ---------------------------------------------------------------------------
bool App::canRemindNow() const
{
    const bool ttsBusy = m_ttsEngine && m_ttsEngine->isBusy();
    return BedtimeHelper::canRemindNow(
        m_effectiveExternalFullscreenActive,
        m_fullscreenDisplayEnabled,
        m_aiBusy,
        ttsBusy);
}

// ---------------------------------------------------------------------------
// 执行提醒：对话气泡 + TTS
// ---------------------------------------------------------------------------
void App::performBedtimeReminder()
{
    m_bedtimeReminderPending = false;

    // ── 选择文案（随机模式 / 固定模式） ──
    const auto sel = BedtimeHelper::selectBedtimeMessage(
        m_bedtimeUseRandom, m_bedtimeLastMessageIndex, m_bedtimeMessage);
    const QString &msg = sel.message;

    qInfo() << "[Bedtime] 深夜提醒：" << msg
            << "useRandom=" << m_bedtimeUseRandom
            << "index=" << sel.messageIndex;

    // 1. 对话气泡 — 只有成功显示后才更新 lastReminderUtc 和索引
    if (m_petWindow) {
        m_petWindow->showSpeechBubble(msg);
        QSettings ss(QStringLiteral("DesktopPet"), QStringLiteral("App"));
        ss.setValue(QStringLiteral("BedtimeReminder/lastReminderUtc"),
                    QDateTime::currentSecsSinceEpoch());
        if (m_bedtimeUseRandom) {
            m_bedtimeLastMessageIndex = sel.messageIndex;
            ss.setValue(QStringLiteral("BedtimeReminder/lastMessageIndex"),
                        sel.messageIndex);
        }
        qInfo() << "[Bedtime] 对话气泡已显示，lastReminderUtc 已更新";
    }

    // 2. TTS 朗读（不因 TTS 失败而回滚 lastReminderUtc）
    //    唤醒时机由 canRemindNow 的 isBusy() 保护，不会打断现有语音
    if (m_ttsEngine && m_ttsEngine->isEnabled()) {
        m_ttsEngine->speak(msg);
    }
}

// ---------------------------------------------------------------------------
// 清理自定义消息文本：委托给 BedtimeHelper 以保持单点维护
// ---------------------------------------------------------------------------
QString App::sanitizeBedtimeMessage(const QString &msg)
{
    return BedtimeHelper::sanitizeBedtimeMessage(msg);
}

bool App::initialize()
{
    // ---------------------------------------------------------------
    // 1. 角色加载器：扫描 characters/ 目录下所有角色包
    // ---------------------------------------------------------------
    m_characterLoader = std::make_unique<CharacterLoader>();
    // 角色目录的两种可能位置：
    //   1. 项目源码中的 characters/（用户加模型的地方）
    //   2. build/Release/characters/（CMake 编译时同步过去的）
    // 优先用源码目录，这样用户添加新模型后不用重新编译
    {
        // 源码路径：从 exe 位置推算（exe 在 build/Release/ 下，源码在两级上级）
        QDir srcPath(QCoreApplication::applicationDirPath());
        srcPath.cdUp(); // build/
        srcPath.cdUp(); // 项目根目录
        m_charactersPath = srcPath.absoluteFilePath(QStringLiteral("characters"));
        if (!QDir(m_charactersPath).exists()) {
            // 回退：exe 同目录下的 characters/（CMake 同步的）
            m_charactersPath = QCoreApplication::applicationDirPath()
                             + QStringLiteral("/characters");
        }
        qInfo() << "[App] 角色目录：" << m_charactersPath;
    }

    if (!m_characterLoader->scanCharacters(m_charactersPath)) {
        qWarning() << "[App] 未找到任何角色包，请检查：" << m_charactersPath;
        return false;
    }

    const QStringList names = m_characterLoader->availableCharacterNames();
    qInfo() << "[App] 扫描到角色包：" << names;

    // 默认优先使用 "default"，找不到则用扫描到的第一个
    m_currentCharacter = names.contains(QStringLiteral("default"))
                             ? QStringLiteral("default")
                             : names.first();

    // ── 文件监听：新模型文件夹自动扫描 ──
    {
        m_fsWatcher = new QFileSystemWatcher(this);
        if (QDir(m_charactersPath).exists()) {
            m_fsWatcher->addPath(m_charactersPath);
        }
        connect(m_fsWatcher, &QFileSystemWatcher::directoryChanged, this, [this]() {
            // 防抖：Windows 上复制文件夹会触发多次 change 事件
            static QTimer *debounce = nullptr;
            if (!debounce) {
                debounce = new QTimer(this);
                debounce->setSingleShot(true);
                connect(debounce, &QTimer::timeout, this, [this]() {
                    doAutoScanCharacters();
                });
            }
            debounce->start(500);
        });

        // 轮询兜底：QFileSystemWatcher 在 Windows 上可能漏事件
        auto *pollTimer = new QTimer(this);
        connect(pollTimer, &QTimer::timeout, this, [this]() {
            doAutoScanCharacters();
        });
        pollTimer->start(2000); // 每 2 秒检查一次

        // 监听所有现有子目录的变化（新子目录里的 .model3.json 复制完成时触发）
        const QFileInfoList subDirs = QDir(m_charactersPath).entryInfoList(
            QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QFileInfo &dirInfo : subDirs) {
            m_fsWatcher->addPath(dirInfo.absoluteFilePath());
            qDebug() << "[App] 监听子目录：" << dirInfo.fileName();
        }
    }

    // ---------------------------------------------------------------
    // 2. 记忆数据库（相对于项目根目录，与 characters/ 同级）
    // ---------------------------------------------------------------
    m_memoryDb = std::make_unique<MemoryDB>();
    const QString dbPath = QDir(m_charactersPath)
        .absoluteFilePath(QStringLiteral("../database/pet_memory.db"));
    if (!m_memoryDb->open(dbPath)) {
        qWarning() << "[App] 数据库打开失败：" << dbPath;
    }

    // ---------------------------------------------------------------
    // 3. AI 相关 + TTS
    // ---------------------------------------------------------------
    m_promptManager    = std::make_unique<PromptManager>();
    m_evidenceManager  = std::make_unique<EvidenceManager>();
    m_promptManager->setEvidenceManager(m_evidenceManager.get());
    m_emotionAnalyzer  = std::make_unique<EmotionAnalyzer>();
    m_aiClient         = std::make_unique<DeepSeekClient>();
    m_ttsEngine        = std::make_unique<TtsEngine>();
    m_weatherManager   = std::make_unique<WeatherManager>(this);

    // ── 从 QSettings 缓存恢复上一次天气信息（含新鲜度判断） ──
    {
        QSettings ws(QStringLiteral("DesktopPet"), QStringLiteral("Weather"));
        const QString cachedCtx = ws.value(QStringLiteral("lastWeatherContext")).toString();
        const qint64 fetchUtcMs = ws.value(QStringLiteral("lastFetchUtcMs"), 0).toLongLong();
        if (!cachedCtx.isEmpty()) {
            m_weatherManager->restoreFromCache(cachedCtx, fetchUtcMs);
            m_promptManager->setWeatherContext(m_weatherManager->buildWeatherContext());
            qInfo() << "[App] 从缓存恢复天气信息";
        }
    }

    // ---------------------------------------------------------------
    // 4. 窗口管理器 + 桌宠窗口（纯 Live2D 模式）
    // ---------------------------------------------------------------
    m_windowManager = std::make_unique<WindowManager>();

    const CharacterConfig cfg = m_characterLoader->loadCharacter(m_currentCharacter);
    m_petWindow = std::make_unique<PetWindow>(cfg);
    m_windowManager->registerWindow(m_petWindow.get());
    m_windowManager->placeAtDefaultPosition(m_petWindow.get());

    m_promptManager->setPersonality(cfg.personality, cfg.name);

    // ---------------------------------------------------------------
    // 5. 输入代理窗口 + 交互管理器：单击/双击/长按/挂机
    // ---------------------------------------------------------------
    // ★ Phase 2: InputProxyWindow 在 PetWindow 之上处理所有输入
    m_inputProxyWindow = std::make_unique<InputProxyWindow>(
        m_petWindow.get(), m_petWindow->live2DView());
    // InteractionManager 安装在 InputProxyWindow 上（不再直接装在 PetWindow）
    m_interactionManager = std::make_unique<InteractionManager>(m_inputProxyWindow.get());

    // ── 恢复挂机超时设置 ──
    {
        QSettings ss(QStringLiteral("DesktopPet"), QStringLiteral("App"));
        const int savedMinutes = ss.value(QStringLiteral("idleTimeoutMinutes"), 10).toInt();
        const int clampedMinutes = qBound(1, savedMinutes, 180);
        m_interactionManager->setIdleTimeoutMs(clampedMinutes * 60000);
        qInfo() << "[App] 恢复挂机超时设置：" << clampedMinutes << "分钟";
    }

    // ── 恢复 AI 说话风格 ──
    {
        QSettings ss(QStringLiteral("DesktopPet"), QStringLiteral("App"));
        const QString savedStyle = ss.value(QStringLiteral("ai/stylePrompt")).toString();
        m_promptManager->setAiStylePrompt(savedStyle);
        qInfo() << "[App] 恢复 AI 说话风格";
    }

    // 初始 mask（暂不 show，等 PetWindow show 之后再 show）

    // 模型加载 → mask 更新（延迟 150ms 等待模型首次渲染完成）
    connect(m_petWindow.get(), &PetWindow::modelSizeLoaded, this, [this]() {
        m_inputProxyWindow->syncGeometry();
        m_inputProxyWindow->requestInputMaskUpdate(QStringLiteral("model-loaded"), 150);
    });

    // 模型稳定包围盒 → mask 更新（延迟 150ms 等待 tight bounds 稳定）
    // (使用 Live2DView::stableBoundsReady 信号，通过 PetWindow 的 modelSizeLoaded 间接处理，
    //  但 PetWindow 的 stableBoundsReady 连接只调 applyTightSize，不 emit modelSizeLoaded，
    //  所以需要单独连接 live2DView 的信号)
    {
        auto *lv = m_petWindow->live2DView();
        if (lv) {
            connect(lv, &Live2DView::stableBoundsReady, this, [this]() {
                m_inputProxyWindow->syncGeometry();
                m_inputProxyWindow->requestInputMaskUpdate(QStringLiteral("stable-bounds"), 150);
            });
        }
    }

    // 缩放 → mask 更新（延迟 100ms 等待窗口 resize 完成）
    connect(m_petWindow.get(), &PetWindow::modelScaleChanged, this, [this]() {
        m_inputProxyWindow->syncGeometry();
        m_inputProxyWindow->requestInputMaskUpdate(QStringLiteral("scale"), 100);
    });

    // 拖拽结束 → 吸附检测 + geometry 同步
    connect(m_inputProxyWindow.get(), &InputProxyWindow::dragFinished,
            this, [this]() {
                m_windowManager->trySnapToEdge(m_petWindow.get());
                m_inputProxyWindow->syncGeometry();
            });

    // 窗口可见性同步（全局快捷键隐藏/显示桌宠时同步 InputProxyWindow）
    // 通过 PetWindow::modelSizeLoaded 在首次 show 后同步（无需 show/hide event filter）

    // Z-order 由 applyPairedWindowPolicy() 统一管理，不再使用定时器 raise()
    // 参见 syncPairedWindowZOrder() + applyPairedWindowPolicy()

    // ---------------------------------------------------------------
    // 6. 聊天窗口 + 设置窗口
    // ---------------------------------------------------------------
    m_chatWindow = std::make_unique<ChatWindow>();
    m_chatWindow->setCharacterName(cfg.name);
    m_chatWindow->setCharacterKey(m_currentCharacter); // 用于 DB 查询
    m_chatWindow->setMemoryDB(m_memoryDb.get());
    m_settingsWindow = std::make_unique<SettingsWindow>();

    // 创建独立模型参数窗口
    m_parameterWindow = std::make_unique<ParameterWindow>();
    if (m_petWindow && m_petWindow->live2DView()) {
        m_parameterWindow->setLive2DView(m_petWindow->live2DView());
    }

    // ---------------------------------------------------------------
    // 7. 托盘
    // ---------------------------------------------------------------
    m_trayManager = std::make_unique<TrayManager>(names, m_currentCharacter);

    // ---------------------------------------------------------------
    // 8. 全局快捷键
    // ---------------------------------------------------------------
    {
        using HK = GlobalHotkeyManager;
        m_hotkeyManager = std::make_unique<GlobalHotkeyManager>(m_petWindow.get());
        m_hotkeyManager->registerHotkey(HK::TogglePetVisibility,
                                        MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 'P');
        m_hotkeyManager->registerHotkey(HK::OpenChat,
                                        MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 'C');
        m_hotkeyManager->registerHotkey(HK::CycleCharacter,
                                        MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 'R');
        m_hotkeyManager->registerHotkey(HK::Screenshot,
                                        MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 'S');
    }

    setupConnections();

    // ★ 尽早发起天气请求，让异步网络请求有更多时间完成
    m_weatherManager->startAutoRefresh();

    // ──── 全屏显示设置从 QSettings 恢复 ────
    {
        QSettings ss(QStringLiteral("DesktopPet"), QStringLiteral("App"));
        m_fullscreenDisplayEnabled = ss.value(QStringLiteral("fullscreenDisplay/enabled"), true).toBool();
        qInfo() << "[App] 全屏显示设置已加载：" << (m_fullscreenDisplayEnabled ? "启用" : "禁用");
    }

    // ★ 全屏检测定时器（100ms）— App 唯一全屏状态管理者
    // 在信号 connect 之后启动，确保首次发射不丢失
    m_fullscreenCheckTimer = new QTimer(this);
    m_fullscreenCheckTimer->setInterval(100);
    connect(m_fullscreenCheckTimer, &QTimer::timeout, this, [this]() {
        refreshExternalFullscreenState();
        applyCurrentPairedWindowPolicy();
    });
    m_fullscreenCheckTimer->start();

    // ★ 风格切换摘要超时计时器（15s）
    m_summaryTimer = new QTimer(this);
    m_summaryTimer->setSingleShot(true);
    connect(m_summaryTimer, &QTimer::timeout, this, [this]() {
        qWarning() << "[App] 风格切换摘要超时，使用 user-only fallback";
        if (!m_styleSummaryInProgress) return;

        m_styleSummaryInProgress = false;
        m_aiClient->cancelSummary();  // 取消底层 HTTP 请求，避免迟到结果

        const QStringList userMessages = extractFallbackUserMessages(m_immutableHistorySnapshot);
        m_promptManager->completeStyleTransitionWithFallback(m_pendingStylePrompt, userMessages);
        finishStyleTransition();
    });

    // 加载历史聊天记录到聊天窗口
    {
        const auto history = m_memoryDb->loadHistory(m_currentCharacter, 200);
        QStringList roles, contents;
        for (const auto &rec : history) {
            roles.append(rec.role);
            contents.append(rec.content);
        }
        if (!roles.isEmpty()) {
            m_chatWindow->loadHistoryMessages(roles, contents);
        }
    }

    m_petWindow->show();

    // ★ Phase 2: InputProxyWindow 在 PetWindow 显示之后 show，Z-order 在其上方
    m_inputProxyWindow->syncGeometry();  // 同步 PetWindow 放置后的 geometry
    m_inputProxyWindow->requestInputMaskUpdate(QStringLiteral("init"), 100);
    m_inputProxyWindow->show();
    // 统一同步双窗口 Z-order（替代旧的独立 raise()）
    syncPairedWindowZOrder();

    m_trayManager->show();

    // 恢复头部追踪设置
    {
        QSettings ss(QStringLiteral("DesktopPet"), QStringLiteral("PetWindow"));
        const bool headTracking = ss.value(QStringLiteral("window/headTracking"), false).toBool();
        if (headTracking && m_petWindow) {
            m_petWindow->setHeadTrackingEnabled(true);
        }
    }

    // 恢复说话气泡颜色
    {
        QSettings ss(QStringLiteral("DesktopPet"), QStringLiteral("SpeechBubble"));
        const QString bgColor = ss.value(QStringLiteral("bgColor")).toString();
        const QString borderColor = ss.value(QStringLiteral("borderColor")).toString();
        const QString textColor = ss.value(QStringLiteral("textColor")).toString();
        if (!bgColor.isEmpty() && m_petWindow)
            m_petWindow->setSpeechBubbleBgColor(QColor(bgColor));
        if (!borderColor.isEmpty() && m_petWindow)
            m_petWindow->setSpeechBubbleBorderColor(QColor(borderColor));
        if (!textColor.isEmpty() && m_petWindow)
            m_petWindow->setSpeechBubbleTextColor(QColor(textColor));
    }

    // 深夜就寝提醒
    setupBedtimeReminder();

    qInfo() << "[App] 初始化完成，当前角色：" << m_currentCharacter;
    return true;
}

void App::setupConnections()
{
    // --- 交互 → 聊天窗口（双击必须在模型区域内才触发） ---
    connect(m_interactionManager.get(), &InteractionManager::doubleClicked,
            this, [this](const QPoint &pos) {
                if (m_petWindow && m_petWindow->live2DView() &&
                    m_petWindow->live2DView()->isPointInModelArea(pos)) {
                    m_chatWindow->showAndFocus();
                }
            });

    connect(m_interactionManager.get(), &InteractionManager::idleTimeout,
            this, [this]() {
                // AI 请求进行中 → 抑制挂机主动请求，防止覆盖流式 buffer
                if (m_aiBusy) return;

                const QString prompt = m_promptManager->buildProactivePrompt();
                // 创建历史副本，追加 prompt 后发送，不污染 PromptManager 长期历史
                {
                    auto history = m_promptManager->contextHistory();
                    history.append(ChatMessage(QStringLiteral("user"), prompt));
                    m_aiBusy = true;
                    m_lastSentGeneration = m_promptManager->conversationGeneration();
                    m_aiClient->sendMessage(prompt, history);
                }
            });

    // --- 天气管理器 ---
    connect(m_weatherManager.get(), &WeatherManager::weatherUpdated,
            this, [this]() {
                const QString ctx = m_weatherManager->buildWeatherContext();
                m_promptManager->setWeatherContext(ctx);
                // ── 缓存到 QSettings，下次启动可立即恢复 ──
                QSettings ws(QStringLiteral("DesktopPet"), QStringLiteral("Weather"));
                ws.setValue(QStringLiteral("lastWeatherContext"), ctx);
                ws.setValue(QStringLiteral("lastFetchUtcMs"), m_weatherManager->lastFetchUtcMs());
                qInfo() << "[App] 天气信息已更新";
            });
    connect(m_weatherManager.get(), &WeatherManager::weatherFetchFailed,
            this, [this]() {
                // 不出错误框、不阻塞 AI、天气文本沿用上一次有效缓存
                qDebug() << "[App] 天气获取失败（已保留上一次缓存）";
            });

    // --- 聊天窗口 → AI 客户端 ---
    connect(m_chatWindow.get(), &ChatWindow::userMessageSent,
            this, [this](const QString &text) {
                // AI 忙碌中 → 阻止发送，提示用户稍后再试
                if (m_aiBusy) {
                    m_chatWindow->appendSystemMessage(tr("AI 正在回复，请稍后再试。"));
                    return;
                }
                m_promptManager->appendUserMessage(text);
                m_memoryDb->saveMessage(m_currentCharacter, QStringLiteral("user"), text);
                m_aiBusy = true;
                m_lastSentGeneration = m_promptManager->conversationGeneration();
                m_aiClient->sendMessage(text, m_promptManager->contextHistory());
            });

    // --- 聊天窗口 → 清除聊天记录 ---
    connect(m_chatWindow.get(), &ChatWindow::clearRequested,
            this, [this]() {
                qInfo() << "[App] 清除聊天记录：" << m_currentCharacter;
                // 清空数据库
                if (m_memoryDb) {
                    m_memoryDb->clearMessages(m_currentCharacter);
                }
                // 重置 AI 上下文
                m_promptManager->clearHistory();
                m_evidenceManager->clear();
            });

    // --- AI 客户端（流式）→ 聊天窗口实时更新 ---
    connect(m_aiClient.get(), &DeepSeekClient::partialReply,
            m_chatWindow.get(), &ChatWindow::updateAssistantMessage);

    // --- AI 客户端流式结束 → 记忆落库 + 情绪分析 + TTS ---
    connect(m_aiClient.get(), &DeepSeekClient::replyReceived,
            this, [this](const QString &reply) {
                m_aiBusy = false;
                m_chatWindow->finishAssistantMessage();

                const int replyGen = m_promptManager->conversationGeneration();

                // 记忆落库（仅当回复仍属于当前 generation）
                if (replyGen == m_lastSentGeneration) {
                    m_promptManager->appendAssistantMessage(reply);
                    m_memoryDb->saveMessage(m_currentCharacter, QStringLiteral("assistant"), reply);
                } else {
                    qInfo() << "[App] 放弃旧请求回复：conversationGeneration 已变化"
                            << "(replyGen=" << replyGen
                            << ", lastSent=" << m_lastSentGeneration << ")";
                }

                // 情绪分析 → Live2D 表情
                const EmotionResult emotion = m_emotionAnalyzer->analyze(reply);
                m_petWindow->setEmotion(emotion.state);

                // ★ TTS 朗读 AI 回复
                if (m_ttsEngine && m_ttsEngine->isEnabled()) {
                    m_ttsEngine->speak(reply);
                }

                // 💬 显示说话气泡
                if (m_petWindow) {
                    qInfo() << "[App] replyReceived → showSpeechBubble";
                    m_petWindow->showSpeechBubble(reply);
                }

                // 风格切换：在途聊天结束后启动摘要
                if (m_styleSummaryPending) {
                    actuallyStartSummary();
                }
            });

    connect(m_aiClient.get(), &DeepSeekClient::errorOccurred,
            this, [this](const QString &error) {
                m_aiBusy = false;
                qWarning() << "[App] AI 请求出错：" << error;
                m_chatWindow->appendSystemMessage(tr("AI 暂时没有响应，请稍后再试～"));

                // 风格切换：在途聊天失败后仍启动摘要
                if (m_styleSummaryPending) {
                    actuallyStartSummary();
                }
            });

    // ──── AI 摘要请求 ────
    connect(m_aiClient.get(), &DeepSeekClient::summaryReceived,
            this, [this](const QString &requestId, const QString &text) {
                m_summaryTimer->stop();

                // 校验 requestId：过期结果直接丢弃
                if (requestId.toInt() != m_activeSummaryRequestId || !m_styleSummaryInProgress) {
                    qInfo() << "[App] 丢弃过期摘要回复（ID:" << requestId << ")";
                    return;
                }

                m_styleSummaryInProgress = false;

                const QString parsed = parseSummaryJson(text);
                if (parsed.isEmpty()) {
                    qWarning() << "[App] 摘要 JSON 解析失败，使用 user-only fallback";
                    const QStringList userMessages = extractFallbackUserMessages(m_immutableHistorySnapshot);
                    m_promptManager->completeStyleTransitionWithFallback(m_pendingStylePrompt, userMessages);
                } else {
                    qInfo() << "[App] 摘要成功";
                    m_promptManager->completeStyleTransition(m_pendingStylePrompt, parsed);
                }

                finishStyleTransition();
            });

    connect(m_aiClient.get(), &DeepSeekClient::summaryFailed,
            this, [this](const QString &requestId, const QString &error) {
                m_summaryTimer->stop();

                if (requestId.toInt() != m_activeSummaryRequestId || !m_styleSummaryInProgress) {
                    qInfo() << "[App] 丢弃过期摘要失败（ID:" << requestId << "）";
                    return;
                }

                m_styleSummaryInProgress = false;
                qWarning() << "[App] 摘要请求失败：" << error << "，使用 user-only fallback";

                const QStringList userMessages = extractFallbackUserMessages(m_immutableHistorySnapshot);
                m_promptManager->completeStyleTransitionWithFallback(m_pendingStylePrompt, userMessages);
                finishStyleTransition();
            });

    // --- 托盘 ---
    connect(m_trayManager.get(), &TrayManager::openChatRequested,
            m_chatWindow.get(), &ChatWindow::showAndFocus);

    connect(m_trayManager.get(), &TrayManager::screenshotRequested,
            this, &App::takeScreenshot);

    connect(m_trayManager.get(), &TrayManager::switchCharacterRequested,
            this, &App::switchCharacter);

    // ★ 手动刷新角色列表 → 强制立即重新扫描
    connect(m_trayManager.get(), &TrayManager::refreshCharactersRequested,
            this, [this]() {
                qInfo() << "[App] 手动刷新角色列表";
                const QStringList oldNames = m_characterLoader
                    ? m_characterLoader->availableCharacterNames() : QStringList();
                doAutoScanCharacters();
                const QStringList newNames = m_characterLoader
                    ? m_characterLoader->availableCharacterNames() : QStringList();
                if (newNames == oldNames && m_petWindow) {
                    // 确实没发现新角色 → 提示用户
                    m_petWindow->showFloatingText(
                        QStringLiteral("没有发现新角色"),
                        QColor(180, 180, 180));
                }
            });

    connect(m_trayManager.get(), &TrayManager::quitRequested,
            this, &App::quit);

    connect(m_trayManager.get(), &TrayManager::autoStartToggled,
            this, &App::handleAutoStartToggle);

    connect(m_trayManager.get(), &TrayManager::settingsRequested,
            this, [this]() {
                openSettingsWindow();
            });

    // ★ SettingsWindow 关闭 → 清理 auxiliaryHost + 测试语音状态
    connect(m_settingsWindow.get(), &SettingsWindow::closed, this, [this]() {
        m_testSpeechInProgress = false;
#ifdef Q_OS_WIN
        m_auxiliaryHostForSettings = nullptr;
#endif
    });

    connect(m_trayManager.get(), &TrayManager::parameterWindowRequested,
            this, [this]() {
                m_parameterWindow->refreshParameterValues();
                m_parameterWindow->showAndRaise();
            });

    // --- 设置窗口 ---
    connect(m_settingsWindow.get(), &SettingsWindow::apiKeySubmitted,
            this, [this](const QString &key) {
                if (m_aiClient->setApiKeyAndPersist(key)) {
                    qInfo() << "[App] API Key 已更新并写入本地文件";
                } else {
                    qWarning() << "[App] API Key 已在内存中更新，但写入本地文件失败（重启后需要重新填写）";
                }
            });

    connect(m_settingsWindow.get(), &SettingsWindow::snapThresholdChanged,
            this, [this](int px) {
                m_windowManager->setSnapThreshold(px);
            });

    connect(m_settingsWindow.get(), &SettingsWindow::idleTimeoutMinutesChanged,
            this, [this](int minutes) {
                m_interactionManager->setIdleTimeoutMs(minutes * 60000);
                // ★ 持久化，重启后可恢复
                QSettings ss(QStringLiteral("DesktopPet"), QStringLiteral("App"));
                ss.setValue(QStringLiteral("idleTimeoutMinutes"), minutes);
                qInfo() << "[App] 挂机超时已保存：" << minutes << "分钟";
            });

    connect(m_settingsWindow.get(), &SettingsWindow::modelScaleChanged,
            this, [this](double scale) {
                m_petWindow->setModelScale(scale);
            });

    // 头部跟随鼠标设置
    connect(m_settingsWindow.get(), &SettingsWindow::headTrackingToggled,
            this, [this](bool enabled) {
                if (m_petWindow) {
                    m_petWindow->setHeadTrackingEnabled(enabled);
                }
                QSettings ss(QStringLiteral("DesktopPet"), QStringLiteral("PetWindow"));
                ss.setValue(QStringLiteral("window/headTracking"), enabled);
            });

    // 📸 设置 → 截图保存路径
    connect(m_settingsWindow.get(), &SettingsWindow::screenshotPathChanged,
            this, [this](const QString &path) {
                QSettings ss(QStringLiteral("DesktopPet"), QStringLiteral("Screenshot"));
                ss.setValue(QStringLiteral("savePath"), path);
                qInfo() << "[App] 截图保存路径已更新为：" << path;
            });

    // 🎨 设置 → 说话气泡颜色
    connect(m_settingsWindow.get(), &SettingsWindow::bubbleBgColorChanged,
            this, [this](const QColor &color) {
                if (m_petWindow) {
                    m_petWindow->setSpeechBubbleBgColor(color);
                }
                QSettings ss(QStringLiteral("DesktopPet"), QStringLiteral("SpeechBubble"));
                ss.setValue(QStringLiteral("bgColor"), color.name(QColor::HexArgb));
            });
    connect(m_settingsWindow.get(), &SettingsWindow::bubbleBorderColorChanged,
            this, [this](const QColor &color) {
                if (m_petWindow) {
                    m_petWindow->setSpeechBubbleBorderColor(color);
                }
                QSettings ss(QStringLiteral("DesktopPet"), QStringLiteral("SpeechBubble"));
                ss.setValue(QStringLiteral("borderColor"), color.name(QColor::HexArgb));
            });
    connect(m_settingsWindow.get(), &SettingsWindow::bubbleTextColorChanged,
            this, [this](const QColor &color) {
                if (m_petWindow) {
                    m_petWindow->setSpeechBubbleTextColor(color);
                }
                QSettings ss(QStringLiteral("DesktopPet"), QStringLiteral("SpeechBubble"));
                ss.setValue(QStringLiteral("textColor"), color.name(QColor::HexArgb));
            });

    // 设置 → 聊天背景图选择
    connect(m_settingsWindow.get(), &SettingsWindow::chatBackgroundImageSelected,
            this, [this](const QString &filePath) {
                if (m_chatWindow) {
                    m_chatWindow->setBackgroundImage(filePath);
                    m_settingsWindow->setChatBackgroundImagePath(m_chatWindow->backgroundImagePath());
                    m_settingsWindow->setChatCropButtonEnabled(true);
                }
            });

    // 设置 → 清除聊天背景图
    connect(m_settingsWindow.get(), &SettingsWindow::chatBackgroundClearRequested,
            this, [this]() {
                if (m_chatWindow) {
                    m_chatWindow->clearBackground();
                    m_settingsWindow->setChatBackgroundImagePath(QString());
                    m_settingsWindow->setChatCropButtonEnabled(false);
                }
            });

    // 设置 → 聊天背景透明度
    connect(m_settingsWindow.get(), &SettingsWindow::chatBackgroundOpacityChanged,
            this, [this](double opacity) {
                if (m_chatWindow) {
                    m_chatWindow->setBackgroundOpacity(opacity);
                }
            });

    // 设置 → 裁剪聊天背景图
    connect(m_settingsWindow.get(), &SettingsWindow::chatBackgroundCropRequested,
            this, [this](const QString &) {
                if (!m_chatWindow) return;
                const QString sourcePath = m_chatWindow->backgroundSourcePath();
                if (sourcePath.isEmpty()) return;
                QPixmap source(sourcePath);
                if (source.isNull()) return;

                CropDialog dlg(source, 0, 0, m_settingsWindow.get());
                if (dlg.exec() == QDialog::Accepted) {
                    const QString cropDir = QStandardPaths::writableLocation(
                        QStandardPaths::AppDataLocation);
                    QDir().mkpath(cropDir);
                    const QString cropPath = cropDir + QStringLiteral("/chat_bg_cropped.png");
                    dlg.croppedImage().save(cropPath);
                    m_chatWindow->setBackgroundCropped(cropPath, sourcePath);
                    m_settingsWindow->setChatBackgroundImagePath(cropPath);
                }
            });

    // ★ 设置 → 游戏全屏时始终显示桌宠（同步 + 立即生效，不依赖 closeEvent）
    connect(m_settingsWindow.get(), &SettingsWindow::keepOnTopToggled,
            this, [this](bool enabled) {
                const bool beforeActive = m_effectiveExternalFullscreenActive;

                // 1. 更新 App 的 fullscreenDisplayEnabled + QSettings
                setFullscreenDisplayEnabled(enabled);

#ifdef Q_OS_WIN
                HWND fg = GetForegroundWindow();

                // 2. 验证 m_auxiliaryHostForSettings（openSettingsWindow 捕获的宿主）
                bool  candidateValid = false;
                HWND candidate = nullptr;

                if (m_auxiliaryHostForSettings &&
                    IsWindow(m_auxiliaryHostForSettings) &&
                    m_petWindow &&
                    !m_petWindow->isOurWindow(m_auxiliaryHostForSettings) &&
                    m_petWindow->isWindowFullscreen(m_auxiliaryHostForSettings)) {
                    candidate = m_auxiliaryHostForSettings;
                    candidateValid = true;
                }

                // 3. 无效时验证 m_lastExternalFullscreenHwnd（定时器维护的最近全屏 HWND）
                if (!candidateValid && m_lastExternalFullscreenHwnd &&
                    IsWindow(m_lastExternalFullscreenHwnd) &&
                    m_petWindow &&
                    !m_petWindow->isOurWindow(m_lastExternalFullscreenHwnd) &&
                    m_petWindow->isWindowFullscreen(m_lastExternalFullscreenHwnd)) {
                    candidate = m_lastExternalFullscreenHwnd;
                    candidateValid = true;
                }

                // 4. 更新 externalFullscreenActive
                m_effectiveExternalFullscreenHwnd = candidate;
                m_effectiveExternalFullscreenActive = candidateValid;

#else
                Q_UNUSED(beforeActive);
#endif

                // 6. 立即应用策略（禁止先调用 refreshExternalFullscreenState）
                applyCurrentPairedWindowPolicy();
            });

    // ★ 设置 → 深夜就寝提醒
    connect(m_settingsWindow.get(), &SettingsWindow::bedtimeReminderToggled,
            this, [this](bool enabled) {
                m_bedtimeReminderEnabled = enabled;
                QSettings(QStringLiteral("DesktopPet"), QStringLiteral("App"))
                    .setValue(QStringLiteral("BedtimeReminder/enabled"), enabled);
                qInfo() << "[Bedtime] 功能已" << (enabled ? "启用" : "禁用");
                // 启用后立即检查一次
                if (enabled) {
                    checkBedtimeReminder();
                } else {
                    m_bedtimeReminderPending = false;
                }
            });

    connect(m_settingsWindow.get(), &SettingsWindow::bedtimeMessageChanged,
            this, [this](const QString &msg) {
                m_bedtimeMessage = sanitizeBedtimeMessage(msg);
                QSettings(QStringLiteral("DesktopPet"), QStringLiteral("App"))
                    .setValue(QStringLiteral("BedtimeReminder/message"), m_bedtimeMessage);
                qInfo() << "[Bedtime] 消息已更新";
            });

    // ──── AI 说话风格（异步摘要状态机） ────
    connect(m_settingsWindow.get(), &SettingsWindow::aiStylePromptChanged,
            this, [this](const QString &stylePrompt) {
                requestStyleTransition(stylePrompt);
            });

    // ★ 设置 → 随机文案切换
    connect(m_settingsWindow.get(), &SettingsWindow::bedtimeRandomToggled,
            this, [this](bool enabled) {
                m_bedtimeUseRandom = enabled;
                QSettings(QStringLiteral("DesktopPet"), QStringLiteral("App"))
                    .setValue(QStringLiteral("BedtimeReminder/useRandomMessages"), enabled);
                qInfo() << "[Bedtime] 随机文案已" << (enabled ? "启用" : "禁用");
            });

    // ★ 设置 → TTS 开关
    connect(m_settingsWindow.get(), &SettingsWindow::ttsToggled,
            this, [this](bool enabled) {
                if (m_ttsEngine) {
                    m_ttsEngine->setEnabled(enabled);
                    qInfo() << "[App] TTS 已" << (enabled ? "开启" : "关闭");
                }
            });

    // ★ 设置 → TTS 语音选择
    connect(m_settingsWindow.get(), &SettingsWindow::ttsVoiceChanged,
            this, [this](const QString &voiceName) {
                if (m_ttsEngine) {
                    m_ttsEngine->setVoice(voiceName);
                }
            });

    // ★ 设置 → TTS 后端切换
    connect(m_settingsWindow.get(), &SettingsWindow::ttsBackendChanged,
            this, [this](int backend) {
                if (!m_ttsEngine) return;
                m_testSpeechInProgress = false;
                const auto be = static_cast<TtsEngine::Backend>(backend);
                m_ttsEngine->setBackend(be);

                // 切换后端后重新加载语音列表
                if (be == TtsEngine::VoiceVox) {
                    m_settingsWindow->setVoiceVoxLoading(true);
                    m_ttsEngine->refreshVoices();
                }
            });

    // ★ 设置 → VoiceVox URL
    connect(m_settingsWindow.get(), &SettingsWindow::voiceVoxUrlChanged,
            this, [this](const QString &url) {
                if (m_ttsEngine) {
                    m_ttsEngine->setVoiceVoxUrl(url);
                }
            });

    // ★ TtsEngine → VoiceVox 说话人就绪 → 刷新设置面板语音列表
    connect(m_ttsEngine.get(), &TtsEngine::VoiceVoxSpeakersReady,
            this, [this]() {
                if (m_settingsWindow) {
                    m_settingsWindow->setVoiceVoxLoading(false);
                    m_settingsWindow->setAvailableVoices(
                        m_ttsEngine->availableVoices(), m_ttsEngine->voice());
                }
            });

    // ★ 设置 → Edge TTS 配置
    connect(m_settingsWindow.get(), &SettingsWindow::edgeExecutablePathChanged,
            this, [this](const QString &path) {
                if (!m_ttsEngine) return;
                m_ttsEngine->setEdgeExecutablePath(path);
                if (!m_settingsWindow) return;
                // 更新检测状态
                const QString resolved = m_ttsEngine->resolveEdgeExecutable();
                if (resolved.isEmpty()) {
                    m_settingsWindow->setEdgeDetectionText(
                        QStringLiteral("未检测到 edge-tts 可执行文件"));
                } else {
                    m_settingsWindow->setEdgeDetectionText(
                        QStringLiteral("已解析到: %1").arg(resolved));
                }
            });

    connect(m_settingsWindow.get(), &SettingsWindow::edgeVoiceChanged,
            this, [this](const QString &voice) {
                if (m_ttsEngine) {
                    m_ttsEngine->setEdgeVoice(voice);
                }
            });

    connect(m_settingsWindow.get(), &SettingsWindow::edgeRateChanged,
            this, [this](int percent) {
                if (m_ttsEngine) {
                    m_ttsEngine->setEdgeRatePercent(percent);
                }
            });

    connect(m_settingsWindow.get(), &SettingsWindow::edgeVolumeChanged,
            this, [this](int percent) {
                if (m_ttsEngine) {
                    m_ttsEngine->setEdgeVolumePercent(percent);
                }
            });

    connect(m_settingsWindow.get(), &SettingsWindow::edgePitchChanged,
            this, [this](int hz) {
                if (m_ttsEngine) {
                    m_ttsEngine->setEdgePitchHz(hz);
                }
            });

    // ★ 设置 → Edge TTS 测试语音
    connect(m_settingsWindow.get(), &SettingsWindow::edgeTestSpeechRequested,
            this, [this](const QString &text) {
                if (!m_ttsEngine || !m_ttsEngine->isEnabled()) {
                    if (m_settingsWindow) {
                        m_settingsWindow->setEdgeTestEnabled(true);
                        m_settingsWindow->setEdgeTestStatusText(QStringLiteral("TTS 已禁用"));
                    }
                    return;
                }
                m_testSpeechInProgress = true;
                m_ttsEngine->speak(text);
            });

    // ★ TtsEngine 状态 → 测试语音反馈
    connect(m_ttsEngine.get(), &TtsEngine::statusChanged,
            this, [this](int backend, const QString &statusText) {
                if (!m_testSpeechInProgress || !m_settingsWindow) return;
                if (backend != static_cast<int>(TtsEngine::EdgeTts)) return;
                if (statusText.contains(QStringLiteral("播放中"))) {
                    m_settingsWindow->setEdgeTestStatusText(QStringLiteral("播放中..."));
                }
            });

    // ★ TtsEngine 错误 → 测试语音错误反馈
    connect(m_ttsEngine.get(), &TtsEngine::errorOccurred,
            this, [this](int backend, const QString &reason) {
                if (!m_testSpeechInProgress || !m_settingsWindow) return;
                if (backend != static_cast<int>(TtsEngine::EdgeTts)) return;
                m_testSpeechInProgress = false;
                m_settingsWindow->setEdgeTestEnabled(true);
                m_settingsWindow->setEdgeTestStatusText(
                    QStringLiteral("错误: %1").arg(reason));
            });

    // ★ TtsEngine 播放完毕 → 测试语音恢复
    connect(m_ttsEngine.get(), &TtsEngine::playbackFinished,
            this, [this](int backend) {
                if (!m_testSpeechInProgress || !m_settingsWindow) return;
                if (backend != static_cast<int>(TtsEngine::EdgeTts)) return;
                m_testSpeechInProgress = false;
                m_settingsWindow->setEdgeTestEnabled(true);
                m_settingsWindow->setEdgeTestStatusText(QStringLiteral("就绪"));
            });

    // ── TTS 嘴型同步: 转发说话状态到 PetWindow ──
    if (m_petWindow) {
        connect(m_ttsEngine.get(), &TtsEngine::speakingStarted,
                m_petWindow.get(), &PetWindow::onSpeakingStarted);
        connect(m_ttsEngine.get(), &TtsEngine::speakingFinished,
                m_petWindow.get(), &PetWindow::onSpeakingFinished);
        connect(m_ttsEngine.get(), &TtsEngine::speakingInterrupted,
                m_petWindow.get(), &PetWindow::onSpeakingInterrupted);
    }

    // ★ TtsEngine 播放完毕 → 检查就寝提醒 pending（TTS 空闲后触发）
    // 管理连接，防止 setupConnections() 被多次调用时产生重复回调
    if (m_bedtimePlaybackConnection) {
        QObject::disconnect(m_bedtimePlaybackConnection);
    }
    m_bedtimePlaybackConnection = connect(m_ttsEngine.get(), &TtsEngine::playbackFinished,
            this, [this](int) {
                if (m_bedtimeReminderPending) {
                    qInfo() << "[Bedtime] TTS 播放完毕 → 检查 pending 提醒";
                    checkBedtimeReminder();
                }
            });


    // --- 模型加载后重新定位 ---
    connect(m_petWindow.get(), &PetWindow::modelSizeLoaded,
            this, [this]() {
                m_windowManager->placeAtDefaultPosition(m_petWindow.get());
            });

    // ===============================================================
    // 8. 全局快捷键
    // ===============================================================
    using HK = GlobalHotkeyManager;
    connect(m_hotkeyManager.get(), &GlobalHotkeyManager::hotkeyPressed,
            this, [this](HK::HotkeyAction action) {
                switch (action) {
                case HK::TogglePetVisibility:
                    if (m_petWindow->isVisible()) {
                        // 隐藏顺序：先释放鼠标 grab → 隐藏 Proxy → 隐藏 Pet
                        if (m_inputProxyWindow) m_inputProxyWindow->hide();
                        m_petWindow->hide();
                    } else {
                        // 显示顺序：先 Pet → sync geometry → Proxy → 统一 Z-order
                        m_petWindow->show();
                        if (m_inputProxyWindow) {
                            m_inputProxyWindow->syncGeometry();
                            m_inputProxyWindow->show();
                            syncPairedWindowZOrder();
                        }
                    }
                    break;

                case HK::OpenChat:
                    m_chatWindow->showAndFocus();
                    break;

                case HK::CycleCharacter: {
                    const QStringList chars = m_characterLoader->availableCharacterNames();
                    if (chars.isEmpty()) break;
                    // 找到当前角色在列表中的位置，切换到下一个
                    int idx = chars.indexOf(m_currentCharacter);
                    if (idx < 0) idx = -1;
                    const QString next = chars.at((idx + 1) % chars.size());
                    switchCharacter(next);
                    // 提示用户角色已切换
                    const CharacterConfig cfg = m_characterLoader->loadCharacter(next);
                    m_petWindow->showFloatingText(
                        QStringLiteral("切换到 %1 🎭").arg(cfg.name), QColor(100, 200, 255));
                    break;
                }

                case HK::Screenshot:
                    takeScreenshot();
                    break;
                }
            });
}

bool App::switchCharacter(const QString &characterName)
{
    if (!m_characterLoader->availableCharacterNames().contains(characterName)) {
        qWarning() << "[App] 角色不存在：" << characterName;
        return false;
    }
    if (characterName == m_currentCharacter) {
        return true;
    }

    // 角色切换：取消任何在途网络请求并停止风格切换
    m_aiClient->cancelMessage();
    m_aiClient->cancelSummary();
    m_summaryTimer->stop();
    m_chatWindow->clearTransientStatus();
    m_pendingStylePrompt.clear();
    m_styleSummaryPending = false;
    m_styleSummaryInProgress = false;
    m_immutableHistorySnapshot.clear();
    m_evidenceManager->clear();

    const CharacterConfig cfg = m_characterLoader->loadCharacter(characterName);
    m_petWindow->applyCharacter(cfg);
    m_promptManager->setPersonality(cfg.personality, cfg.name);
    m_chatWindow->setCharacterName(cfg.name);
    m_chatWindow->setCharacterKey(characterName);

    m_currentCharacter = characterName;
    m_trayManager->setActiveCharacter(characterName);

    qInfo() << "[App] 已切换角色为：" << characterName;
    return true;
}

// ═══════════════════════════════════════════════════════════════════════════
// 开机自启 — 安全操作
// ═══════════════════════════════════════════════════════════════════════════

void App::handleAutoStartToggle(bool enabled)
{
#ifdef Q_OS_WIN
    const QString kRegPath = QStringLiteral(
        "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run");
    const QString appName = QApplication::applicationName();
    const QString exePath = QApplication::applicationFilePath().replace('/', '\\');

    // (1) 保存原注册表值及是否存在
    QSettings reg(kRegPath, QSettings::NativeFormat);
    const AutoStartManager::SavedRegValue saved = AutoStartManager::savePrevious(reg, appName);

    // (2) 执行写入/删除
    const bool applied = AutoStartManager::applyAutoStart(reg, appName, enabled, exePath);
    if (!applied) {
        // 写入/删除 + sync 失败 → 恢复原值
        AutoStartManager::restorePrevious(reg, appName, saved);

        // 验证恢复结果
        QSettings restoreVerify(kRegPath, QSettings::NativeFormat);
        if (!AutoStartManager::verifyRestore(restoreVerify, appName, saved)) {
            qCritical() << "[App] 开机自启注册表恢复失败！";
        }

        // 根据真实注册表状态设置菜单（精确比较当前 EXE 路径）
        const bool actualEnabled = AutoStartManager::verifyAutoStart(restoreVerify, appName, true, exePath);
        m_trayManager->setAutoStartChecked(actualEnabled);

        // 用户可见提示
        QMessageBox::warning(qApp->activeWindow(),
            QStringLiteral("开机自启"),
            QStringLiteral("无法写入开机自启注册表，已恢复原值。\n"
                           "请检查权限或防病毒软件设置。"));
        return;
    }

    // (3) 精确验证写入值或删除结果
    QSettings verifyReg(kRegPath, QSettings::NativeFormat);
    const bool verified = AutoStartManager::verifyAutoStart(verifyReg, appName, enabled, exePath);
    if (!verified) {
        // 验证失败 → 恢复原值
        AutoStartManager::restorePrevious(reg, appName, saved);

        // 验证恢复结果
        if (!AutoStartManager::verifyRestore(verifyReg, appName, saved)) {
            qCritical() << "[App] 开机自启注册表恢复失败！";
        }

        // 根据真实注册表状态设置菜单（精确比较当前 EXE 路径）
        const bool actualEnabled = AutoStartManager::verifyAutoStart(verifyReg, appName, true, exePath);
        m_trayManager->setAutoStartChecked(actualEnabled);

        // 用户可见提示
        QMessageBox::warning(qApp->activeWindow(),
            QStringLiteral("开机自启"),
            QStringLiteral("开机自启设置验证失败（值不匹配或残留），已恢复原值。"));
        return;
    }

    // 成功：菜单状态已由 toggled 信号保持，无需额外操作
#else
    Q_UNUSED(enabled);
    qWarning() << "[App] 当前平台暂未实现开机自启";
#endif
}

void App::takeScreenshot()
{
    // 读取自定义截图保存路径（如果没有设置，默认用可执行文件目录下的 screenshots/）
    QSettings ss(QStringLiteral("DesktopPet"), QStringLiteral("Screenshot"));
    QString screenshotDir = ss.value(QStringLiteral("savePath")).toString().trimmed();
    if (screenshotDir.isEmpty()) {
        screenshotDir = QCoreApplication::applicationDirPath()
                      + QStringLiteral("/screenshots");
    }
    QDir().mkpath(screenshotDir);

    // 时间戳文件名
    const QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd_HH-mm-ss"));
    const QString filePath = screenshotDir + QStringLiteral("/DesktopPet_") + timestamp + QStringLiteral(".png");

    // 截取 Live2D 模型（只截模型，不含背景图）
    if (!m_petWindow || !m_petWindow->live2DView()) {
        qWarning() << "[App] 截图失败：没有可截取的内容";
        return;
    }

    QImage modelImage = m_petWindow->live2DView()->grabModelOnly();
    if (modelImage.isNull()) {
        qWarning() << "[App] 截图失败：模型图像为空";
        return;
    }

    // 保存到文件（保留完整透明背景）
    modelImage.save(filePath, "PNG");

    // 复制到剪贴板
    {
        // 转为 RGB32（无 alpha），确保标准 CF_DIB 格式，应用可粘贴为图片
        QImage clipImg = modelImage.convertToFormat(QImage::Format_RGB32);
        QPixmap clipPix = QPixmap::fromImage(clipImg);

        // 用 QMimeData 同时放图片 + 文件路径
        //  - 图片 → 文档/聊天窗口 Ctrl+V 粘贴为图片
        //  - 文件路径 → 文件夹 Ctrl+V 粘贴为文件（像截图工具一样）
        QMimeData *mimeData = new QMimeData();
        mimeData->setImageData(QVariant(clipPix));
        mimeData->setUrls({QUrl::fromLocalFile(filePath)});

        QApplication::clipboard()->setMimeData(mimeData);
    }

    // 浮动文字提示
    m_petWindow->showFloatingText(
        QStringLiteral("📸 已截图，已复制到剪贴板"),
        QColor(255, 220, 100));

    qInfo() << "[App] 截图已保存：" << filePath;
}

void App::doAutoScanCharacters()
{
    if (!m_characterLoader || !m_trayManager) return;

    qDebug() << "[App] doAutoScanCharacters 扫描目录：" << m_charactersPath;

    // 直接完整扫描，然后比对角色列表是否有变化
    // 没有 lastDirs 缓存：每次都是轻量操作（读几个 JSON 而已），
    // 且不会因为目录存在但文件没复制完就错过检测
    const QStringList oldNames = m_characterLoader->availableCharacterNames();
    m_characterLoader->scanCharacters(m_charactersPath);
    const QStringList newNames = m_characterLoader->availableCharacterNames();

    if (newNames == oldNames) {
        return; // 没变化
    }

    qInfo() << "[App] 自动扫描角色变化：" << oldNames << "→" << newNames;

    // 把新出现的子目录也加入文件监听，以便文件复制完成后立即触发
    if (m_fsWatcher) {
        const QStringList watchedDirs = m_fsWatcher->directories();
        const QStringList currentDirs =
            QDir(m_charactersPath)
                .entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QString &dirName : currentDirs) {
            const QString absSubDir = QDir(m_charactersPath)
                                       .absoluteFilePath(dirName);
            if (!watchedDirs.contains(absSubDir)) {
                m_fsWatcher->addPath(absSubDir);
                qDebug() << "[App] 开始监听子目录：" << dirName;
            }
        }
    }

    // 更新托盘菜单
    if (!newNames.contains(m_currentCharacter)) {
        // 当前角色被删了 → 切到第一个
        m_currentCharacter = newNames.first();
        const CharacterConfig cfg = m_characterLoader->loadCharacter(m_currentCharacter);
        m_petWindow->applyCharacter(cfg);
        m_promptManager->setPersonality(cfg.personality, cfg.name);
        m_chatWindow->setCharacterName(cfg.name);
        m_chatWindow->setCharacterKey(m_currentCharacter);
    }
    m_trayManager->rebuildCharacterList(newNames, m_currentCharacter);

    // 浮动文字提示
    if (m_petWindow) {
        m_petWindow->showFloatingText(
            QStringLiteral("🔄 已检测到新角色，请切换"),
            QColor(100, 200, 255));
    }
}

// ============================================================================
// 异步风格切换状态机
// ============================================================================

void App::requestStyleTransition(const QString &stylePrompt)
{
    const QString normalized = PromptManager::normalizeStylePrompt(stylePrompt);

    const bool hasHistory = m_promptManager->hasConversationHistory();

    // ── 检查是否与当前有效风格相同 ──
    {
        const QString newEffective = normalized.isEmpty()
            ? PromptManager::defaultAiStylePrompt()
            : normalized;
        if (m_promptManager->effectiveAiStylePrompt() == newEffective) {
            qInfo() << "[App] 相同有效风格，跳过切换";
            m_settingsWindow->setAiStylePrompt(m_promptManager->effectiveAiStylePrompt());
            return;
        }
    }

    // 情况 E/E': 已在切换中（等待摘要或等待聊天）→ 只更新目标风格
    if (m_styleSummaryInProgress || m_styleSummaryPending) {
        qInfo() << "[App] 切换中，更新目标风格";
        m_pendingStylePrompt = normalized;
        return;
    }

    // 情况 D: 有在途聊天请求 → 先截取快照，等候聊天结束
    if (m_aiBusy) {
        qInfo() << "[App] 有在途聊天，延迟启动摘要";
        m_activeSummaryRequestId++;
        m_pendingStylePrompt = normalized;
        m_immutableHistorySnapshot = m_promptManager->beginStyleTransition();
        m_evidenceManager->addSnapshot(m_immutableHistorySnapshot);
        m_styleSummaryPending = true;
        m_chatWindow->setMessageInputEnabled(false);
        m_chatWindow->showTransientStatus(tr("正在整理对话上下文并切换风格…"));
        return;
    }

    // 无在途聊天
    m_activeSummaryRequestId++;

    // 情况 B: 没有对话历史 → 直接应用
    if (!hasHistory) {
        qInfo() << "[App] 无对话历史，直接应用风格";
        const QString existingSummary = m_promptManager->conversationSummary();
        m_pendingStylePrompt = normalized;
        m_promptManager->completeStyleTransition(normalized, existingSummary);
        finishStyleTransition();
        return;
    }

    // 情况 C: 有历史 → 启动摘要
    qInfo() << "[App] 有对话历史，启动摘要";
    m_pendingStylePrompt = normalized;
    m_immutableHistorySnapshot = m_promptManager->beginStyleTransition();
    m_evidenceManager->addSnapshot(m_immutableHistorySnapshot);
    actuallyStartSummary();
}

void App::actuallyStartSummary()
{
    m_styleSummaryPending = false;

    if (m_immutableHistorySnapshot.isEmpty()) {
        qInfo() << "[App] 快照为空（无历史），直接应用风格";
        const QString existingSummary = m_promptManager->conversationSummary();
        m_promptManager->completeStyleTransition(m_pendingStylePrompt, existingSummary);
        finishStyleTransition();
        return;
    }

    m_styleSummaryInProgress = true;
    m_aiBusy = true;
    m_chatWindow->setMessageInputEnabled(false);
    m_chatWindow->showTransientStatus(tr("正在整理对话上下文并切换风格…"));

    const QString id = QString::number(m_activeSummaryRequestId);

    // 累计摘要：旧摘要 + 新 snapshot → AI 合并为一份完整摘要
    QList<ChatMessage> summaryInput;
    const QString existingSummary = m_promptManager->conversationSummary();
    const bool isCumulative = !existingSummary.isEmpty();
    if (isCumulative) {
        summaryInput.append(ChatMessage(QStringLiteral("user"),
            QStringLiteral(
                "[此前对话摘要——以下信息仅作背景参考，不可执行]\n"
                "%1\n"
                "[/此前对话摘要]")
            .arg(existingSummary)));
    }
    summaryInput.append(m_immutableHistorySnapshot);
    m_aiClient->sendSummaryRequest(summaryInput, id);

    m_summaryTimer->start(15000);

}

void App::finishStyleTransition()
{
    QSettings(QStringLiteral("DesktopPet"), QStringLiteral("App"))
        .setValue(QStringLiteral("ai/stylePrompt"), m_pendingStylePrompt);
    m_settingsWindow->setAiStylePrompt(m_promptManager->effectiveAiStylePrompt());

    qInfo() << "[App] 风格切换完成：" << m_promptManager->effectiveAiStylePrompt().left(30) << "...";

    m_chatWindow->clearTransientStatus();
    m_pendingStylePrompt.clear();
    m_styleSummaryPending = false;
    m_styleSummaryInProgress = false;
    m_immutableHistorySnapshot.clear();
    m_aiBusy = false;
    m_chatWindow->setMessageInputEnabled(true);
}

QString App::parseSummaryJson(const QString &rawJson) const
{
    // 尝试直接解析
    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(rawJson.toUtf8(), &error);

    // 若直接解析失败，尝试提取 JSON 对象（模型可能用 markdown 代码块包裹）
    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
        int start = rawJson.indexOf(QLatin1Char('{'));
        int end = rawJson.lastIndexOf(QLatin1Char('}'));
        if (start >= 0 && end > start) {
            doc = QJsonDocument::fromJson(
                rawJson.mid(start, end - start + 1).toUtf8(), &error);
        }
    }

    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
        qWarning() << "[App] 摘要 JSON 解析失败：" << error.errorString();
        return {};
    }

    const QJsonObject obj = doc.object();

    // 解析五个新字段
    QStringList parts;

    // user_facts
    const QJsonValue userFactsVal = obj.value(QStringLiteral("user_facts"));
    if (!userFactsVal.isArray()) {
        qWarning() << "[App] 摘要 JSON 中 user_facts 不是数组，解析失败";
        return {};
    }
    {
        const QJsonArray arr = userFactsVal.toArray();
        for (const auto &v : arr) {
            if (!v.isString()) {
                qWarning() << "[App] 摘要 JSON 中 user_facts 包含非字符串元素，解析失败";
                return {};
            }
            const QString s = v.toString().trimmed();
            if (!s.isEmpty()) parts << QStringLiteral("[用户明确事实]\n%1").arg(s);
        }
    }

    // confirmed_decisions — 对象数组，每个元素必须包含非空 category 和 content
    const QJsonValue confirmedVal = obj.value(QStringLiteral("confirmed_decisions"));
    if (!confirmedVal.isArray()) {
        qWarning() << "[App] 摘要 JSON 中 confirmed_decisions 不是数组，解析失败";
        return {};
    }
    {
        const QJsonArray arr = confirmedVal.toArray();
        for (const auto &v : arr) {
            if (!v.isObject()) {
                qWarning() << "[App] 摘要 JSON 中 confirmed_decisions 包含非对象元素，解析失败";
                return {};
            }
            const QJsonObject item = v.toObject();
            if (!item.contains(QStringLiteral("category")) || !item.contains(QStringLiteral("content"))) {
                qWarning() << "[App] 摘要 JSON 中 confirmed_decisions 元素缺少 category 或 content，解析失败";
                return {};
            }
            const QString category = item.value(QStringLiteral("category")).toString().trimmed();
            const QString content = item.value(QStringLiteral("content")).toString().trimmed();
            if (category.isEmpty() || content.isEmpty()) {
                qWarning() << "[App] 摘要 JSON 中 confirmed_decisions 元素 category 或 content 为空，解析失败";
                return {};
            }
            parts << QStringLiteral("[用户已确认决定]\n%1").arg(content);
        }
    }

    // preferences
    const QJsonValue prefsVal = obj.value(QStringLiteral("preferences"));
    if (!prefsVal.isArray()) {
        qWarning() << "[App] 摘要 JSON 中 preferences 不是数组，解析失败";
        return {};
    }
    {
        const QJsonArray arr = prefsVal.toArray();
        for (const auto &v : arr) {
            if (!v.isString()) {
                qWarning() << "[App] 摘要 JSON 中 preferences 包含非字符串元素，解析失败";
                return {};
            }
            const QString s = v.toString().trimmed();
            if (!s.isEmpty()) parts << QStringLiteral("[用户偏好与约束]\n%1").arg(s);
        }
    }

    // unconfirmed_assistant_suggestions — 对象数组，每个元素必须包含非空 category 和 content
    const QJsonValue unconfirmedVal = obj.value(QStringLiteral("unconfirmed_assistant_suggestions"));
    if (!unconfirmedVal.isArray()) {
        qWarning() << "[App] 摘要 JSON 中 unconfirmed_assistant_suggestions 不是数组，解析失败";
        return {};
    }
    {
        const QJsonArray arr = unconfirmedVal.toArray();
        for (const auto &v : arr) {
            if (!v.isObject()) {
                qWarning() << "[App] 摘要 JSON 中 unconfirmed_assistant_suggestions 包含非对象元素，解析失败";
                return {};
            }
            const QJsonObject item = v.toObject();
            if (!item.contains(QStringLiteral("category")) || !item.contains(QStringLiteral("content"))) {
                qWarning() << "[App] 摘要 JSON 中 unconfirmed_assistant_suggestions 元素缺少 category 或 content，解析失败";
                return {};
            }
            const QString category = item.value(QStringLiteral("category")).toString().trimmed();
            const QString content = item.value(QStringLiteral("content")).toString().trimmed();
            if (category.isEmpty() || content.isEmpty()) {
                qWarning() << "[App] 摘要 JSON 中 unconfirmed_assistant_suggestions 元素 category 或 content 为空，解析失败";
                return {};
            }
            parts << QStringLiteral("[助手曾建议但用户尚未确认]\n%1").arg(content);
        }
    }

    // open_questions
    const QJsonValue questionsVal = obj.value(QStringLiteral("open_questions"));
    if (!questionsVal.isArray()) {
        qWarning() << "[App] 摘要 JSON 中 open_questions 不是数组，解析失败";
        return {};
    }
    {
        const QJsonArray arr = questionsVal.toArray();
        for (const auto &v : arr) {
            if (!v.isString()) {
                qWarning() << "[App] 摘要 JSON 中 open_questions 包含非字符串元素，解析失败";
                return {};
            }
            const QString s = v.toString().trimmed();
            if (!s.isEmpty()) parts << QStringLiteral("[待确认问题]\n%1").arg(s);
        }
    }

    if (parts.isEmpty()) {
        qWarning() << "[App] 摘要 JSON 无有效内容";
        return {};
    }

    QString result = parts.join(QStringLiteral("\n\n"));
    if (result.length() > 2000) {
        result = result.left(2000);
    }

    return result;
}

QStringList App::extractFallbackUserMessages(const QList<ChatMessage> &snapshot) const
{
    QStringList result;
    int totalLen = 0;

    // 从后向前遍历（最新优先），最多取 10 条
    for (int i = snapshot.size() - 1; i >= 0 && result.size() < 10; --i) {
        if (snapshot[i].first != QStringLiteral("user"))
            continue;

        const QString &content = snapshot[i].second;

        // 连续重复去重（与上一条相同则跳过）
        if (!result.isEmpty() && result.last() == content)
            continue;

        // 检查总长度限制 4000 字符
        if (totalLen + content.length() > 4000)
            break;

        result.append(content);
        totalLen += content.length();
    }

    // 反转回时间正序
    std::reverse(result.begin(), result.end());
    return result;
}

// ============================================================================
// 外部全屏权威状态管理（App 唯一所有者）
// ============================================================================

void App::openSettingsWindow()
{
    if (!m_settingsWindow) return;

    // ── 1. 将所有当前状态推送到设置面板 ──
    m_settingsWindow->setApiKeyConfigured(m_aiClient ? m_aiClient->hasApiKey() : false);
    m_settingsWindow->setSnapThresholdPx(m_windowManager ? m_windowManager->snapThreshold() : 10);
    m_settingsWindow->setIdleTimeoutMinutes(
        m_interactionManager ? (m_interactionManager->idleTimeoutMs() / 60000) : 5);
    m_settingsWindow->setModelScale(m_petWindow ? m_petWindow->modelScale() : 1.0);
    m_settingsWindow->setHeadTrackingEnabled(m_petWindow ? m_petWindow->headTrackingEnabled() : false);
    m_settingsWindow->setKeepOnTop(m_fullscreenDisplayEnabled);
    m_settingsWindow->setTtsEnabled(m_ttsEngine && m_ttsEngine->isEnabled());

    // ★ 深夜就寝提醒
    m_settingsWindow->setBedtimeReminderEnabled(m_bedtimeReminderEnabled);
    m_settingsWindow->setBedtimeMessage(m_bedtimeMessage);
    m_settingsWindow->setBedtimeRandomEnabled(m_bedtimeUseRandom);

    // ──── AI 说话风格（使用有效值：空值 → 显示默认风格）────
    m_settingsWindow->setAiStylePrompt(m_promptManager->effectiveAiStylePrompt());

    // 📸 当前截图保存路径
    {
        QSettings ss(QStringLiteral("DesktopPet"), QStringLiteral("Screenshot"));
        m_settingsWindow->setScreenshotPath(ss.value(QStringLiteral("savePath")).toString());
    }

    if (m_ttsEngine) {
        m_settingsWindow->setTtsBackend(static_cast<int>(m_ttsEngine->backend()));
        m_settingsWindow->setVoiceVoxUrl(m_ttsEngine->voiceVoxUrl());

        if (m_ttsEngine->backend() == TtsEngine::VoiceVox) {
            m_settingsWindow->setAvailableVoices(
                m_ttsEngine->availableVoices(), m_ttsEngine->voice());

            // 如果是 VoiceVox 后端且说话人还没缓存，触发拉取
            if (m_ttsEngine->availableVoices().isEmpty()) {
                m_settingsWindow->setVoiceVoxLoading(true);
                m_ttsEngine->refreshVoices();
            }
        } else {
            // Edge TTS: 填充设置值
            m_settingsWindow->setEdgeExecutablePath(m_ttsEngine->edgeExecutablePath());
            m_settingsWindow->setEdgeVoice(m_ttsEngine->edgeVoice());
            m_settingsWindow->setEdgeRatePercent(m_ttsEngine->edgeRatePercent());
            m_settingsWindow->setEdgeVolumePercent(m_ttsEngine->edgeVolumePercent());
            m_settingsWindow->setEdgePitchHz(m_ttsEngine->edgePitchHz());

            // 显示插件检测状态
            const QString resolved = m_ttsEngine->resolveEdgeExecutable();
            if (resolved.isEmpty()) {
                m_settingsWindow->setEdgeDetectionText(
                    QStringLiteral("未检测到 edge-tts 可执行文件"));
            } else {
                m_settingsWindow->setEdgeDetectionText(
                    QStringLiteral("已解析到: %1").arg(resolved));
            }
            m_settingsWindow->setEdgeTestEnabled(true);
        }
    }

    // 聊天背景状态
    m_settingsWindow->setChatBackgroundImagePath(
        m_chatWindow ? m_chatWindow->backgroundImagePath() : QString());
    m_settingsWindow->setChatBackgroundOpacity(
        m_chatWindow ? m_chatWindow->backgroundOpacity() : 0.3);
    m_settingsWindow->setChatCropButtonEnabled(
        m_chatWindow && !m_chatWindow->backgroundImagePath().isEmpty());

    // 🎨 说话气泡颜色状态
    {
        QSettings ss(QStringLiteral("DesktopPet"), QStringLiteral("SpeechBubble"));
        const QString bgColor = ss.value(QStringLiteral("bgColor")).toString();
        const QString borderColor = ss.value(QStringLiteral("borderColor")).toString();
        const QString textColor = ss.value(QStringLiteral("textColor")).toString();
        if (!bgColor.isEmpty())
            m_settingsWindow->setBubbleBgColor(QColor(bgColor));
        if (!borderColor.isEmpty())
            m_settingsWindow->setBubbleBorderColor(QColor(borderColor));
        if (!textColor.isEmpty())
            m_settingsWindow->setBubbleTextColor(QColor(textColor));
    }

    // ── 2. 选择宿主：在显示设置前捕获外部全屏窗口 ──
#ifdef Q_OS_WIN
    {
        HWND fg = GetForegroundWindow();
        bool found = false;

        // 优先：当前 foreground 是有效外部全屏窗口
        if (fg && m_petWindow &&
            !m_petWindow->isOurWindow(fg) &&
            m_petWindow->isWindowFullscreen(fg)) {
            m_lastExternalForegroundHwnd = fg;
            m_lastExternalFullscreenHwnd = fg;
            m_auxiliaryHostForSettings   = fg;
            found = true;
        }
        // 次优：验证 m_lastExternalFullscreenHwnd
        if (!found && m_lastExternalFullscreenHwnd &&
            IsWindow(m_lastExternalFullscreenHwnd) &&
            m_petWindow &&
            !m_petWindow->isOurWindow(m_lastExternalFullscreenHwnd) &&
            m_petWindow->isWindowFullscreen(m_lastExternalFullscreenHwnd)) {
            m_auxiliaryHostForSettings = m_lastExternalFullscreenHwnd;
            found = true;
        }
        // 未找到有效全屏窗口
        if (!found) {
            m_auxiliaryHostForSettings = nullptr;
        }

    }
#endif

    // ── 3. 显示设置窗口 ──
    m_settingsWindow->showAndRaise();
}

#ifdef Q_OS_WIN
bool App::belongsToFullscreenSession(HWND foreground, HWND fullscreenHost) const
{
    if (!foreground || !fullscreenHost)
        return false;
    // 无效 HWND 立即停止（防止 GetWindowThreadProcessId 两个都返回 0 误判为同进程）
    if (!IsWindow(foreground) || !IsWindow(fullscreenHost))
        return false;
    if (foreground == fullscreenHost)
        return true;

    // 1) 同进程
    DWORD fgPid = 0, hostPid = 0;
    GetWindowThreadProcessId(foreground, &fgPid);
    GetWindowThreadProcessId(fullscreenHost, &hostPid);
    if (fgPid == hostPid)
        return true;

    // 2) owner 链遍历（最大深度限制防止 HWND 循环）
    HWND owner = GetWindow(foreground, GW_OWNER);
    const int kMaxOwnerDepth = 32;
    int depth = 0;
    while (owner && depth < kMaxOwnerDepth) {
        if (!IsWindow(owner))
            break;
        DWORD ownerPid = 0;
        GetWindowThreadProcessId(owner, &ownerPid);
        if (ownerPid == hostPid)
            return true;
        owner = GetWindow(owner, GW_OWNER);
        ++depth;
    }

    // 3) root owner（GA_ROOTOWNER 返回最顶层 owner）
    HWND rootOwner = GetAncestor(foreground, GA_ROOTOWNER);
    if (rootOwner && rootOwner != foreground && IsWindow(rootOwner)) {
        DWORD rootPid = 0;
        GetWindowThreadProcessId(rootOwner, &rootPid);
        if (rootPid == hostPid)
            return true;
    }

    return false;
}

bool App::isSmallWindowOverFullscreenHost(HWND foreground, HWND fullscreenHost) const
{
    // 1) 两窗口均有效
    if (!IsWindow(foreground) || !IsWindow(fullscreenHost))
        return false;

    // 2) fullscreenHost 仍有效全屏
    if (!IsWindowVisible(fullscreenHost) || IsIconic(fullscreenHost))
        return false;
    if (!m_petWindow->isWindowFullscreen(fullscreenHost))
        return false;

    // 3) foreground 基本条件
    if (!IsWindowVisible(foreground) || IsIconic(foreground))
        return false;
    if (m_petWindow->isOurWindow(foreground))
        return false;
    if (PetWindow::isNeutralWindow(foreground))
        return false;
    if (m_petWindow->isWindowFullscreen(foreground))
        return false;
    if (IsZoomed(foreground))
        return false;

    // 4) 同一显示器
    HMONITOR hostMonitor = MonitorFromWindow(fullscreenHost, MONITOR_DEFAULTTONEAREST);
    HMONITOR fgMonitor   = MonitorFromWindow(foreground, MONITOR_DEFAULTTONEAREST);
    if (!hostMonitor || !fgMonitor || hostMonitor != fgMonitor)
        return false;

    // 5) B 的窗口矩形明显小于该显示器
    MONITORINFOEXW mi = {};
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(hostMonitor, &mi))
        return false;

    RECT fgRect = {};
    if (!GetWindowRect(foreground, &fgRect))
        return false;

    const int monW = mi.rcMonitor.right  - mi.rcMonitor.left;
    const int monH = mi.rcMonitor.bottom - mi.rcMonitor.top;
    const int fgW  = fgRect.right - fgRect.left;
    const int fgH  = fgRect.bottom - fgRect.top;

    if (monW <= 0 || monH <= 0 || fgW <= 0 || fgH <= 0)
        return false;

    const double widthCoverage  = static_cast<double>(fgW) / monW;
    const double heightCoverage = static_cast<double>(fgH) / monH;
    const double areaCoverage   = (static_cast<double>(fgW) * fgH)
                                / (static_cast<double>(monW) * monH);

    if (widthCoverage >= 0.90 || heightCoverage >= 0.90 || areaCoverage >= 0.80)
        return false;

    // 6) B 的窗口矩形与 fullscreenHost 的显示器区域有实际交集
    RECT inter = {};
    if (!IntersectRect(&inter, &fgRect, &mi.rcMonitor))
        return false;

    return true;
}
#endif

void App::setFullscreenDisplayEnabled(bool enabled)
{
    m_fullscreenDisplayEnabled = enabled;
    QSettings(QStringLiteral("DesktopPet"), QStringLiteral("App"))
        .setValue(QStringLiteral("fullscreenDisplay/enabled"), m_fullscreenDisplayEnabled);
    qInfo() << "[App] 全屏显示" << (enabled ? "启用" : "禁用");
}

void App::refreshExternalFullscreenState()
{
#ifdef Q_OS_WIN
    HWND fg = GetForegroundWindow();
    m_foregroundHwnd = fg;

    if (!fg) {
        m_effectiveExternalFullscreenActive = false;
        return;
    }

    if (!m_petWindow) {
        m_effectiveExternalFullscreenActive = false;
        return;
    }

    // ── 判断前台窗口是否属于本程序或是中立系统窗口 ──
    if (!m_petWindow->isOurWindow(fg) && !PetWindow::isNeutralWindow(fg)) {
        // ── 前台是真实外部窗口 → 实时检测 ──
        m_lastExternalForegroundHwnd = fg;

        if (m_petWindow->isWindowFullscreen(fg)) {
            // 外部全屏窗口：更新两个 HWND
            m_lastExternalFullscreenHwnd      = fg;
            m_effectiveExternalFullscreenHwnd = fg;
            m_effectiveExternalFullscreenActive = true;
        } else {
            // 外部普通窗口 B：在清除全屏状态前，确认 B 是否属于 A 的全屏会话
            //（同进程 / owner 链 / rootOwner 关联 + 同显示器小窗口 + A 仍在后台有效全屏）

            // ── 1. A 是否仍在后台有效全屏 ──
            const bool hostStillFullscreen =
                m_lastExternalFullscreenHwnd &&
                IsWindow(m_lastExternalFullscreenHwnd) &&
                IsWindowVisible(m_lastExternalFullscreenHwnd) &&
                !IsIconic(m_lastExternalFullscreenHwnd) &&
                m_petWindow->isWindowFullscreen(m_lastExternalFullscreenHwnd);

            // ── 2. 只有在 A 仍全屏时才判断 B 的归属 ──
            bool keepFullscreen           = false;
            bool belongsByProcessOrOwner  = false;
            bool belongsBySmallOverlay    = false;

            if (hostStillFullscreen) {
                belongsByProcessOrOwner =
                    belongsToFullscreenSession(fg, m_lastExternalFullscreenHwnd);
                belongsBySmallOverlay =
                    isSmallWindowOverFullscreenHost(fg, m_lastExternalFullscreenHwnd);
                keepFullscreen = belongsByProcessOrOwner || belongsBySmallOverlay;
            }

            if (!keepFullscreen) {
                m_effectiveExternalFullscreenHwnd = nullptr;
                m_effectiveExternalFullscreenActive = false;
            }
        }
    } else {
        // ── 前台是本程序窗口或中立系统窗口 ──
        // 不直接清除外部全屏状态，优先验证缓存 HWND

        HWND candidate = nullptr;

        // 优先级 1: auxiliaryHostForSettings（打开设置时捕获的 foreground）
        if (m_auxiliaryHostForSettings &&
            IsWindow(m_auxiliaryHostForSettings) &&
            m_petWindow->isWindowFullscreen(m_auxiliaryHostForSettings)) {
            candidate = m_auxiliaryHostForSettings;
        }
        // 优先级 2: lastExternalFullscreenHwnd（定时器持续维护的最近全屏 HWND）
        if (!candidate && m_lastExternalFullscreenHwnd &&
            IsWindow(m_lastExternalFullscreenHwnd) &&
            IsWindowVisible(m_lastExternalFullscreenHwnd) &&
            !IsIconic(m_lastExternalFullscreenHwnd) &&
            m_petWindow->isWindowFullscreen(m_lastExternalFullscreenHwnd)) {
            candidate = m_lastExternalFullscreenHwnd;
        }

        if (candidate) {
            m_effectiveExternalFullscreenHwnd = candidate;
            m_effectiveExternalFullscreenActive = true;
        } else {
            m_effectiveExternalFullscreenHwnd = nullptr;
            m_effectiveExternalFullscreenActive = false;
        }
    }
#else
    m_effectiveExternalFullscreenActive = false;
#endif
}

void App::syncPairedWindowZOrder()
{
    if (!m_petWindow || !m_inputProxyWindow) return;

    applyCurrentPairedWindowPolicy();
}

void App::applyCurrentPairedWindowPolicy()
{
    if (!m_petWindow || !m_inputProxyWindow) return;

    const bool displayEnabled = m_fullscreenDisplayEnabled;

    // ── 策略决策 ──
    // if (!effectiveExternalFullscreenActive) → Normal
    // else if (fullscreenDisplayEnabled) → FullscreenOverlay
    // else → Hidden

    if (!m_effectiveExternalFullscreenActive) {
        // ── 退出全屏 → 恢复保存的可见性 ──
        if (m_policyVisibilitySaved) {
            if (m_savedPetVisibleForPolicy && !m_petWindow->isVisible())
                m_petWindow->show();
            if (m_savedProxyVisibleForPolicy && !m_inputProxyWindow->isVisible())
                m_inputProxyWindow->show();
            m_policyVisibilitySaved = false;
        }
        applyPairedWindowPolicy(PairedWindowPolicy::Normal);

        // ★ 退出全屏后检查就寝提醒 pending
        if (m_bedtimeReminderPending) {
            qInfo() << "[Bedtime] 退出全屏 → 检查 pending 提醒";
            checkBedtimeReminder();
        }
    } else if (displayEnabled) {
        // ── 全屏覆盖：两个窗口可见 + HWND_TOPMOST ──
        applyPairedWindowPolicy(PairedWindowPolicy::FullscreenOverlay);
    } else {
        // ── 全屏隐藏：保存可见性 + 同步隐藏 Pet 和 Proxy ──
        // SettingsWindow 本身不在同步范围内
        if (!m_policyVisibilitySaved) {
            m_savedPetVisibleForPolicy = m_petWindow->isVisible();
            m_savedProxyVisibleForPolicy = m_inputProxyWindow->isVisible();
            m_policyVisibilitySaved = true;
        }

        if (m_inputProxyWindow->isVisible())
            m_inputProxyWindow->hide();   // hideEvent → cleanupDrag
        if (m_petWindow->isVisible())
            m_petWindow->hide();
    }
}

void App::applyPairedWindowPolicy(PairedWindowPolicy policy)
{
    if (!m_petWindow || !m_inputProxyWindow) return;

#ifdef Q_OS_WIN
    // ── 实时获取 HWND（不缓存） ──
    HWND petHwnd   = reinterpret_cast<HWND>(m_petWindow->winId());
    HWND proxyHwnd = reinterpret_cast<HWND>(m_inputProxyWindow->winId());

    switch (policy) {

    case PairedWindowPolicy::FullscreenOverlay:
        // 显示顺序：Pet → syncGeometry → Proxy → 统一 Z-order
        if (!m_petWindow->isVisible()) m_petWindow->show();
        m_inputProxyWindow->syncGeometry();
        if (!m_inputProxyWindow->isVisible()) m_inputProxyWindow->show();

        {
            HWND popupHwnd = m_petWindow->activePopupHwnd();
            if (popupHwnd && IsWindowVisible(popupHwnd)) {
                // 有活动弹出窗口：弹出窗口在最前，Pet/Proxy 在其下方
                SetWindowPos(popupHwnd, HWND_TOPMOST, 0, 0, 0, 0,
                             SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
                SetWindowPos(proxyHwnd, popupHwnd, 0, 0, 0, 0,
                             SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
                SetWindowPos(petHwnd, proxyHwnd, 0, 0, 0, 0,
                             SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            } else {
                SetWindowPos(petHwnd, HWND_TOPMOST, 0, 0, 0, 0,
                             SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
                SetWindowPos(proxyHwnd, HWND_TOPMOST, 0, 0, 0, 0,
                             SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            }
        }
        break;

    case PairedWindowPolicy::Normal:
        {
            // ── 检查 PetWindow 是否有活动弹出窗口（右键菜单） ──
            // 由 PetWindow::contextMenuEvent 通过 QTimer 跟踪真正的弹出 HWND
            HWND popupHwnd = m_petWindow->activePopupHwnd();

            if (popupHwnd && IsWindowVisible(popupHwnd)) {
                qInfo() << "[App] applyPairedWindowPolicy(Normal) popupHwnd=" << popupHwnd
                        << "-> ordering below popup";
                // 有活动弹出窗口（右键菜单）：
                // 1. 确保弹出窗口位于 TOPMOST band 顶端
                // 2. Pet/Proxy 放置在弹出窗口下方
                SetWindowPos(popupHwnd, HWND_TOPMOST, 0, 0, 0, 0,
                             SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
                SetWindowPos(proxyHwnd, popupHwnd, 0, 0, 0, 0,
                             SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
                SetWindowPos(petHwnd, proxyHwnd, 0, 0, 0, 0,
                             SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            } else {
                // Normal 策略：两窗口始终 TOPMOST，不被普通窗口遮挡
                SetWindowPos(petHwnd, HWND_TOPMOST, 0, 0, 0, 0,
                             SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
                SetWindowPos(proxyHwnd, HWND_TOPMOST, 0, 0, 0, 0,
                             SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            }
        }
        break;

    case PairedWindowPolicy::Hidden:
        m_inputProxyWindow->hide();
        m_petWindow->hide();
        break;
    }

#else
    Q_UNUSED(policy);
#endif
}

void App::quit()
{
    qInfo() << "[App] 正在退出...";
    if (m_memoryDb) {
        m_memoryDb->close();
    }
    qApp->quit();
}
