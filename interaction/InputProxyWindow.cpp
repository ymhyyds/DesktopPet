#include "InputProxyWindow.h"

#include "../pet/PetWindow.h"
#include "../pet/Live2DView.h"

#include <QMouseEvent>
#include <QContextMenuEvent>
#include <QPaintEvent>
#include <QApplication>
#include <QCoreApplication>
#include <QDebug>

// ============================================================================
// 构造 / 析构
// ============================================================================

InputProxyWindow::InputProxyWindow(PetWindow *petWindow, Live2DView *live2DView,
                                   QWidget *parent)
    : QWidget(parent)
    , m_petWindow(petWindow)
    , m_live2DView(live2DView)
{
    // 顶层无边框窗口，不绘制内容，不抢焦点
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_ShowWithoutActivating, true);
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    setAutoFillBackground(false);

    // 不显示在任务栏
    setWindowFlag(Qt::WindowDoesNotAcceptFocus, true);

    // 初始 geometry 与 PetWindow 一致
    syncGeometry();
}

InputProxyWindow::~InputProxyWindow() = default;

// ============================================================================
// Geometry 同步
// ============================================================================

void InputProxyWindow::syncGeometry()
{
    if (!m_petWindow) return;
    setGeometry(m_petWindow->geometry());
}

// ============================================================================
// Mask 更新
// ============================================================================

void InputProxyWindow::updateInputMask()
{
    if (!m_live2DView) { clearMask(); m_hasCachedMask = false; return; }

    float mx = 0.0f, my = 0.0f, mw = 0.0f, mh = 0.0f;
    if (!m_live2DView->getModelPixelRect(mx, my, mw, mh) || mw <= 0 || mh <= 0) {
        clearMask();
        m_hasCachedMask = false;
        return;
    }

    // 围绕模型区域构建 mask + padding
    const int px = qRound(mx) - kMaskPaddingPx;
    const int py = qRound(my) - kMaskPaddingPx;
    const int pw = qRound(mw) + kMaskPaddingPx * 2;
    const int ph = qRound(mh) + kMaskPaddingPx * 2;

    QRegion region(px, py, pw, ph);
    region = region.intersected(QRect(0, 0, width(), height()));

    if (region.isEmpty()) { clearMask(); m_hasCachedMask = false; return; }

    // 全窗口 → 清除 mask（罕见情况，模型填满窗口）
    if (region == QRegion(QRect(0, 0, width(), height()))) {
        if (m_hasCachedMask) { clearMask(); m_hasCachedMask = false; }
        return;
    }

    // 防重复：相同 region 跳过
    if (m_hasCachedMask && m_cachedMask == region) return;

    setMask(region);
    m_cachedMask = region;
    m_hasCachedMask = true;

    qInfo().noquote()
        << "[InputProxy] mask updated:"
        << region.boundingRect().x() << region.boundingRect().y()
        << region.boundingRect().width() << "x" << region.boundingRect().height();
}

// ============================================================================
// 鼠标事件
// ============================================================================

void InputProxyWindow::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging = true;
        m_dragStartOffset = event->pos();

        const float px = static_cast<float>(event->position().x());
        const float py = static_cast<float>(event->position().y());

        // 命中检测：头部 / 身体 / 空白
        bool isHead = false;
        bool hit = m_live2DView ? m_live2DView->hitTestAt(px, py, isHead) : false;

        if (hit) {
            // Alpha 检测：过滤透明像素内的点击
            bool alphaOk = m_live2DView ? m_live2DView->alphaTestAt(px, py) : false;
            if (alphaOk) {
                m_pressInHeadArea = isHead;
                m_pressInBodyArea = !isHead;
            } else {
                m_pressInHeadArea = false;
                m_pressInBodyArea = false;
            }
        } else {
            m_pressInHeadArea = false;
            m_pressInBodyArea = false;
        }

        m_pressStartPos = event->pos();
    }
}

void InputProxyWindow::mouseMoveEvent(QMouseEvent *event)
{
    if (m_dragging && (event->buttons() & Qt::LeftButton)) {
        // 拖拽中检查是否超出容差 → 取消点击资格
        if (m_pressInHeadArea || m_pressInBodyArea) {
            const int dist = (event->pos() - m_pressStartPos).manhattanLength();
            if (dist > 6) {
                m_pressInHeadArea = false;
                m_pressInBodyArea = false;
            }
        }
        handleDragMove(event);
    }
}

void InputProxyWindow::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && m_dragging) {
        m_dragging = false;

        // 松手时判定点击区域
        if (m_pressInHeadArea) {
            m_pressInHeadArea = false;
            qInfo() << "[InputProxy] headClicked → triggerPet";
            if (m_petWindow) m_petWindow->triggerPet();
        } else if (m_pressInBodyArea) {
            m_pressInBodyArea = false;
            qInfo() << "[InputProxy] bodyClicked → playTapMotion";
            if (m_live2DView) m_live2DView->playTapMotion();
        }

        emit dragFinished();
    }

    m_pressInHeadArea = false;
    m_pressInBodyArea = false;
}

void InputProxyWindow::handleDragMove(QMouseEvent *event)
{
    const QPoint newTopLeft = event->globalPosition().toPoint() - m_dragStartOffset;
    move(newTopLeft);

    // PetWindow 跟随
    if (m_petWindow) {
        m_petWindow->move(pos());
    }
}

// ============================================================================
// 右键菜单 → 转发到 PetWindow
// ============================================================================

void InputProxyWindow::contextMenuEvent(QContextMenuEvent *event)
{
    // 转发到 PetWindow 显示右键菜单（好感度/喂食/问候等）
    if (m_petWindow) {
        QCoreApplication::sendEvent(m_petWindow, event);
    }
}

// ============================================================================
// 绘制：零内容
// ============================================================================

void InputProxyWindow::paintEvent(QPaintEvent * /*event*/)
{
    // 不绘制任何内容 — 完全透明
}
