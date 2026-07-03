#include "SpeechBubble.h"

#include <QPainter>
#include <QPainterPath>
#include <QFontMetrics>
#include <QGraphicsOpacityEffect>
#include <QPropertyAnimation>
#include <QSequentialAnimationGroup>

SpeechBubble::SpeechBubble(QWidget *parent)
    : QWidget(parent)
{
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint | Qt::Tool | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    setAttribute(Qt::WA_ShowWithoutActivating, true);

    // 生命周期定时器（到时后启动淡出）
    m_lifeTimer = new QTimer(this);
    m_lifeTimer->setSingleShot(true);
    connect(m_lifeTimer, &QTimer::timeout, this, [this]() {
        startFadeOut(0);
    });
}

SpeechBubble::~SpeechBubble() = default;

// ──── 🎨 颜色 setter ────
void SpeechBubble::setBackgroundColor(const QColor &color)
{
    m_bgColor = color;
    update();
}

void SpeechBubble::setBorderColor(const QColor &color)
{
    m_borderColor = color;
    update();
}

void SpeechBubble::setTextColor(const QColor &color)
{
    m_textColor = color;
    update();
}

void SpeechBubble::setShadowColor(const QColor &color)
{
    m_shadowColor = color;
    update();
}

void SpeechBubble::showText(const QString &text, int durationMs)
{
    qInfo() << "[SpeechBubble] showText:" << text.left(30) << "durationMs=" << durationMs;

    // ① 先停生命周期定时器，防止过渡期间 timer 并发触发 startFadeOut
    m_lifeTimer->stop();

    // ② 清理正在运行的淡出动画
    if (m_fadeAnim) {
        // stop() 不会发射 finished，所以不会触发 finished 回调中的 hide()
        m_fadeAnim->stop();
        delete m_fadeAnim;  // 立即删除，不用 deleteLater 防遗留
        m_fadeAnim = nullptr;
    }

    // ③ 如果当前正在显示，清理窗口状态并复位
    if (isVisible()) {
        hide();
    }

    m_text = text;
    m_opacity = 1.0;

    // ④ 计算气泡尺寸
    QFont font = this->font();
    font.setPointSize(12);
    setFont(font);
    QFontMetrics fm(font);

    // 按最大宽度计算文本换行后的高度
    const int maxTextWidth = kMaxWidth - kPadding * 2;
    QRect textRect = fm.boundingRect(
        QRect(0, 0, maxTextWidth, 0),
        Qt::AlignLeft | Qt::TextWordWrap,
        m_text);

    const int bubbleW = qMin(textRect.width() + kPadding * 2, kMaxWidth) + kShadowOffset;
    const int bubbleH = textRect.height() + kPadding * 2 + kTailHeight + kShadowOffset;
    resize(bubbleW, bubbleH);

    // ⑤ 位置复位到 (0,0)，由 PetWindow::updateSpeechBubblePosition 重新定位
    move(0, 0);

    // ⑥ 启动生命周期定时器
    m_lifeTimer->start(durationMs);

    // ⑦ 显示
    show();
    raise();
}

void SpeechBubble::setOpacity(qreal opacity)
{
    m_opacity = opacity;
    update();
}

void SpeechBubble::startFadeOut(int delayMs)
{
    qInfo() << "[SpeechBubble] startFadeOut delayMs=" << delayMs;

    if (m_fadeAnim) {
        m_fadeAnim->stop();
        m_fadeAnim->deleteLater();
        m_fadeAnim = nullptr;
    }

    m_fadeAnim = new QPropertyAnimation(this, "opacity", this);
    m_fadeAnim->setDuration(500);
    m_fadeAnim->setStartValue(1.0);
    m_fadeAnim->setEndValue(0.0);
    m_fadeAnim->setEasingCurve(QEasingCurve::InCubic);

    connect(m_fadeAnim, &QPropertyAnimation::finished, this, [this]() {
        qInfo() << "[SpeechBubble] fadeOut finished → hide";
        // 先清指针，再 hide — 防止 showText 在 hide() 和指针清空之间误判
        m_fadeAnim = nullptr;
        hide();
        m_opacity = 1.0;
    });

    if (delayMs > 0) {
        QTimer::singleShot(delayMs, this, [this]() {
            if (m_fadeAnim) m_fadeAnim->start();
        });
    } else {
        m_fadeAnim->start();
    }
}

void SpeechBubble::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);
    p.setOpacity(m_opacity);

    const int w = width() - kShadowOffset;
    const int h = height() - kTailHeight - kShadowOffset;
    const int tailCenterX = w / 2;

    // ── 绘制阴影 ──
    p.setPen(Qt::NoPen);
    p.setBrush(m_shadowColor);
    p.drawRoundedRect(kShadowOffset, kShadowOffset, w, h, kRadius, kRadius);

    // ── 绘制气泡主体 ──
    QPainterPath bodyPath;
    bodyPath.addRoundedRect(0, 0, w, h, kRadius, kRadius);
    p.setBrush(m_bgColor);
    p.setPen(QPen(m_borderColor, 1));
    p.drawPath(bodyPath);

    // ── 绘制尾部三角 ──
    QPainterPath tailPath;
    tailPath.moveTo(tailCenterX - kTailWidth / 2.0, h);
    tailPath.lineTo(tailCenterX + kTailWidth / 2.0, h);
    tailPath.lineTo(tailCenterX, h + kTailHeight);
    tailPath.closeSubpath();
    p.setBrush(m_bgColor);
    p.setPen(QPen(m_borderColor, 1));
    p.drawPath(tailPath);

    // ── 覆盖气泡底部和三角之间的分割线 ──
    p.setPen(Qt::NoPen);
    p.setBrush(m_bgColor);
    p.drawRect(tailCenterX - kTailWidth / 2.0 + 1, h - 1, kTailWidth - 2, 2);

    // ── 绘制文字 ──
    QFont font = p.font();
    font.setPointSize(12);
    p.setFont(font);
    p.setPen(m_textColor);
    QRect textRect(kPadding, kPadding, w - kPadding * 2, h - kPadding * 2);
    p.drawText(textRect, Qt::AlignLeft | Qt::TextWordWrap | Qt::AlignVCenter, m_text);
}
