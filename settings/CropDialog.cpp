#include "CropDialog.h"

#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QLabel>
#include <QApplication>

// ============================================================================
// 构造
// ============================================================================

CropDialog::CropDialog(const QPixmap &source, int aspectW, int aspectH, QWidget *parent)
    : QDialog(parent)
    , m_source(source)
{
    setWindowTitle(QStringLiteral("裁剪图片"));
    setMinimumSize(500, 400);
    resize(800, 650);

    if (aspectW > 0 && aspectH > 0) {
        m_aspectRatio = static_cast<double>(aspectW) / aspectH;
    }

    // 按钮 — 使用 QDialogButtonBox 自动适配系统主题
    auto *buttonBox = new QDialogButtonBox(this);
    auto *okBtn = buttonBox->addButton(QStringLiteral("✓ 确认裁剪"), QDialogButtonBox::AcceptRole);
    auto *cancelBtn = buttonBox->addButton(QStringLiteral("✕ 取消"), QDialogButtonBox::RejectRole);
    okBtn->setFixedHeight(36);
    cancelBtn->setFixedHeight(36);

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 8);
    mainLayout->addWidget(new QLabel(QStringLiteral(
        "<div style='text-align:center; color:gray; font-size:12px; padding:4px;'>"
        "拖动裁剪框移动 · 拖拽边缘调整大小 · 滚轮缩放"
        "</div>"
    ), this));
    // 图片区域用 stretch 占满
    mainLayout->addStretch(1);
    mainLayout->addWidget(buttonBox);
    setLayout(mainLayout);

    connect(buttonBox, &QDialogButtonBox::accepted, this, [this]() {
        m_cropped = m_source.copy(cropRectOnSource());
        if (!m_cropped.isNull()) {
            accept();
        }
    });
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    // 初始裁剪框
    initCropRect();
}

// ============================================================================
// 坐标计算
// ============================================================================

void CropDialog::initCropRect()
{
    if (m_source.isNull()) return;

    // 计算缩放和居中偏移（保持宽高比）
    const int cw = contentsRect().width();
    const int ch = contentsRect().height() - 80; // 减去按钮区域
    const double srcAspect = static_cast<double>(m_source.width()) / m_source.height();

    if (srcAspect > static_cast<double>(cw) / ch) {
        m_scale = static_cast<double>(cw) / m_source.width();
        m_display = m_source.scaledToWidth(cw, Qt::SmoothTransformation);
    } else {
        m_scale = static_cast<double>(ch) / m_source.height();
        m_display = m_source.scaledToHeight(ch, Qt::SmoothTransformation);
    }
    m_offset = QPoint((cw - m_display.width()) / 2,
                      (ch - m_display.height()) / 2 + 10);

    // 默认裁剪框：居中 80% 尺寸
    const int dw = static_cast<int>(m_display.width() * 0.8);
    const int dh = static_cast<int>(m_display.height() * 0.8);

    if (m_aspectRatio > 0.0) {
        // 锁定比例：以较短边为基准
        if (dw / dh > m_aspectRatio) {
            // 裁剪框太宽 → 以高为准
            const int w = static_cast<int>(dh * m_aspectRatio);
            m_cropRect = QRect((m_display.width() - w) / 2,
                               (m_display.height() - dh) / 2,
                               w, dh);
        } else {
            const int h = static_cast<int>(dw / m_aspectRatio);
            m_cropRect = QRect((m_display.width() - dw) / 2,
                               (m_display.height() - h) / 2,
                               dw, h);
        }
    } else {
        m_cropRect = QRect((m_display.width() - dw) / 2,
                           (m_display.height() - dh) / 2,
                           dw, dh);
    }

    m_dragging = false;
    m_activeEdge = None;
}

void CropDialog::clampCropRect()
{
    QRect r = m_cropRect;
    const int dw = m_display.width();
    const int dh = m_display.height();

    // 确保最小尺寸
    r.setWidth(qMax(kMinSize, r.width()));
    r.setHeight(qMax(kMinSize, r.height()));

    if (m_aspectRatio > 0.0) {
        // 锁定比例：迭代修正位置 & 尺寸直到稳定
        for (int i = 0; i < 3; i++) {
            // 先 clamp 位置
            r.moveLeft(qBound(0, r.left(), dw - r.width()));
            r.moveTop(qBound(0, r.top(), dh - r.height()));

            // 以宽为基准调高
            r.setHeight(qRound(r.width() / m_aspectRatio));
            if (r.bottom() > dh) {
                r.moveBottom(dh);
                int newW = qRound(r.height() * m_aspectRatio);
                if (newW != r.width()) {
                    r.setWidth(newW);
                    continue;   // 宽度变了，需要再检查
                }
            }
            if (r.right() > dw) {
                r.moveRight(dw);
                int newH = qRound(r.width() / m_aspectRatio);
                if (newH != r.height()) {
                    r.setHeight(newH);
                    continue;
                }
            }
            break;              // 收敛了
        }
        // 收尾 clamp（比例修正可能又把位置推偏了）
        r.moveLeft(qBound(0, r.left(), dw - r.width()));
        r.moveTop(qBound(0, r.top(), dh - r.height()));
    } else {
        // 无比例约束：直接 clamp 位置不越界
        r.moveLeft(qBound(0, r.left(), dw - r.width()));
        r.moveTop(qBound(0, r.top(), dh - r.height()));
    }

    // 最终确保不低于最小尺寸
    if (r.width() < kMinSize) r.setWidth(kMinSize);
    if (r.height() < kMinSize) r.setHeight(kMinSize);

    m_cropRect = r;
}

QRect CropDialog::cropRectOnSource() const
{
    // 显示坐标 → 原始坐标（m_cropRect 位于显示图上，无需减 offset）
    const int sx = qRound(m_cropRect.x() / m_scale);
    const int sy = qRound(m_cropRect.y() / m_scale);
    const int sw = qRound(m_cropRect.width() / m_scale);
    const int sh = qRound(m_cropRect.height() / m_scale);

    return QRect(sx, sy, sw, sh).intersected(m_source.rect());
}

// ============================================================================
// 命中测试
// ============================================================================

CropDialog::Edge CropDialog::hitTest(const QPoint &pos) const
{
    // 控件坐标 → 显示坐标（减去图片偏移）
    const QPoint dp = pos - m_offset;
    const QRect r = m_cropRect;
    const int h = kHandleSize;

    // 角
    if (QRect(r.topLeft() - QPoint(0, 0), QSize(h, h)).contains(dp)) return TopLeft;
    if (QRect(r.topRight() - QPoint(h, 0), QSize(h, h)).contains(dp)) return TopRight;
    if (QRect(r.bottomLeft() - QPoint(0, h), QSize(h, h)).contains(dp)) return BottomLeft;
    if (QRect(r.bottomRight() - QPoint(h, h), QSize(h, h)).contains(dp)) return BottomRight;

    // 边
    if (QRect(r.left() - 3, r.top() + h, 6, r.height() - 2 * h).contains(dp)) return Left;
    if (QRect(r.right() - 3, r.top() + h, 6, r.height() - 2 * h).contains(dp)) return Right;
    if (QRect(r.left() + h, r.top() - 3, r.width() - 2 * h, 6).contains(dp)) return Top;
    if (QRect(r.left() + h, r.bottom() - 3, r.width() - 2 * h, 6).contains(dp)) return Bottom;

    // 内部区域 → 移动
    if (r.contains(dp)) return Move;

    return None;
}

// ============================================================================
// 绘制
// ============================================================================

void CropDialog::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.setRenderHint(QPainter::Antialiasing);

    const QRect area = contentsRect();
    p.fillRect(area, QColor(40, 40, 40));

    // 绘制图片
    if (!m_display.isNull()) {
        p.drawPixmap(m_offset, m_display);
    } else {
        p.setPen(Qt::white);
        p.drawText(area, Qt::AlignCenter, QStringLiteral("无法加载图片"));
        return;
    }

    // 裁剪框外半透明遮罩
    p.setClipRect(area);
    const QRect cr = m_cropRect;
    const QRect crWidget = cr.translated(m_offset); // 转到控件坐标
    QPainterPath path;
    path.addRect(area);
    path.addRect(crWidget);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, 140));
    p.setCompositionMode(QPainter::CompositionMode_SourceOver);
    p.drawPath(path);

    // 裁剪框边框
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(255, 255, 255), 2));
    p.drawRect(crWidget);

    // 四角手柄
    const int hs = kHandleSize;
    p.setBrush(QColor(255, 255, 255));
    p.setPen(QPen(QColor(0, 0, 0), 1));
    p.drawRect(QRect(crWidget.topLeft() - QPoint(0, 0), QSize(hs, hs)));
    p.drawRect(QRect(crWidget.topRight() - QPoint(hs, 0), QSize(hs, hs)));
    p.drawRect(QRect(crWidget.bottomLeft() - QPoint(0, hs), QSize(hs, hs)));
    p.drawRect(QRect(crWidget.bottomRight() - QPoint(hs, hs), QSize(hs, hs)));

    // 尺寸信息
    const QRect srcCrop = cropRectOnSource();
    QString info = QStringLiteral("%1 × %2").arg(srcCrop.width()).arg(srcCrop.height());
    p.setPen(Qt::white);
    p.setFont(QFont(QString(), 12));
    p.drawText(crWidget.left() + 6, crWidget.top() - 10, info);
}

// ============================================================================
// 鼠标事件
// ============================================================================

void CropDialog::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        const QPoint pos = event->pos();
        Edge edge = hitTest(pos);
        if (edge != None) {
            m_dragging = true;
            m_activeEdge = edge;
            m_dragStart = pos;
            m_dragStartRect = m_cropRect;
            setCursor(edge == Move ? Qt::SizeAllCursor : Qt::CrossCursor);
        }
    }
}

void CropDialog::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_dragging) {
        // 更新光标
        Edge edge = hitTest(event->pos());
        switch (edge) {
        case TopLeft:
        case BottomRight: setCursor(Qt::SizeFDiagCursor); break;
        case TopRight:
        case BottomLeft: setCursor(Qt::SizeBDiagCursor); break;
        case Left:
        case Right: setCursor(Qt::SizeHorCursor); break;
        case Top:
        case Bottom: setCursor(Qt::SizeVerCursor); break;
        case Move: setCursor(Qt::SizeAllCursor); break;
        default: setCursor(Qt::ArrowCursor); break;
        }
        return;
    }

    const QPoint delta = event->pos() - m_dragStart;
    QRect r = m_dragStartRect;

    auto enforceWidthAspect = [&]() {
        if (m_aspectRatio > 0.0)
            r.setHeight(qRound(r.width() / m_aspectRatio));
    };
    auto enforceHeightAspect = [&]() {
        if (m_aspectRatio > 0.0)
            r.setWidth(qRound(r.height() * m_aspectRatio));
    };

    switch (m_activeEdge) {
    case Move:
        r.translate(delta);
        break;
    case Left:
        r.setLeft(r.left() + delta.x());
        enforceWidthAspect();
        break;
    case Right:
        r.setRight(r.right() + delta.x());
        enforceWidthAspect();
        break;
    case Top:
        r.setTop(r.top() + delta.y());
        enforceHeightAspect();
        break;
    case Bottom:
        r.setBottom(r.bottom() + delta.y());
        enforceHeightAspect();
        break;
    case TopLeft:
        r.setTopLeft(r.topLeft() + delta);
        enforceWidthAspect();
        break;
    case TopRight:
        r.setTopRight(r.topRight() + delta);
        enforceWidthAspect();
        break;
    case BottomLeft:
        r.setBottomLeft(r.bottomLeft() + delta);
        enforceWidthAspect();
        break;
    case BottomRight:
        r.setBottomRight(r.bottomRight() + delta);
        enforceWidthAspect();
        break;
    default: break;
    }

    m_cropRect = r;
    clampCropRect();
    update();
}

void CropDialog::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && m_dragging) {
        m_dragging = false;
        m_activeEdge = None;
        setCursor(Qt::ArrowCursor);
    }
}

void CropDialog::wheelEvent(QWheelEvent *event)
{
    // 以裁剪框中心为基准缩放，滚轮上推放大，下滚缩小
    const double step = 0.05; // 每次 ±5%
    const double factor = 1.0 + (event->angleDelta().y() > 0 ? step : -step);

    QPointF center = m_cropRect.center();
    int newW = qRound(m_cropRect.width() * factor);
    int newH = qRound(m_cropRect.height() * factor);

    // 保持最小尺寸
    if (newW < kMinSize) { newW = kMinSize; newH = qRound(newW / (m_aspectRatio > 0.0 ? m_aspectRatio : 1.0)); }
    if (newH < kMinSize) { newH = kMinSize; newW = qRound(newH * (m_aspectRatio > 0.0 ? m_aspectRatio : 1.0)); }

    // 不能超过显示图尺寸
    const int dw = m_display.width();
    const int dh = m_display.height();
    if (newW > dw) newW = dw;
    if (newH > dh) newH = dh;

    m_cropRect.setSize(QSize(newW, newH));
    m_cropRect.moveCenter(center.toPoint());

    clampCropRect();
    update();
    event->accept();
}

void CropDialog::resizeEvent(QResizeEvent *event)
{
    QDialog::resizeEvent(event);
    // 重新计算布局
    initCropRect();
    update();
}

void CropDialog::keyPressEvent(QKeyEvent *event)
{
    // 方向键微调裁剪框
    int dx = 0, dy = 0;
    switch (event->key()) {
    case Qt::Key_Left:  dx = -1; break;
    case Qt::Key_Right: dx = 1;  break;
    case Qt::Key_Up:    dy = -1; break;
    case Qt::Key_Down:  dy = 1;  break;
    case Qt::Key_Return:
    case Qt::Key_Enter: {
        m_cropped = m_source.copy(cropRectOnSource());
        if (!m_cropped.isNull()) accept();
        return;
    }
    case Qt::Key_Escape:
        reject();
        return;
    default:
        QDialog::keyPressEvent(event);
        return;
    }

    m_cropRect.translate(dx, dy);
    clampCropRect();
    update();
}
