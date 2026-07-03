#pragma once

#include <QObject>
#include <QPoint>

class QWidget;
class QTimer;

/**
 * @brief InteractionManager 负责把"原始鼠标事件"翻译成"交互语义"：
 *        单击 / 双击 / 长按 / 挂机超时主动说话。
 *
 * 实现方式：对目标窗口 installEventFilter，监听鼠标事件流，
 * 不直接继承/修改 PetWindow——这样 PetWindow 只管自己的拖拽和外观，
 * "怎么判断这是单击还是长按"这种和具体窗口无关的通用逻辑全部封装在这里。
 *
 * 单击 vs 双击的经典处理方式：
 *  收到 release 后不立刻判定为单击，而是先等待系统双击间隔（QApplication::doubleClickInterval），
 *  如果这段时间内没有收到 MouseButtonDblClick，才真正确认为单击；
 *  如果收到了双击事件，则取消这次单击判定，只触发双击。
 */
class InteractionManager : public QObject
{
    Q_OBJECT

public:
    explicit InteractionManager(QWidget *targetWindow, QObject *parent = nullptr);

    /// 挂机超时阈值（毫秒），默认 10 分钟，对应需求里的"挂机超过10分钟主动说话"。
    void setIdleTimeoutMs(int ms);
    int idleTimeoutMs() const;

    /// 长按触发阈值（毫秒），默认 600ms。
    void setLongPressThresholdMs(int ms) { m_longPressThresholdMs = ms; }

    /// 移动超过这个像素距离就不再算作"点击"（防止拖拽误判为单击/长按）。
    void setMoveTolerancePx(int px) { m_moveTolerancePx = px; }

signals:
    void singleClicked();
    void doubleClicked(const QPoint &pos);
    void longPressed();
    void idleTimeout();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void resetIdleTimer();
    void cancelPendingSingleClick();

private:
    QWidget *m_target = nullptr;

    QPoint m_pressPos;
    bool m_pressedWithinTolerance = false;

    QTimer *m_longPressTimer = nullptr;
    bool m_longPressFired = false;

    // 用一个递增 token 来让"延迟判定单击"的 lambda 失效，避免裸的 QObject 生命周期判断
    int m_clickToken = 0;

    QTimer *m_idleTimer = nullptr;
    int m_longPressThresholdMs = 600;
    int m_moveTolerancePx = 6;
};
