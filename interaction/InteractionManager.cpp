#include "InteractionManager.h"

#include <QWidget>
#include <QEvent>
#include <QMouseEvent>
#include <QTimer>
#include <QApplication>
#include <QDebug>

InteractionManager::InteractionManager(QWidget *targetWindow, QObject *parent)
    : QObject(parent)
    , m_target(targetWindow)
{
    m_target->installEventFilter(this);

    m_longPressTimer = new QTimer(this);
    m_longPressTimer->setSingleShot(true);
    connect(m_longPressTimer, &QTimer::timeout, this, [this]() {
        m_longPressFired = true;
        emit longPressed();
    });

    m_idleTimer = new QTimer(this);
    m_idleTimer->setSingleShot(true);
    connect(m_idleTimer, &QTimer::timeout, this, [this]() {
        emit idleTimeout();
        // 主动说话之后重新开始计时，而不是停在这里不动
        m_idleTimer->start();
    });

    setIdleTimeoutMs(10 * 60 * 1000); // 默认 10 分钟，需求里明确写了这个数字
}

void InteractionManager::setIdleTimeoutMs(int ms)
{
    m_idleTimer->setInterval(ms);
    if (!m_idleTimer->isActive()) {
        m_idleTimer->start();
    }
}

int InteractionManager::idleTimeoutMs() const
{
    return m_idleTimer->interval();
}

void InteractionManager::resetIdleTimer()
{
    m_idleTimer->start(); // QTimer::start() 会重置已经在跑的计时
}

void InteractionManager::cancelPendingSingleClick()
{
    // 让之前那个延迟判定单击的 lambda 在执行时发现 token 已经过期，从而不再触发 singleClicked
    ++m_clickToken;
}

bool InteractionManager::eventFilter(QObject *watched, QEvent *event)
{
    if (watched != m_target) {
        return QObject::eventFilter(watched, event);
    }

    switch (event->type()) {
        case QEvent::MouseButtonPress: {
            auto *me = static_cast<QMouseEvent *>(event);
            if (me->button() == Qt::LeftButton) {
                resetIdleTimer();
                m_pressPos = me->pos();
                m_pressedWithinTolerance = true;
                m_longPressFired = false;
                m_longPressTimer->start(m_longPressThresholdMs);
            }
            break; // 不消费事件，让 PetWindow 自己的 mousePressEvent 也能跑（处理拖拽）
        }

        case QEvent::MouseMove: {
            auto *me = static_cast<QMouseEvent *>(event);
            if (m_pressedWithinTolerance) {
                const int dist = (me->pos() - m_pressPos).manhattanLength();
                if (dist > m_moveTolerancePx) {
                    // 移动超出容差，判定为拖拽而不是点击/长按，取消长按计时
                    m_pressedWithinTolerance = false;
                    m_longPressTimer->stop();
                }
            }
            resetIdleTimer();
            break;
        }

        case QEvent::MouseButtonRelease: {
            auto *me = static_cast<QMouseEvent *>(event);
            if (me->button() == Qt::LeftButton) {
                m_longPressTimer->stop();
                resetIdleTimer();

                const bool wasLongPress = m_longPressFired;
                m_longPressFired = false;

                if (!wasLongPress && m_pressedWithinTolerance) {
                    // 延迟一个"双击判定窗口"的时间，确认这不是双击的第一次点击
                    const int myToken = ++m_clickToken;
                    const int interval = QApplication::doubleClickInterval();
                    QTimer::singleShot(interval, this, [this, myToken]() {
                        if (myToken == m_clickToken) {
                            emit singleClicked();
                        }
                        // 如果 token 已经变了（说明期间发生了双击或新的一次按下），则丢弃这次单击判定
                    });
                }
                m_pressedWithinTolerance = false;
            }
            break;
        }

        case QEvent::MouseButtonDblClick: {
            auto *me = static_cast<QMouseEvent *>(event);
            if (me->button() == Qt::LeftButton) {
                resetIdleTimer();
                cancelPendingSingleClick(); // 让上面延迟的单击判定失效
                m_longPressTimer->stop();
                m_pressedWithinTolerance = false;
                emit doubleClicked(me->pos());
            }
            break;
        }

        default:
            break;
    }

    return QObject::eventFilter(watched, event); // 永远不吞事件，PetWindow 的拖拽逻辑要正常工作
}
