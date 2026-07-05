#include "TtsEngine.h"
#include "VoiceVoxClient.h"

#include <QMediaPlayer>
#include <QAudioOutput>
#include <QDebug>
#include <QCoreApplication>
#include <QRegularExpression>
#include <QSettings>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QUrl>
#include <QProcess>
#include <QTimer>
#include <QStandardPaths>

TtsEngine::TtsEngine(QObject *parent)
    : QObject(parent)
{
    // ── 创建统一播放器 ──
    m_mediaPlayer = new QMediaPlayer(this);
    m_audioOutput = new QAudioOutput(this);
    m_mediaPlayer->setAudioOutput(m_audioOutput);

    connect(m_mediaPlayer, &QMediaPlayer::mediaStatusChanged, this,
        [this](QMediaPlayer::MediaStatus status) {
            if (status == QMediaPlayer::EndOfMedia) {
                // 正常播放结束 → 释放 source 后清理文件
                m_mediaPlayer->stop();
                m_mediaPlayer->setSource(QUrl());
                cleanupTempFiles();
            } else if (status == QMediaPlayer::InvalidMedia) {
                qWarning() << "[TtsEngine] 无效媒体文件";
                emit errorOccurred(static_cast<int>(m_backend),
                    QStringLiteral("音频文件无效或无法加载"));
                m_mediaPlayer->stop();
                m_mediaPlayer->setSource(QUrl());
                cleanupTempFiles();
            }
        });

    connect(m_mediaPlayer, &QMediaPlayer::errorOccurred, this,
        [this](QMediaPlayer::Error error, const QString &errorString) {
            qWarning() << "[TtsEngine] 播放器错误:" << error << errorString;
            emit errorOccurred(static_cast<int>(m_backend), errorString);
            m_mediaPlayer->stop();
            m_mediaPlayer->setSource(QUrl());
            cleanupTempFiles();
        });

    // ── Edge TTS 超时定时器 ──
    m_edgeTimeoutTimer = new QTimer(this);
    m_edgeTimeoutTimer->setSingleShot(true);
    connect(m_edgeTimeoutTimer, &QTimer::timeout, this, &TtsEngine::onEdgeTimeout);

    // ── 读取设置并执行旧配置迁移 ──
    QSettings settings(QStringLiteral("DesktopPet"), QStringLiteral("TtsEngine"));

    const int schemaVersion = settings.value(QStringLiteral("backendSchemaVersion"), 1).toInt();

    if (schemaVersion < 2) {
        // === 旧格式迁移（version 1 → 2） ===
        const int rawBackend = settings.value(QStringLiteral("backend"), 0).toInt();

        int newBackend;
        if (rawBackend == 0) {
            // 旧 SAPI（值=0）→ EdgeTts
            newBackend = static_cast<int>(EdgeTts);
        } else if (rawBackend == 1) {
            // 旧 VoiceVox（值=1）→ VoiceVox
            newBackend = static_cast<int>(VoiceVox);
        } else {
            newBackend = static_cast<int>(EdgeTts);
        }

        // 原子写入迁移后配置
        settings.setValue(QStringLiteral("backend"), newBackend);
        settings.setValue(QStringLiteral("backendSchemaVersion"), 2);
        settings.remove(QStringLiteral("sapiVoice"));

        m_backend = static_cast<Backend>(newBackend);
        qInfo() << "[TtsEngine] 旧配置已迁移: schemaVersion=1→2, backend=" << newBackend;
    } else {
        // 已迁移版本，直接读取
        const int rawBackend = settings.value(QStringLiteral("backend"),
            static_cast<int>(EdgeTts)).toInt();
        if (rawBackend != static_cast<int>(EdgeTts)
            && rawBackend != static_cast<int>(VoiceVox)) {
            m_backend = EdgeTts;
        } else {
            m_backend = static_cast<Backend>(rawBackend);
        }
    }

    // ── 恢复 Edge TTS 设置 ──
    m_edgeExecutablePath = settings.value(QStringLiteral("edgeExecutablePath")).toString();
    m_edgeVoice = settings.value(QStringLiteral("edgeVoice"),
        QStringLiteral("zh-CN-XiaoyiNeural")).toString();
    m_edgeRatePercent = settings.value(QStringLiteral("edgeRatePercent"), 5).toInt();
    m_edgeVolumePercent = settings.value(QStringLiteral("edgeVolumePercent"), 0).toInt();
    m_edgePitchHz = settings.value(QStringLiteral("edgePitchHz"), 15).toInt();

    // ── 恢复 VoiceVox 配置 ──
    m_vvUrl = settings.value(QStringLiteral("vvUrl"),
        QStringLiteral("http://127.0.0.1:50021")).toString();
    m_vvStyleId = settings.value(QStringLiteral("vvStyleId"), 0).toInt();
    m_vvVoiceDisplayName = settings.value(QStringLiteral("vvVoiceName")).toString();

    // ── 创建 VoiceVox 客户端 ──
    m_vvClient = new VoiceVoxClient(this);
    m_vvClient->setBaseUrl(m_vvUrl);

    // VoiceVox 音频就绪 → 统一处理
    connect(m_vvClient, &VoiceVoxClient::audioReady,
        this, &TtsEngine::handleAudioReady);

    // VoiceVox 请求失败
    connect(m_vvClient, &VoiceVoxClient::requestFailed,
        this, [this](int requestId, const QString &msg) {
            if (requestId != m_generation) return; // 已过期
            qWarning() << "[TtsEngine] VoiceVox 请求失败 (req=" << requestId << "):" << msg;
            emit errorOccurred(static_cast<int>(VoiceVox), msg);
        });

    // VoiceVox 网络错误（非请求级，如说话人列表拉取失败）
    connect(m_vvClient, &VoiceVoxClient::networkError,
        this, [this](const QString &msg) {
            qWarning() << "[TtsEngine] VoiceVox 网络错误:" << msg;
        });

    // VoiceVox 说话人就绪 → 通知 UI
    connect(m_vvClient, &VoiceVoxClient::speakersReady,
        this, [this]() {
            emit VoiceVoxSpeakersReady();
        });

    qInfo() << "[TtsEngine] 初始化完成，后端:" << (m_backend == VoiceVox ? "VoiceVox" : "EdgeTTS");
}

TtsEngine::~TtsEngine()
{
    // 超时定时器自动随父对象析构
    stop();
}

// ============================================================================
// 启用/禁用
// ============================================================================

void TtsEngine::setEnabled(bool enabled)
{
    if (m_enabled == enabled) return;
    m_enabled = enabled;
    if (!enabled) {
        stop();
    }
}

// ============================================================================
// 后端切换
// ============================================================================

void TtsEngine::setBackend(Backend backend)
{
    if (backend == m_backend) return;
    m_backend = backend;

    QSettings settings(QStringLiteral("DesktopPet"), QStringLiteral("TtsEngine"));
    settings.setValue(QStringLiteral("backend"), static_cast<int>(backend));

    // 停止当前所有操作
    stop();

    qInfo() << "[TtsEngine] 后端切换为:" << (backend == VoiceVox ? "VoiceVox" : "EdgeTTS");
    emit statusChanged(static_cast<int>(backend),
        backend == VoiceVox
            ? QStringLiteral("VoiceVox")
            : QStringLiteral("Edge TTS"));
}

// ============================================================================
// 语音列表 / 选择
// ============================================================================

QStringList TtsEngine::availableVoices()
{
    if (m_backend == EdgeTts) {
        // Stage 2: 尚未实现 Edge 语音列表查询（阶段 3 会补上）
        return {};
    }

    // VoiceVox：从缓存返回显示名列表
    QStringList names;
    const auto speakers = m_vvClient->speakers();
    for (const auto &s : speakers) {
        names.append(s.name + QStringLiteral("（") + s.styleName + QStringLiteral("）"));
    }
    return names;
}

void TtsEngine::setVoice(const QString &voiceDisplayName)
{
    if (m_backend == EdgeTts) {
        // Stage 2: Edge 语音选择通过 setEdgeVoice() 单独设置
        return;
    }

    // VoiceVox：从显示名反查 styleId
    const auto speakers = m_vvClient->speakers();
    for (const auto &s : speakers) {
        const QString display = s.name + QStringLiteral("（") + s.styleName + QStringLiteral("）");
        if (display == voiceDisplayName) {
            m_vvStyleId = s.styleId;
            m_vvVoiceDisplayName = voiceDisplayName;

            QSettings settings(QStringLiteral("DesktopPet"), QStringLiteral("TtsEngine"));
            settings.setValue(QStringLiteral("vvStyleId"), m_vvStyleId);
            settings.setValue(QStringLiteral("vvVoiceName"), m_vvVoiceDisplayName);

            qInfo() << "[TtsEngine] VoiceVox 说话人设置为:" << voiceDisplayName;
            return;
        }
    }
}

QString TtsEngine::voice() const
{
    if (m_backend == EdgeTts) {
        return {};
    }
    return m_vvVoiceDisplayName;
}

void TtsEngine::refreshVoices()
{
    if (m_backend == VoiceVox) {
        m_vvClient->fetchSpeakers();
    }
}

// ============================================================================
// 朗读（统一入口，latest-wins）
// ============================================================================

void TtsEngine::speak(const QString &text)
{
    if (!m_enabled || text.isEmpty()) return;

    // 清理 HTML/Markdown 标记
    QString sanitized = text;
    sanitized.remove(QRegularExpression(QStringLiteral("<[^>]*>")));
    if (sanitized.isEmpty()) return;

    // ↑ generation → 取消旧请求，标记最新
    m_generation++;
    const int gen = m_generation;

    // 停止当前播放和合成
    stopPlayer();
    killEdgeProcess();
    m_vvClient->cancelPendingRequests();
    cleanupTempFiles();

    // 停止超时定时器（killEdgeProcess 不负责停定时器）
    m_edgeTimeoutTimer->stop();

    if (m_backend == EdgeTts) {
        startEdgeTts(sanitized, gen);
    } else if (m_backend == VoiceVox) {
        m_vvClient->synthesize(sanitized, m_vvStyleId, gen);
        emit statusChanged(static_cast<int>(VoiceVox),
            QStringLiteral("VoiceVox 合成中..."));
    }
}

// ============================================================================
// 停止（停止一切：播放 + 合成 + 清理）
// ============================================================================

void TtsEngine::stop()
{
    m_generation++;
    stopPlayer();
    killEdgeProcess();
    m_vvClient->cancelPendingRequests();
    m_edgeTimeoutTimer->stop();
    cleanupTempFiles();
}

// ============================================================================
// 播放器控制
// ============================================================================

void TtsEngine::stopPlayer()
{
    m_mediaPlayer->stop();
    m_mediaPlayer->setSource(QUrl());
}

// ============================================================================
// 临时文件管理
// ============================================================================

void TtsEngine::cleanupTempFiles()
{
    if (!m_currentTempDir.isEmpty()) {
        QDir dir(m_currentTempDir);
        if (dir.exists()) {
            // 删除目录下所有文件
            const auto files = dir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
            for (const QFileInfo &fi : files) {
                QFile::remove(fi.absoluteFilePath());
            }
            dir.rmdir(m_currentTempDir);
        }
        m_currentTempDir.clear();
        m_currentTempRequestId = -1;
    }
}

// ============================================================================
// VoiceVox 音频就绪处理
// ============================================================================

void TtsEngine::handleAudioReady(int requestId, const QByteArray &wavData)
{
    // 校验 generation：过期请求不播放
    if (requestId != m_generation) {
        qInfo() << "[TtsEngine] 丢弃过期 VoiceVox 音频 (req="
                << requestId << "gen=" << m_generation << ")";
        return;
    }

    if (m_backend != VoiceVox) {
        qInfo() << "[TtsEngine] 后端已切换，丢弃 VoiceVox 音频";
        return;
    }

    if (!m_enabled) {
        qInfo() << "[TtsEngine] TTS 已关闭，丢弃音频";
        return;
    }

    // 创建唯一临时目录
    const QString tmpDir = QDir::tempPath()
        + QStringLiteral("/DesktopPetTts/")
        + QString::number(QCoreApplication::applicationPid())
        + QStringLiteral("/")
        + QString::number(requestId);

    QDir().mkpath(tmpDir);

    const QString wavPath = tmpDir + QStringLiteral("/output.wav");
    QFile f(wavPath);
    if (!f.open(QIODevice::WriteOnly)) {
        qWarning() << "[TtsEngine] 无法写入临时 WAV:" << wavPath;
        emit errorOccurred(static_cast<int>(VoiceVox),
            QStringLiteral("无法写入临时 WAV 文件"));
        return;
    }
    f.write(wavData);
    f.close();

    // 记录当前临时文件
    m_currentTempDir = tmpDir;
    m_currentTempRequestId = requestId;

    // 通过统一播放器播放
    m_mediaPlayer->setSource(QUrl::fromLocalFile(wavPath));
    m_mediaPlayer->play();

    emit statusChanged(static_cast<int>(VoiceVox),
        QStringLiteral("VoiceVox 播放中"));
}

// ============================================================================
// VoiceVox URL（带规范化）
// ============================================================================

void TtsEngine::setVoiceVoxUrl(const QString &url)
{
    QString clean = url.trimmed();
    while (clean.endsWith('/')) clean.chop(1);

    // 验证 scheme
    if (!clean.startsWith(QStringLiteral("http://"))
        && !clean.startsWith(QStringLiteral("https://"))) {
        qWarning() << "[TtsEngine] VoiceVox URL scheme 无效，使用默认 URL";
        clean = QStringLiteral("http://127.0.0.1:50021");
    }

    m_vvUrl = clean;
    m_vvClient->setBaseUrl(m_vvUrl);

    QSettings(QStringLiteral("DesktopPet"), QStringLiteral("TtsEngine"))
        .setValue(QStringLiteral("vvUrl"), m_vvUrl);

    qInfo() << "[TtsEngine] VoiceVox URL 已设置:" << m_vvUrl;
}

QString TtsEngine::voiceVoxUrl() const
{
    return m_vvUrl;
}

// ============================================================================
// Edge TTS 配置 getter/setter（立即持久化）
// ============================================================================

void TtsEngine::setEdgeExecutablePath(const QString &path)
{
    m_edgeExecutablePath = path;
    QSettings(QStringLiteral("DesktopPet"), QStringLiteral("TtsEngine"))
        .setValue(QStringLiteral("edgeExecutablePath"), path);
}

QString TtsEngine::edgeExecutablePath() const
{
    return m_edgeExecutablePath;
}

void TtsEngine::setEdgeVoice(const QString &voice)
{
    m_edgeVoice = voice;
    QSettings(QStringLiteral("DesktopPet"), QStringLiteral("TtsEngine"))
        .setValue(QStringLiteral("edgeVoice"), voice);
}

QString TtsEngine::edgeVoice() const
{
    return m_edgeVoice;
}

void TtsEngine::setEdgeRatePercent(int percent)
{
    m_edgeRatePercent = percent;
    QSettings(QStringLiteral("DesktopPet"), QStringLiteral("TtsEngine"))
        .setValue(QStringLiteral("edgeRatePercent"), percent);
}

int TtsEngine::edgeRatePercent() const
{
    return m_edgeRatePercent;
}

void TtsEngine::setEdgeVolumePercent(int percent)
{
    m_edgeVolumePercent = percent;
    QSettings(QStringLiteral("DesktopPet"), QStringLiteral("TtsEngine"))
        .setValue(QStringLiteral("edgeVolumePercent"), percent);
}

int TtsEngine::edgeVolumePercent() const
{
    return m_edgeVolumePercent;
}

void TtsEngine::setEdgePitchHz(int hz)
{
    m_edgePitchHz = hz;
    QSettings(QStringLiteral("DesktopPet"), QStringLiteral("TtsEngine"))
        .setValue(QStringLiteral("edgePitchHz"), hz);
}

int TtsEngine::edgePitchHz() const
{
    return m_edgePitchHz;
}

void TtsEngine::setEdgeTimeoutMs(int ms)
{
    m_edgeTimeoutMs = ms;
}

// ============================================================================
// 插件发现 — 5 级优先级
// ============================================================================

QString TtsEngine::resolveEdgeExecutable() const
{
    // 1. QSettings 指定的路径（用户手动配置）
    if (!m_edgeExecutablePath.isEmpty()) {
        QFileInfo fi(m_edgeExecutablePath);
        if (fi.exists() && fi.isFile()) {
            return fi.absoluteFilePath();
        }
    }

    // 2. 项目开发目录：tools/edge_tts/.venv/Scripts/edge-tts.exe
    {
        const QString devPath = QDir::cleanPath(QCoreApplication::applicationDirPath()
            + QStringLiteral("/../../tools/edge_tts/.venv/Scripts/edge-tts.exe"));
        QFileInfo fi(devPath);
        if (fi.exists() && fi.isFile()) {
            return fi.absoluteFilePath();
        }
    }

    // 3. 应用 plugins 目录：plugins/edge-tts/edge-tts.exe
    {
        const QString pluginPath = QDir::cleanPath(QCoreApplication::applicationDirPath()
            + QStringLiteral("/plugins/edge-tts/edge-tts.exe"));
        QFileInfo fi(pluginPath);
        if (fi.exists() && fi.isFile()) {
            return fi.absoluteFilePath();
        }
    }

    // 4. 系统 PATH 中查找 edge-tts.exe
    {
        const QString exe = QStandardPaths::findExecutable(QStringLiteral("edge-tts.exe"));
        if (!exe.isEmpty()) {
            return exe;
        }
    }

    // 5. 系统 PATH 中查找 edge-tts（无扩展名）
    {
        const QString exe = QStandardPaths::findExecutable(QStringLiteral("edge-tts"));
        if (!exe.isEmpty()) {
            return exe;
        }
    }

    return {};
}

// ============================================================================
// CLI 参数格式化
// ============================================================================

QString TtsEngine::formatEdgePercent(int percent)
{
    if (percent >= 0) {
        return QStringLiteral("+%1%").arg(percent);
    }
    // percent < 0: 字符串中的负号自带 "-"
    return QStringLiteral("%1%").arg(percent);
}

QString TtsEngine::formatEdgeHz(int hz)
{
    if (hz >= 0) {
        return QStringLiteral("+%1Hz").arg(hz);
    }
    return QStringLiteral("%1Hz").arg(hz);
}

// ============================================================================
// Edge TTS 合成启动
// ============================================================================

void TtsEngine::startEdgeTts(const QString &sanitized, int gen)
{
    // ── 解析可执行文件 ──
    const QString executable = resolveEdgeExecutable();
    if (executable.isEmpty()) {
        qWarning() << "[TtsEngine] Edge TTS 插件不存在 (req=" << gen << ")";
        emit errorOccurred(static_cast<int>(EdgeTts),
            QStringLiteral("Edge TTS 插件不存在"));
        return;
    }

    // ── 创建唯一临时目录 ──
    const QString tmpDir = QDir::tempPath()
        + QStringLiteral("/DesktopPetTts/")
        + QString::number(QCoreApplication::applicationPid())
        + QStringLiteral("/")
        + QString::number(gen);

    QDir().mkpath(tmpDir);

    // ── 写入 input.txt（UTF-8） ──
    const QString inputPath = tmpDir + QStringLiteral("/input.txt");
    {
        QFile f(inputPath);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
            qWarning() << "[TtsEngine] 无法写入输入文件:" << inputPath;
            emit errorOccurred(static_cast<int>(EdgeTts),
                QStringLiteral("无法写入 Edge TTS 输入文件"));
            QDir(tmpDir).rmdir(tmpDir);
            return;
        }
        f.write(sanitized.toUtf8());
        f.close();
    }

    // ── 输出路径 ──
    const QString outputPath = tmpDir + QStringLiteral("/output.mp3");

    // ── 构建 CLI 参数（QStringList，禁止 shell 拼接） ──
    QStringList args;
    args << QStringLiteral("--file") << inputPath;
    args << QStringLiteral("--voice") << m_edgeVoice;
    args << QStringLiteral("--rate") << formatEdgePercent(m_edgeRatePercent);
    args << QStringLiteral("--volume") << formatEdgePercent(m_edgeVolumePercent);
    args << QStringLiteral("--pitch") << formatEdgeHz(m_edgePitchHz);
    args << QStringLiteral("--write-media") << outputPath;

    // ── 记录当前临时目录以便 cleanup ──
    m_currentTempDir = tmpDir;
    m_currentTempRequestId = gen;

    // ── 创建 QProcess ──
    m_edgeProcess = new QProcess(this);
    m_edgeProcessGeneration = gen;

    connect(m_edgeProcess, &QProcess::started, this, &TtsEngine::onEdgeStarted);
    connect(m_edgeProcess,
        QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
        this, &TtsEngine::onEdgeFinished);
    connect(m_edgeProcess, &QProcess::errorOccurred, this, &TtsEngine::onEdgeErrorOccurred);

    // ── 启动超时定时器 ──
    m_edgeTimeoutGeneration = gen;
    m_edgeTimeoutTimer->start(m_edgeTimeoutMs);

    // ── 启动进程 ──
    m_edgeProcess->start(executable, args);

    qInfo() << "[TtsEngine] Edge TTS 启动 (req=" << gen
            << "textLength=" << sanitized.length()
            << "voice=" << m_edgeVoice
            << "rate=" << m_edgeRatePercent
            << "volume=" << m_edgeVolumePercent
            << "pitch=" << m_edgePitchHz << ")";

    emit statusChanged(static_cast<int>(EdgeTts),
        QStringLiteral("Edge TTS 合成中..."));
}

// ============================================================================
// Edge TTS QProcess 生命周期
// ============================================================================

void TtsEngine::onEdgeStarted()
{
    // QProcess 已成功启动，无需额外操作。
    // 状态已由 startEdgeTts 发射。
}

void TtsEngine::onEdgeFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    // 停止超时定时器
    m_edgeTimeoutTimer->stop();

    // 校验：是否仍为当前请求
    if (m_edgeProcessGeneration != m_generation) {
        qInfo() << "[TtsEngine] 丢弃过期 Edge TTS 结果 (req="
                << m_edgeProcessGeneration << "gen=" << m_generation << ")";
        // 进程指针已在 killEdgeProcess 中清理，此处只做校验
        return;
    }

    if (!m_enabled) {
        qInfo() << "[TtsEngine] TTS 已关闭，丢弃 Edge TTS 结果";
        return;
    }

    // 检查退出状态
    if (exitStatus != QProcess::NormalExit || exitCode != 0) {
        qWarning() << "[TtsEngine] Edge TTS 进程异常退出 (req="
                   << m_edgeProcessGeneration
                   << "exitStatus=" << exitStatus
                   << "exitCode=" << exitCode << ")";
        emit errorOccurred(static_cast<int>(EdgeTts),
            QStringLiteral("Edge TTS 合成失败（退出码 %1）").arg(exitCode));
        cleanupTempFiles();
        // 清理进程指针
        killEdgeProcess();
        return;
    }

    // 检查输出文件
    const QString outputPath = m_currentTempDir + QStringLiteral("/output.mp3");
    QFileInfo fi(outputPath);
    if (!fi.exists()) {
        qWarning() << "[TtsEngine] Edge TTS 输出文件不存在 (req="
                   << m_edgeProcessGeneration << ")";
        emit errorOccurred(static_cast<int>(EdgeTts),
            QStringLiteral("Edge TTS 输出文件不存在"));
        cleanupTempFiles();
        killEdgeProcess();
        return;
    }
    if (fi.size() == 0) {
        qWarning() << "[TtsEngine] Edge TTS 输出文件为空 (req="
                   << m_edgeProcessGeneration << ")";
        emit errorOccurred(static_cast<int>(EdgeTts),
            QStringLiteral("Edge TTS 输出文件为空"));
        cleanupTempFiles();
        killEdgeProcess();
        return;
    }

    // 成功：通过统一播放器播放
    m_mediaPlayer->setSource(QUrl::fromLocalFile(outputPath));
    m_mediaPlayer->play();

    qInfo() << "[TtsEngine] Edge TTS 播放 (req=" << m_edgeProcessGeneration
            << "fileSize=" << fi.size() << ")";

    emit statusChanged(static_cast<int>(EdgeTts),
        QStringLiteral("Edge TTS 播放中"));
}

void TtsEngine::onEdgeErrorOccurred(QProcess::ProcessError error)
{
    m_edgeTimeoutTimer->stop();

    if (m_edgeProcessGeneration != m_generation) {
        // 已过期
        return;
    }

    QString reason;
    switch (error) {
    case QProcess::FailedToStart:
        reason = QStringLiteral("Edge TTS 进程启动失败");
        break;
    case QProcess::Crashed:
        reason = QStringLiteral("Edge TTS 进程崩溃");
        break;
    case QProcess::Timedout:
        reason = QStringLiteral("Edge TTS 进程超时");
        break;
    case QProcess::WriteError:
        reason = QStringLiteral("Edge TTS 进程写入错误");
        break;
    case QProcess::ReadError:
        reason = QStringLiteral("Edge TTS 进程读取错误");
        break;
    default:
        reason = QStringLiteral("Edge TTS 进程未知错误");
        break;
    }

    qWarning() << "[TtsEngine] Edge TTS 进程错误 (req="
               << m_edgeProcessGeneration << "):" << reason;

    emit errorOccurred(static_cast<int>(EdgeTts), reason);
    cleanupTempFiles();
    killEdgeProcess();
}

void TtsEngine::onEdgeTimeout()
{
    if (m_edgeTimeoutGeneration != m_generation) {
        // 已过期
        return;
    }

    qWarning() << "[TtsEngine] Edge TTS 合成超时 (req="
               << m_edgeProcessGeneration << ")";
    emit errorOccurred(static_cast<int>(EdgeTts),
        QStringLiteral("Edge TTS 合成超时"));

    killEdgeProcess();
    cleanupTempFiles();
}

// ============================================================================
// 终止 Edge TTS 进程
// ============================================================================

void TtsEngine::killEdgeProcess()
{
    m_edgeProcessGeneration = -1;
    if (m_edgeProcess) {
        // 断开所有信号，防止旧进程的延迟信号触发
        m_edgeProcess->disconnect();
        if (m_edgeProcess->state() != QProcess::NotRunning) {
            m_edgeProcess->kill();
            m_edgeProcess->waitForFinished(1000);
        }
        m_edgeProcess->deleteLater();
        m_edgeProcess = nullptr;
    }
    m_edgeTimeoutTimer->stop();
}
