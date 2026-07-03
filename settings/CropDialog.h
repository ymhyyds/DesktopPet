#pragma once

#include <QDialog>
#include <QPixmap>
#include <QRect>
#include <QPoint>

class QLabel;

/**
 * @brief CropDialog — 自由裁剪图片选择区域，用于聊天/桌宠背景。
 *
 * 功能：
 *  - 按比例缩放显示完整图片
 *  - 半透明遮罩突出裁剪区域
 *  - 拖动裁剪框移动位置
 *  - 拖动边缘/角调整大小
 *  - 裁剪时保留原始分辨率
 */
class CropDialog : public QDialog
{
    Q_OBJECT

public:
    /// @param source  原始图片
    /// @param aspectW  裁剪框宽比（如 11）
    /// @param aspectH  裁剪框高比（如 14）
    /// @param parent   父窗口
    CropDialog(const QPixmap &source, int aspectW = 0, int aspectH = 0,
               QWidget *parent = nullptr);

    /// 裁剪后的图片（原始分辨率）
    QPixmap croppedImage() const { return m_cropped; }

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    /// 将裁剪框位置信息映射回原始图片坐标
    QRect cropRectOnSource() const;

    /// 在缩放后的图片坐标中初始化默认裁剪框
    void initCropRect();

    /// 将裁剪框 clamp 到图片范围内
    void clampCropRect();

    /// 根据鼠标位置返回要拖拽的边角类型
    enum Edge { None, Left, Right, Top, Bottom, TopLeft, TopRight, BottomLeft, BottomRight, Move };
    Edge hitTest(const QPoint &pos) const;

private:
    QPixmap m_source;           // 原始图
    QPixmap m_display;          // 缩放显示图
    QPixmap m_cropped;          // 裁剪结果

    QRect   m_cropRect;         // 裁剪框（在 m_display 坐标中）
    double  m_scale = 1.0;      // 原始图 → 显示图 缩放比
    QPoint  m_offset;           // 显示图在控件中的偏移（居中显示）

    double  m_aspectRatio = 0.0; // 0 = 自由比例

    // 拖拽状态
    Edge    m_activeEdge = None;
    QPoint  m_dragStart;
    QRect   m_dragStartRect;
    bool    m_dragging = false;

    static constexpr int kHandleSize = 10;   // 拖拽手柄大小
    static constexpr int kMinSize    = 40;   // 裁剪框最小尺寸
};
