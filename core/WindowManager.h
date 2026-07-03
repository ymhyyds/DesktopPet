#pragma once

#include <QObject>
#include <QPoint>
#include <QRect>
#include <QPointer>
#include <QVector>

class QWidget;

/**
 * @brief WindowManager 专门处理"窗口该出现在屏幕的什么位置、怎么贴边、怎么置顶"，
 *        不涉及任何桌宠业务逻辑（动画/AI/交互都不归它管）。
 *
 * 之所以单独拆出来，是因为这部分逻辑和"桌宠是什么"无关，纯粹是窗口几何行为，
 * 未来如果要支持多个桌宠窗口同屏、或者支持多显示器布局策略，
 * 只需要扩展这个类，PetWindow 本身不用动。
 */
class WindowManager : public QObject
{
    Q_OBJECT

public:
    explicit WindowManager(QObject *parent = nullptr);

    /// 把窗口设置为无边框透明、永远置顶（窗口标志的统一设置入口）。
    void registerWindow(QWidget *window);

    /// 把窗口放到默认初始位置（屏幕右下角，留出一定边距）。
    void placeAtDefaultPosition(QWidget *window);

    /**
     * @brief 检测窗口当前位置是否靠近屏幕边缘，如果靠近就贴边吸附。
     *        建议在窗口拖拽结束（mouseReleaseEvent）后调用。
     */
    void trySnapToEdge(QWidget *window);

    /// 吸附判定的触发距离（像素），越大越容易触发吸附。
    void setSnapThreshold(int pixels) { m_snapThreshold = pixels; }
    int snapThreshold() const { return m_snapThreshold; }

    /// 重新对所有已注册窗口做一次置顶刷新（比如全屏应用切换后调用）。
    void refreshTopMost();

private:
    QRect currentScreenGeometry(const QWidget *window) const;

private:
    QVector<QPointer<QWidget>> m_managedWindows;
    int m_snapThreshold = 24; // 像素
    int m_screenMargin  = 16; // 默认放置时离屏幕边缘的间距
};
