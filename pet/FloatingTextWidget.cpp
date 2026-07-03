#include "FloatingTextWidget.h"

#include <QPainter>
#include <QFontMetrics>

FloatingTextWidget::FloatingTextWidget(const QString &text, const QColor &color)
    : QWidget(nullptr)  // 独立顶层窗口
    , m_text(text)
    , m_color(color)
{
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint | Qt::Tool | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    setAttribute(Qt::WA_ShowWithoutActivating, true);

    // ── 一次性计算文本尺寸（与原 QLabel font-size:14px;font-weight:bold 一致）──
    QFont f;
    f.setPixelSize(14);   // 用像素单位匹配 CSS 14px，非 setPointSize(14)
    f.setBold(true);
    setFont(f);

    QFontMetrics fm(f);
    // 按 boundingRect 计算尺寸，与 QLabel::adjustSize() 行为一致
    const QRect rawRect = fm.boundingRect(
        QRect(0, 0, 500, 0),
        Qt::AlignLeft | Qt::TextWordWrap,
        m_text
    );

    // ★ 预留上浮空间（kFloatDistance = 40px），防止 translate 时文字被窗口边界裁剪
    //   baseY = kFloatDistance = 40
    //   offset=0 → 文字画在 Y=40 （窗口底部，视觉起始位置）
    //   offset=40 → 文字画在 Y=0  （窗口顶部，上浮终点）
    //   全程文字在窗口 [0, height] 范围内，不会被裁剪
    constexpr int kFloatDistance = 40;
    m_textRect = QRect(0, kFloatDistance, rawRect.width(), rawRect.height());
    setFixedSize(rawRect.width(), rawRect.height() + kFloatDistance);
}

FloatingTextWidget::~FloatingTextWidget() = default;

// ── 属性 setter（触发重绘） ──

void FloatingTextWidget::setFloatOffset(qreal v)
{
    m_floatOffset = v;
    update();
}

void FloatingTextWidget::setOpacity(qreal v)
{
    m_opacity = v;
    update();
}

// ── 启动动画 ──

void FloatingTextWidget::startAnimation()
{
    // 上浮动效：0 → 40px，2s，线性（与旧手动插值一致）
    auto *floatAnim = new QPropertyAnimation(this, "floatOffset", this);
    floatAnim->setDuration(2000);
    floatAnim->setStartValue(0.0);
    floatAnim->setEndValue(40.0);
    floatAnim->setEasingCurve(QEasingCurve::Linear);

    // 透明度：淡入(300ms) → 停留(1200ms) → 淡出(500ms)
    auto *fadeIn = new QPropertyAnimation(this, "opacity", this);
    fadeIn->setDuration(300);
    fadeIn->setStartValue(0.0);
    fadeIn->setEndValue(1.0);

    auto *pause = new QPropertyAnimation(this, "opacity", this);
    pause->setDuration(1200);
    pause->setStartValue(1.0);
    pause->setEndValue(1.0);

    auto *fadeOut = new QPropertyAnimation(this, "opacity", this);
    fadeOut->setDuration(500);
    fadeOut->setStartValue(1.0);
    fadeOut->setEndValue(0.0);

    auto *fadeSeq = new QSequentialAnimationGroup(this);
    fadeSeq->addAnimation(fadeIn);
    fadeSeq->addAnimation(pause);
    fadeSeq->addAnimation(fadeOut);

    auto *group = new QParallelAnimationGroup(this);
    group->addAnimation(floatAnim);
    group->addAnimation(fadeSeq);

    // 动画完成后自动清理
    connect(group, &QParallelAnimationGroup::finished, this, &QObject::deleteLater);
    group->start(QAbstractAnimation::DeleteWhenStopped);

    qInfo() << "[FloatingTextWidget] startAnimation — 内部 translate + opacity, 无窗口移动";
}

// ── 自绘制 ──

void FloatingTextWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::TextAntialiasing);

    // 透明度（QPainter::setOpacity 只影响绘制，不触 DWM 合成）
    p.setOpacity(m_opacity);

    // 上浮偏移（向上 = 负 Y）
    p.translate(0, -m_floatOffset);

    // 字体已在构造函数设置，paintEvent 中复用
    // 与原 QLabel 效果一致：纯色文本 + 透明背景，无描边/无阴影/无额外边距
    p.setPen(m_color);
    p.drawText(m_textRect, Qt::AlignLeft | Qt::AlignVCenter, m_text);
}
