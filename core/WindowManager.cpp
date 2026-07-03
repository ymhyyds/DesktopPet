#include "WindowManager.h"

#include <QWidget>
#include <QScreen>
#include <QGuiApplication>
#include <QDebug>

WindowManager::WindowManager(QObject *parent)
    : QObject(parent)
{
}

void WindowManager::registerWindow(QWidget *window)
{
    if (!window) {
        return;
    }

    // 窗口标志由 PetWindow 自己在构造函数设置，这里不再重复 setWindowFlags
    // （setWindowFlags 会触发原生窗口重建，导致子 QOpenGLWidget 的 GL 上下文丢失）。
    // 我们只做注册管理。
    m_managedWindows.append(window);
}

QRect WindowManager::currentScreenGeometry(const QWidget *window) const
{
    QScreen *screen = window->screen();
    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }
    // availableGeometry 已经排除了任务栏占用的区域
    return screen ? screen->availableGeometry() : QRect(0, 0, 1920, 1080);
}

void WindowManager::placeAtDefaultPosition(QWidget *window)
{
    if (!window) {
        return;
    }

    const QRect screenRect = currentScreenGeometry(window);
    const int x = screenRect.right() - window->width() - m_screenMargin;
    const int y = screenRect.bottom() - window->height() - m_screenMargin;

    window->move(qMax(screenRect.left(), x), qMax(screenRect.top(), y));
}

void WindowManager::trySnapToEdge(QWidget *window)
{
    if (!window) {
        return;
    }

    const QRect screenRect = currentScreenGeometry(window);
    QPoint pos = window->pos();
    const QSize size = window->size();

    bool snapped = false;

    // 左边缘
    if (qAbs(pos.x() - screenRect.left()) <= m_snapThreshold) {
        pos.setX(screenRect.left());
        snapped = true;
    }
    // 右边缘
    else if (qAbs((pos.x() + size.width()) - screenRect.right()) <= m_snapThreshold) {
        pos.setX(screenRect.right() - size.width());
        snapped = true;
    }

    // 上边缘
    if (qAbs(pos.y() - screenRect.top()) <= m_snapThreshold) {
        pos.setY(screenRect.top());
        snapped = true;
    }
    // 下边缘（最常见：桌宠站在任务栏上方）
    else if (qAbs((pos.y() + size.height()) - screenRect.bottom()) <= m_snapThreshold) {
        pos.setY(screenRect.bottom() - size.height());
        snapped = true;
    }

    if (snapped) {
        window->move(pos);
        qDebug() << "[WindowManager] 窗口已贴边吸附至：" << pos;
    }
}

void WindowManager::refreshTopMost()
{
    for (const auto &w : m_managedWindows) {
        if (!w) {
            continue;
        }
        // 通过重新设置窗口标志，强制刷新置顶状态（某些情况下全屏应用会抢占置顶）
        w->setWindowFlags(w->windowFlags() | Qt::WindowStaysOnTopHint);
        w->show();
    }
}
