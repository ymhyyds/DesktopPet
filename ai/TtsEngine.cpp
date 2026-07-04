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
#include <QUrl>

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

    // ── 恢复 VoiceVox 配置（不变） ──
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
        // Phase 1: Edge CLI 尚未接入
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
        // Phase 1: 暂时不处理 Edge 语音选择
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
    m_vvClient->cancelPendingRequests();
    cleanupTempFiles();

    if (m_backend == EdgeTts) {
        // Phase 1: Edge CLI 尚未接入
        qInfo() << "[TtsEngine] Edge TTS 将在第二阶段接入";
        emit statusChanged(static_cast<int>(EdgeTts),
            QStringLiteral("Edge TTS 尚未接入"));
        return;
    }

    if (m_backend == VoiceVox) {
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
    m_vvClient->cancelPendingRequests();
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
