#pragma once

#include <QWidget>
#include <QString>
#include <QColor>
#include <QTimer>
#include <QPropertyAnimation>

/**
 * @brief SpeechBubble — 漫画风格说话气泡。
 *
 * 在模型头顶显示一个带尾部三角形的气泡，
 * 用于 AI 回复时显示文字，比浮动文字更二次元。
 *
 * 用法：
 *   SpeechBubble *bubble = new SpeechBubble();
 *   bubble->showText("你好喵～");
 *
 * 气泡会自动在指定时间后淡出消失。
 */
class SpeechBubble : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(qreal opacity READ opacity WRITE setOpacity)

public:
    explicit SpeechBubble(QWidget *parent = nullptr);
    ~SpeechBubble() override;

    /// 显示文字气泡（自动定位到 PetWindow 顶部中间）
    /// @param text      显示的文字
    /// @param durationMs 显示时间（毫秒），默认 6 秒后自动淡出
    void showText(const QString &text, int durationMs = 6000);

    qreal opacity() const { return m_opacity; }
    void setOpacity(qreal opacity);

    // ──── 🎨 颜色自定义 ────
    void setBackgroundColor(const QColor &color);
    void setBorderColor(const QColor &color);
    void setTextColor(const QColor &color);
    void setShadowColor(const QColor &color);

    QColor backgroundColor() const { return m_bgColor; }
    QColor borderColor() const { return m_borderColor; }
    QColor textColor() const { return m_textColor; }
    QColor shadowColor() const { return m_shadowColor; }

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    void startFadeOut(int delayMs);

    QString m_text;
    qreal   m_opacity = 1.0;

    // 🎨 颜色（默认：白色气泡 + 灰色边框 + 深灰文字）
    QColor m_bgColor     = QColor(255, 255, 255, 245);
    QColor m_borderColor = QColor(200, 200, 200);
    QColor m_textColor   = QColor(60, 60, 60);
    QColor m_shadowColor = QColor(0, 0, 0, 50);

    QTimer *m_lifeTimer = nullptr;
    QPropertyAnimation *m_fadeAnim = nullptr;

    static constexpr int kMaxWidth    = 260;
    static constexpr int kPadding     = 14;
    static constexpr int kRadius      = 10;
    static constexpr int kTailWidth   = 16;
    static constexpr int kTailHeight  = 10;
    static constexpr int kShadowOffset = 2;
};
