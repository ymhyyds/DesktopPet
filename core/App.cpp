#include "App.h"
#include "WindowManager.h"

#include "../pet/PetWindow.h"
#include "../pet/CharacterLoader.h"

#include "../ai/DeepSeekClient.h"
#include "../ai/PromptManager.h"
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
#include "GlobalHotkeyManager.h"

#include <QApplication>

#ifdef Q_OS_WIN
#include <windows.h>
#endif
#include <QDebug>
#include <QDir>
#include <QPainter>
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

App::App(QObject *parent)
    : QObject(parent)
{
}

App::~App() = default;

void App::fetchWeather()
{
    auto *net = new QNetworkAccessManager(this);
    QNetworkRequest req{QUrl(QStringLiteral("https://wttr.in?format=%C+%t&lang=zh"))};
    req.setRawHeader("User-Agent", "curl/8.0");
    QNetworkReply *reply = net->get(req);

    connect(reply, &QNetworkReply::finished, this, [this, reply, net]() {
        if (reply->error() == QNetworkReply::NoError) {
            const QString weather = QString::fromUtf8(reply->readAll()).trimmed();
            if (!weather.isEmpty()) {
                m_weatherDescription = weather;
                m_promptManager->setWeatherDescription(weather);
                qInfo() << "[App] 获取到天气信息：" << weather;
            }
        } else {
            qDebug() << "[App] 获取天气失败：" << reply->errorString();
        }
        reply->deleteLater();
        net->deleteLater();
    });
}

// ---------------------------------------------------------------------------
// 深夜就寝提醒：0:00~6:00 间每隔 5 分钟检查一次，
// 如果系统未进入空闲状态（用户还在用电脑），
// 用 TTS 语音 + 浮动文字提醒主人该睡觉了。
// 每晚只提醒一次。
// ---------------------------------------------------------------------------
void App::setupBedtimeReminder()
{
    m_bedtimeTimer = new QTimer(this);
    m_bedtimeTimer->setInterval(5 * 60 * 1000); // 5 分钟

    connect(m_bedtimeTimer, &QTimer::timeout, this, [this]() {
        const int hour = QTime::currentTime().hour();

        // 只在 0:00~6:00 之间检查
        if (hour < 0 || hour >= 6) {
            // 不在深夜时段 → 重置提醒标记，等下一个深夜
            m_bedtimeReminded = false;
            return;
        }

        // 今晚已经提醒过了，不再重复
        if (m_bedtimeReminded) return;

        // 检查系统空闲时间（Windows GetLastInputInfo）
#ifdef Q_OS_WIN
        LASTINPUTINFO lii;
        lii.cbSize = sizeof(LASTINPUTINFO);
        if (GetLastInputInfo(&lii)) {
            const DWORD now = GetTickCount();
            const DWORD idleMs = now - lii.dwTime;
            // 如果超过 10 分钟没有输入 → 认为已离开/睡着，不打扰
            if (idleMs > 10 * 60 * 1000) return;
        }
#endif

        // 触发提醒
        m_bedtimeReminded = true;
        const QString msg = QStringLiteral("主人该上床睡觉了喵");

        qInfo() << "[Bedtime] 深夜提醒：" << msg;

        // TTS 语音朗读
        if (m_ttsEngine && m_ttsEngine->isEnabled()) {
            m_ttsEngine->speak(msg);
        }

        // 浮动文字
        if (m_petWindow) {
            m_petWindow->showFloatingText(msg, QColor(180, 180, 255));
        }
    });

    // 启动定时器（立即开始，第一次触发在 5 分钟后）
    m_bedtimeTimer->start();
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
    m_promptManager   = std::make_unique<PromptManager>();
    m_emotionAnalyzer = std::make_unique<EmotionAnalyzer>();
    m_aiClient        = std::make_unique<DeepSeekClient>();
    m_ttsEngine       = std::make_unique<TtsEngine>();

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

    // 初始 mask（暂不 show，等 PetWindow show 之后再 show）

    // 模型加载 → mask 更新
    connect(m_petWindow.get(), &PetWindow::modelSizeLoaded, this, [this]() {
        m_inputProxyWindow->syncGeometry();
        m_inputProxyWindow->updateInputMask();
    });

    // 模型稳定包围盒 → mask 更新
    // (使用 Live2DView::stableBoundsReady 信号，通过 PetWindow 的 modelSizeLoaded 间接处理，
    //  但 PetWindow 的 stableBoundsReady 连接只调 applyTightSize，不 emit modelSizeLoaded，
    //  所以需要单独连接 live2DView 的信号)
    {
        auto *lv = m_petWindow->live2DView();
        if (lv) {
            connect(lv, &Live2DView::stableBoundsReady, this, [this]() {
                m_inputProxyWindow->syncGeometry();
                m_inputProxyWindow->updateInputMask();
            });
        }
    }

    // 缩放 → mask 更新
    connect(m_petWindow.get(), &PetWindow::modelScaleChanged, this, [this]() {
        m_inputProxyWindow->syncGeometry();
        m_inputProxyWindow->updateInputMask();
    });

    // 拖拽结束 → 吸附检测 + geometry 同步
    connect(m_inputProxyWindow.get(), &InputProxyWindow::dragFinished,
            this, [this]() {
                m_windowManager->trySnapToEdge(m_petWindow.get());
                m_inputProxyWindow->syncGeometry();
            });

    // 窗口可见性同步（全局快捷键隐藏/显示桌宠时同步 InputProxyWindow）
    // 通过 PetWindow::modelSizeLoaded 在首次 show 后同步（无需 show/hide event filter）

    // 定期维护 Z-order：InputProxyWindow 必须在 PetWindow 之上
    auto *zOrderTimer = new QTimer(this);
    connect(zOrderTimer, &QTimer::timeout, this, [this]() {
        if (m_inputProxyWindow && m_inputProxyWindow->isVisible()) {
            m_inputProxyWindow->raise();
        }
    });
    zOrderTimer->start(2000);  // 2秒间隔 — 足够弥补 SetWindowPos(HWND_TOPMOST) 的影响

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
    m_inputProxyWindow->updateInputMask();
    m_inputProxyWindow->show();
    m_inputProxyWindow->raise();  // 确保在 PetWindow 之上

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

    // 异步获取天气
    fetchWeather();

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
                const QString prompt = m_promptManager->buildProactivePrompt();
                m_aiClient->sendMessage(prompt, m_promptManager->contextHistory());
            });

    // --- 聊天窗口 → AI 客户端 ---
    connect(m_chatWindow.get(), &ChatWindow::userMessageSent,
            this, [this](const QString &text) {
                m_promptManager->appendUserMessage(text);
                m_memoryDb->saveMessage(m_currentCharacter, QStringLiteral("user"), text);
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
            });

    // --- AI 客户端（流式）→ 聊天窗口实时更新 ---
    connect(m_aiClient.get(), &DeepSeekClient::partialReply,
            m_chatWindow.get(), &ChatWindow::updateAssistantMessage);

    // --- AI 客户端流式结束 → 记忆落库 + 情绪分析 + TTS ---
    connect(m_aiClient.get(), &DeepSeekClient::replyReceived,
            this, [this](const QString &reply) {
                m_chatWindow->finishAssistantMessage();

                // 记忆落库
                m_promptManager->appendAssistantMessage(reply);
                m_memoryDb->saveMessage(m_currentCharacter, QStringLiteral("assistant"), reply);

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
            });

    connect(m_aiClient.get(), &DeepSeekClient::errorOccurred,
            this, [this](const QString &error) {
                qWarning() << "[App] AI 请求出错：" << error;
                m_chatWindow->appendSystemMessage(tr("AI 暂时没有响应，请稍后再试～"));
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
            this, [](bool enabled) {
#ifdef Q_OS_WIN
                QSettings reg(QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run"),
                              QSettings::NativeFormat);
                const QString appName = QApplication::applicationName();
                if (enabled) {
                    const QString exePath = QApplication::applicationFilePath().replace('/', '\\');
                    reg.setValue(appName, QStringLiteral("\"%1\"").arg(exePath));
                } else {
                    reg.remove(appName);
                }
#else
                Q_UNUSED(enabled);
                qWarning() << "[App] 当前平台暂未实现开机自启";
#endif
            });

    connect(m_trayManager.get(), &TrayManager::settingsRequested,
            this, [this]() {
                m_settingsWindow->setApiKeyConfigured(m_aiClient->hasApiKey());
                m_settingsWindow->setSnapThresholdPx(m_windowManager->snapThreshold());
                m_settingsWindow->setIdleTimeoutMinutes(m_interactionManager->idleTimeoutMs() / 60000);
                m_settingsWindow->setModelScale(m_petWindow->modelScale());
                m_settingsWindow->setHeadTrackingEnabled(m_petWindow->headTrackingEnabled());
                m_settingsWindow->setKeepOnTop(m_petWindow->fullscreenOverlayEnabled());
                m_settingsWindow->setTtsEnabled(m_ttsEngine && m_ttsEngine->isEnabled());

                // 📸 当前截图保存路径
                QSettings ss(QStringLiteral("DesktopPet"), QStringLiteral("Screenshot"));
                m_settingsWindow->setScreenshotPath(
                    ss.value(QStringLiteral("savePath")).toString());

                if (m_ttsEngine) {
                    m_settingsWindow->setTtsBackend(static_cast<int>(m_ttsEngine->backend()));
                    m_settingsWindow->setAvailableVoices(
                        m_ttsEngine->availableVoices(), m_ttsEngine->voice());
                    m_settingsWindow->setVoiceVoxUrl(m_ttsEngine->voiceVoxUrl());

                    // 如果是 VoiceVox 后端且说话人还没缓存，触发拉取
                    if (m_ttsEngine->backend() == TtsEngine::VoiceVox
                        && m_ttsEngine->availableVoices().isEmpty()) {
                        m_settingsWindow->setVoiceVoxLoading(true);
                        m_ttsEngine->refreshVoices();
                    }
                }
                // 聊天背景状态
                m_settingsWindow->setChatBackgroundImagePath(m_chatWindow->backgroundImagePath());
                m_settingsWindow->setChatBackgroundOpacity(m_chatWindow->backgroundOpacity());
                m_settingsWindow->setChatCropButtonEnabled(!m_chatWindow->backgroundImagePath().isEmpty());

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

                m_settingsWindow->showAndRaise();
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

    // ★ 设置 → 游戏全屏时始终显示桌宠（触发全屏检测，而非无条件置顶）
    connect(m_settingsWindow.get(), &SettingsWindow::keepOnTopToggled,
            this, [this](bool enabled) {
                if (m_petWindow) {
                    m_petWindow->setFullscreenOverlayEnabled(enabled);
                }
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
                        m_petWindow->hide();
                        if (m_inputProxyWindow) m_inputProxyWindow->hide();
                    } else {
                        m_petWindow->show();
                        if (m_inputProxyWindow) {
                            m_inputProxyWindow->syncGeometry();
                            m_inputProxyWindow->show();
                            m_inputProxyWindow->raise();
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

void App::quit()
{
    qInfo() << "[App] 正在退出...";
    if (m_memoryDb) {
        m_memoryDb->close();
    }
    qApp->quit();
}
