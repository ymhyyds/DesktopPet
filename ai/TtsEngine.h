#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

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

signals:
    void VoiceVoxSpeakersReady();
    void statusChanged(int backend, const QString &statusText);
    void errorOccurred(int backend, const QString &reason);

private:
    void stopPlayer();
    void cleanupTempFiles();
    void handleAudioReady(int requestId, const QByteArray &wavData);

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

    // --- VoiceVox ---
    VoiceVoxClient *m_vvClient = nullptr;
    QString m_vvUrl = QStringLiteral("http://127.0.0.1:50021");
    int m_vvStyleId = 0;
    QString m_vvVoiceDisplayName;
};
