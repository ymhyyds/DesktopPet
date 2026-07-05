#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

#include <QProcess>
#include <QTimer>

class QMediaPlayer;
class QAudioOutput;
class VoiceVoxClient;

/**
 * @brief TtsEngine — 文本转语音引擎，支持 Edge TTS 和 VoiceVox 双后端。
 *
 * Edge TTS 后端：通过 QProcess 调用 edge-tts CLI 合成 MP3，QMediaPlayer 播放。
 * VoiceVox 后端：通过 VoiceVoxClient HTTP API 合成 WAV，QMediaPlayer 播放。
 *
 * 统一播放层：QMediaPlayer + QAudioOutput，latest-wins 并发策略。
 */
class TtsEngine : public QObject
{
    Q_OBJECT

public:
    enum Backend { EdgeTts = 0, VoiceVox = 1 };
    Q_ENUM(Backend)

    explicit TtsEngine(QObject *parent = nullptr);
    ~TtsEngine() override;

    /// --- 通用控制 ---
    void speak(const QString &text);
    void stop();

    void setEnabled(bool enabled);
    bool isEnabled() const { return m_enabled; }

    /// --- 后端选择 ---
    void setBackend(Backend backend);
    Backend backend() const { return m_backend; }

    /// --- 语音选择 ---
    QStringList availableVoices();
    void setVoice(const QString &voiceDisplayName);
    QString voice() const;
    void refreshVoices();

    /// --- 测试观察接口 ---
    int generation() const { return m_generation; }

    /// --- VoiceVox 专用 ---
    void setVoiceVoxUrl(const QString &url);
    QString voiceVoxUrl() const;

    /// --- Edge TTS 配置（立即持久化到 QSettings） ---
    void setEdgeExecutablePath(const QString &path);
    QString edgeExecutablePath() const;

    void setEdgeVoice(const QString &voice);
    QString edgeVoice() const;

    void setEdgeRatePercent(int percent);
    int edgeRatePercent() const;

    void setEdgeVolumePercent(int percent);
    int edgeVolumePercent() const;

    void setEdgePitchHz(int hz);
    int edgePitchHz() const;

    /// 超时毫秒（仅运行时，用于测试短超时，默认 30000）
    void setEdgeTimeoutMs(int ms);
    int edgeTimeoutMs() const { return m_edgeTimeoutMs; }

    /// --- 插件发现 ---
    QString resolveEdgeExecutable() const;

signals:
    void VoiceVoxSpeakersReady();
    void statusChanged(int backend, const QString &statusText);
    void errorOccurred(int backend, const QString &reason);
    /// 正常播放结束（对端是 VoiceVox / Edge TTS 播放的音频自然播完）
    void playbackFinished(int backend);

private:
    void stopPlayer();
    void killEdgeProcess();
    void cleanupTempFiles();
    void handleAudioReady(int requestId, const QByteArray &wavData);

    // --- Edge TTS 子流程 ---
    void startEdgeTts(const QString &sanitized, int gen);
    void onEdgeStarted();
    void onEdgeFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onEdgeErrorOccurred(QProcess::ProcessError error);
    void onEdgeTimeout();

    // --- CLI 参数格式化 ---
    static QString formatEdgePercent(int percent);
    static QString formatEdgeHz(int hz);

private:
    // --- 通用 ---
    bool m_enabled = true;
    int m_generation = 0;
    Backend m_backend = EdgeTts;

    // --- 统一播放层 (Qt Multimedia) ---
    QMediaPlayer *m_mediaPlayer = nullptr;
    QAudioOutput *m_audioOutput = nullptr;

    // --- 临时文件跟踪 ---
    QString m_currentTempDir;
    int m_currentTempRequestId = -1;

    // --- Edge TTS QProcess ---
    QProcess *m_edgeProcess = nullptr;
    int m_edgeProcessGeneration = -1;  // generation when this process was started

    // --- Edge TTS 超时 ---
    QTimer *m_edgeTimeoutTimer = nullptr;
    int m_edgeTimeoutGeneration = -1;  // generation the timeout was set for
    int m_edgeTimeoutMs = 30000;

    // --- Edge TTS 设置（从 QSettings 加载） ---
    QString m_edgeExecutablePath;
    QString m_edgeVoice = QStringLiteral("zh-CN-XiaoyiNeural");
    int m_edgeRatePercent = 5;
    int m_edgeVolumePercent = 0;
    int m_edgePitchHz = 15;

    // --- VoiceVox ---
    VoiceVoxClient *m_vvClient = nullptr;
    QString m_vvUrl = QStringLiteral("http://127.0.0.1:50021");
    int m_vvStyleId = 0;
    QString m_vvVoiceDisplayName;
};
