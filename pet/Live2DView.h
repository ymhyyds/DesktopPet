#pragma once

#include <QOpenGLWidget>
#include <memory>
#include <map>
#include <QString>
#include <QImage>
#include <QRect>
#include <QVector>

#include "PetAnimationState.h"

struct GLFWwindow;
class ModelController;
class QTimer;
class QPainter;

class Live2DView : public QOpenGLWidget
{
    Q_OBJECT

public:
    explicit Live2DView(QWidget *parent = nullptr);
    ~Live2DView() override;

    void loadModel(const QString &modelDirPath);
    void setModelScale(double scale);

    void setEmotion(PetAnimationState emotion);

    void setHeadTrackingEnabled(bool enabled);
    bool headTrackingEnabled() const;

    void forceModelParamUpdate();

    void playTapMotion();

    void playMotionGroup(const QString &group, int no);

    int parameterCount() const;
    QString parameterId(int index) const;
    float parameterMinValue(int index) const;
    float parameterMaxValue(int index) const;
    float parameterDefaultValue(int index) const;
    float parameterCurrentValue(int index) const;
    void setParameterOverride(int index, float value);
    void clearParameterOverride(int index);
    void clearAllParameterOverrides();
    bool hasParameterOverrides() const;
    std::map<int, float> allParameterOverrides() const;
    void applyParameterOverrides(const std::map<int, float> &overrides);

    void setBackgroundImage(const QString &filePath);
    void clearBackground();
    void setBackgroundOpacity(double opacity);
    QString backgroundImagePath() const { return m_backgroundImagePath; }
    double backgroundOpacity() const { return m_backgroundOpacity; }

    QImage grabModelOnly();

    /// 判断一个窗口局部坐标是否在模型实际绘制区域内（排除动画安全边距）
    bool isPointInModelArea(const QPoint &localPos) const;

    /// 公开命中检测 — 供 InputProxyWindow 调用
    /// @return true=命中某个交互区域（模型有内容可交互）
    /// @param outIsHead true=头部命中，false=身体命中
    bool hitTestAt(float px, float py, bool &outIsHead) const;

    /// 公开 Alpha 检测 — 供 InputProxyWindow 调用
    /// @return true=像素透明度 >= threshold（有效点击）
    bool alphaTestAt(float px, float py, unsigned char threshold = 30);

    /// 返回模型紧贴包围盒在窗口像素坐标系中的矩形区域
    /// @return true=模型已加载且包围盒有效
    bool getModelPixelRect(float &outX0, float &outY0,
                           float &outW, float &outH) const;

    /// 获取所有可见 Drawable 的紧贴包围盒（窗口局部像素坐标）
    /// 用于构建 setMask 的 QRegion，按单个 Drawable 实际绘制区域裁剪窗口
    void getVisibleDrawableRects(QVector<QRect> &outRects) const;

    /// 获取模型头部锚点（窗口局部像素坐标）
    /// 优先级：Head HitArea 顶部中心 → Body-only 头部区域（35% 分割）顶部中心 → tight bounds 顶部中心
    /// @param[out] headCenterX 头部中心 X（窗口局部像素）
    /// @param[out] headTopY    头部顶部 Y（窗口局部像素）
    /// @param[out] modelPixelHeight 模型整体像素高度（用于动态 offset）
    /// @return true=计算成功
    bool getHeadAnchorPoint(float &headCenterX, float &headTopY,
                            float &modelPixelHeight) const;

signals:
    void modelLoaded(double canvasWidth, double canvasHeight,
                     double tightWidth, double tightHeight);

    /// idle 动画稳定后（~3秒）重新读取的顶点包围盒，比 modelLoaded 时的初始姿态更准确
    void stableBoundsReady(double tightWidth, double tightHeight);

    void headClicked();
    void bodyClicked();

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;

    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    void loadPendingModelIfNeeded();
    void drawErrorOverlay(QPainter &painter);
    void setStatusMessage(const QString &msg);

    void updateEyeTracking();

    void uploadBackgroundTexture();
    void drawBackgroundQuad();

    /// 计算顶点紧贴包围盒（tight bounds）在窗口像素空间中的区域
    bool computeModelArea(float &outX0, float &outY0,
                          float &outW, float &outH) const;

    /// 将窗口像素坐标转换为 Cubism 画布坐标（原点在中心）
    bool pixelToCanvas(float px, float py,
                       float &outCx, float &outCy) const;

    /// 每帧在 paintGL 中调用：计算头部锚点并写入缓存
    void computeAndCacheHeadAnchor();

    /// 完整命中检测：HitAreas → drawable 匹配 → 高度比例 fallback
    /// @param px,py 窗口像素坐标
    /// @param outIsHead true=头部命中，false=身体命中
    /// @return true=命中某个交互区域，false=空白区域无交互
    bool performHitTest(float px, float py, bool &outIsHead) const;

    /// Alpha 检测：读取帧缓冲中指定像素的透明度，过滤空白点击
    bool checkPixelAlpha(float px, float py,
                         unsigned char threshold = 30);

private:
    std::unique_ptr<ModelController> m_model;

    QString m_modelDirectory;
    bool    m_hasPendingModel = false;

    bool m_glReady = false;
    bool m_modelLoadFailed = false;
    QString m_statusMessage;
    int     m_textureCount = 0;

    bool   m_mouseCaptured = false;

    bool   m_headTrackingEnabled = false;

    bool   m_pressInHeadArea = false;
    bool   m_pressInBodyArea = false;
    QPoint m_pressStartPos;

    GLFWwindow *m_hiddenGlfwWindow = nullptr;
    QTimer     *m_frameTimer = nullptr;

    QImage   m_backgroundImage;
    QString  m_backgroundImagePath;
    GLuint   m_backgroundTexture = 0;
    double   m_backgroundOpacity = 0.3;
    bool     m_backgroundTexDirty = false;

    bool     m_screenshotMode = false;

    // ★ 顶点紧贴包围盒缓存（画布坐标系），用于命中检测只响应实际顶点覆盖区域
    double m_tightMinX    = 0.0;
    double m_tightMinY    = 0.0;
    double m_tightWidth   = 0.0;
    double m_tightHeight  = 0.0;
    double m_canvasWidth  = 0.0;
    double m_canvasHeight = 0.0;

    // ★ 头部锚点缓存（每帧 paintGL 中更新，浮动文字定时器只读缓存，零额外遍历开销）
    float  m_cachedHeadCenterX      = 0.0f;
    float  m_cachedHeadTopY         = 0.0f;
    float  m_cachedModelPixelHeight = 0.0f;
};
