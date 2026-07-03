#pragma once

#include <QWidget>
#include <QString>
#include <QColor>
#include <QPropertyAnimation>
#include <QSequentialAnimationGroup>
#include <QParallelAnimationGroup>
#include <QEasingCurve>

/**
 * @brief FloatingTextWidget — 自绘制浮动文字。
 *
 * 在模型头顶显示一段带淡入淡出 + 上浮效果的文本。
 * 所有动画效果通过内部 QPainter translate + opacity 实现，
 * 不依赖顶层窗口的 move / setWindowOpacity，避免 DWM 重排卡顿。
 *
 * 用法：
 *   auto *w = new FloatingTextWidget("主人~~❤️ +3", QColor(255,200,200));
 *   w->move(x, y);
 *   w->show();
 *   w->startAnimation();  // 动画结束后自动 deleteLater
 */
class FloatingTextWidget : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(qreal floatOffset READ floatOffset WRITE setFloatOffset)
    Q_PROPERTY(qreal opacity READ opacity WRITE setOpacity)

public:
    explicit FloatingTextWidget(const QString &text, const QColor &color);
    ~FloatingTextWidget() override;

    qreal floatOffset() const { return m_floatOffset; }
    void setFloatOffset(qreal v);
    qreal opacity() const { return m_opacity; }
    void setOpacity(qreal v);

    /// 启动淡入→停留→淡出 + 上浮动画，结束后自动 deleteLater
    void startAnimation();

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QString m_text;
    QColor  m_color;
    QRect   m_textRect;

    qreal m_floatOffset = 0.0;
    qreal m_opacity     = 1.0;
};
