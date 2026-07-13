// GLEW 必须是第一个被包含的 OpenGL 相关头文件
#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include "Live2DView.h"

#include <CubismFramework.hpp>

#include "ModelController.hpp"
#include "LAppPal.hpp"
#include "LAppDefine.hpp"
#include "LAppAllocator_Common.hpp"
#include "MouseActionManager.hpp"

#include "PetWindow.h"
#include "../interaction/DrawableBounds.h"
#include "../interaction/InputRegionBuilder.h"

#include <QTimer>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QDateTime>
#include <QSurfaceFormat>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QOpenGLContext>
#include <QDir>
#include <QPainter>
#include <QPointF>
#include <QRegion>
#include <QMouseEvent>
#include <QCursor>
#include <climits>

namespace {
void writeDebugLog(const QString &message)
{
    QFile file(QStringLiteral("live2d_debug_log.txt"));
    if (file.open(QIODevice::Append | QIODevice::Text)) {
        QTextStream stream(&file);
        stream << QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz"))
               << " " << message << "\n";
    }
}
}

#define LOG_DEBUG(msg) do { qDebug().noquote() << "[Live2DView]" << msg; writeDebugLog(msg); } while(false)

namespace {
LAppAllocator_Common g_cubismAllocator;
int g_cubismFrameworkRefCount = 0;
bool g_glfwInitialized = false;
}

// ============================================================================
// 构造 / 析构
// ============================================================================

Live2DView::Live2DView(QWidget *parent)
    : QOpenGLWidget(parent)
{
    QSurfaceFormat fmt;
    fmt.setAlphaBufferSize(8);
    fmt.setRedBufferSize(8);
    fmt.setGreenBufferSize(8);
    fmt.setBlueBufferSize(8);
    fmt.setDepthBufferSize(24);
    fmt.setStencilBufferSize(8);
    fmt.setSamples(0);
    fmt.setSwapBehavior(QSurfaceFormat::DoubleBuffer);
    fmt.setRenderableType(QSurfaceFormat::OpenGL);
    fmt.setSwapInterval(0);  // 禁用 vsync，避免 QTimer 与显示器刷新不同步导致卡顿
    setFormat(fmt);

    setAttribute(Qt::WA_TranslucentBackground, true);
    setAutoFillBackground(false);

    // 启用鼠标追踪：不按下按键也能收到 mouseMoveEvent
    setMouseTracking(true);

    m_frameTimer = new QTimer(this);
    m_frameTimer->setTimerType(Qt::PreciseTimer);  // 避免 CoarseTimer 合并导致 deltaTime 波动
    connect(m_frameTimer, &QTimer::timeout, this, [this]() {
        // 每帧追踪全局鼠标位置 → 驱动眼球（即使光标不在窗口内）
        updateEyeTracking();
        update();
    });
    m_frameTimer->start(17); // ~59fps，略低于 60fps 避免 vsync 竞争导致帧间隔波动

    // 身体点击 → 播放随机动作
    connect(this, &Live2DView::bodyClicked, this, [this]() {
        playTapMotion();
    });
}

Live2DView::~Live2DView()
{
    makeCurrent();

    if (m_backgroundTexture) {
        glDeleteTextures(1, &m_backgroundTexture);
        m_backgroundTexture = 0;
    }

    m_model.reset();
    doneCurrent();

    if (m_hiddenGlfwWindow) {
        glfwDestroyWindow(m_hiddenGlfwWindow);
        m_hiddenGlfwWindow = nullptr;
    }

    --g_cubismFrameworkRefCount;
    if (g_cubismFrameworkRefCount <= 0) {
        Csm::CubismFramework::Dispose();
        g_cubismFrameworkRefCount = 0;
    }
    MouseActionManager::ReleaseInstance();
}

// ============================================================================
// 自动修复 model3.json：补全缺失的 Motions / Expressions / EyeBlink 引用
// 这样用户把 Live2D 模型丢进 characters/ 就能直接用，不用手动改配置
// ============================================================================
static void autoPatchModelJson(const QString &modelDirPath)
{
    QDir modelDir(modelDirPath);
    const QStringList modelJsonFiles = modelDir.entryList({QStringLiteral("*.model3.json")}, QDir::Files);
    if (modelJsonFiles.isEmpty()) return;

    const QString modelJsonPath = modelDir.absoluteFilePath(modelJsonFiles.first());
    QFile file(modelJsonPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return;

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &err);
    file.close();
    if (err.error != QJsonParseError::NoError || !doc.isObject()) return;

    QJsonObject root = doc.object();
    QJsonObject fileRefs = root.value(QStringLiteral("FileReferences")).toObject();
    bool modified = false;

    // ── 补全 Motions（如果在目录里找到了 .motion3.json 但 model3.json 没引用的） ──
    if (!fileRefs.contains(QStringLiteral("Motions"))) {
        const QStringList motionFiles = modelDir.entryList(
            {QStringLiteral("*.motion3.json")}, QDir::Files);
        if (!motionFiles.isEmpty()) {
            QJsonObject motions;
            QJsonArray idleArray;
            for (const QString &mf : motionFiles) {
                QJsonObject entry;
                entry[QStringLiteral("File")] = mf;
                entry[QStringLiteral("FadeInTime")] = 0.5;
                entry[QStringLiteral("FadeOutTime")] = 0.5;
                idleArray.append(entry);
            }
            motions[QStringLiteral("Idle")] = idleArray;
            fileRefs[QStringLiteral("Motions")] = motions;
            modified = true;
            qInfo().noquote() << "[ModelPatch] 发现未引用的动画文件，已自动补全 Motions:"
                              << motionFiles;
        }
    }

    // ── 补全 Expressions（如果在目录里找到了 .exp3.json 但没引用的） ──
    if (!fileRefs.contains(QStringLiteral("Expressions"))) {
        const QStringList expFiles = modelDir.entryList(
            {QStringLiteral("*.exp3.json")}, QDir::Files);
        if (!expFiles.isEmpty()) {
            QJsonArray expressions;
            for (const QString &ef : expFiles) {
                QJsonObject entry;
                // 用去掉扩展名的文件名做表情名
                QString name = ef;
                if (name.endsWith(QStringLiteral(".exp3.json")))
                    name.chop(10); // strlen(".exp3.json") == 10
                entry[QStringLiteral("Name")] = name;
                entry[QStringLiteral("File")] = ef;
                expressions.append(entry);
            }
            fileRefs[QStringLiteral("Expressions")] = expressions;
            modified = true;
            qInfo().noquote() << "[ModelPatch] 发现未引用的表情文件，已自动补全 Expressions:"
                              << expFiles;
        }
    }

    if (modified) {
        root[QStringLiteral("FileReferences")] = fileRefs;
    }

    // ── 补全 EyeBlink Group 的 Ids（如果 Group 存在但 Ids 为空） ──
    //    标准眨眼参数是 ParamEyeLOpen 和 ParamEyeROpen，绝大多数 Live2D 模型都有
    if (root.contains(QStringLiteral("Groups"))) {
        QJsonArray groups = root[QStringLiteral("Groups")].toArray();
        for (int i = 0; i < groups.size(); ++i) {
            QJsonObject g = groups[i].toObject();
            if (g[QStringLiteral("Name")].toString() == QStringLiteral("EyeBlink")
                && g[QStringLiteral("Ids")].toArray().isEmpty())
            {
                QJsonArray ids;
                ids.append(QStringLiteral("ParamEyeLOpen"));
                ids.append(QStringLiteral("ParamEyeROpen"));
                g[QStringLiteral("Ids")] = ids;
                groups[i] = g;
                root[QStringLiteral("Groups")] = groups;
                modified = true;
                qInfo().noquote() << "[ModelPatch] EyeBlink Group Ids 为空，已自动填入标准参数";
                break;
            }
        }
    }

    if (!modified) return;

    // 写回文件
    QFile writeFile(modelJsonPath);
    if (!writeFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "[ModelPatch] 无法写入 model3.json：" << modelJsonPath;
        return;
    }
    writeFile.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    writeFile.close();
    qInfo().noquote() << "[ModelPatch] model3.json 已自动修复：" << modelJsonFiles.first();
}

// ============================================================================
// 模型加载
// ============================================================================

void Live2DView::loadModel(const QString &modelDirPath)
{
    if (modelDirPath.isEmpty()) {
        LOG_DEBUG("loadModel 收到空路径，忽略");
        return;
    }

    m_modelDirectory  = modelDirPath; // 持久保存，GL 重建后可重载
    m_hasPendingModel = true;

    if (m_glReady) {
        // 必须在 GL 上下文中释放旧模型 + 加载新模型
        // 因为 SetupModel() → CreateRenderer() + SetupTextures() 都需要 GL 上下文
        makeCurrent();

        if (m_model) {
            m_model.reset();
        }
        loadPendingModelIfNeeded();

        doneCurrent();
        update();
    }
}

QString Live2DView::modelName() const
{
    if (m_modelDirectory.isEmpty())
        return {};
    QString name = m_modelDirectory;
    if (name.endsWith(QLatin1Char('/')))
        name.chop(1);
    const int idx = name.lastIndexOf(QLatin1Char('/'));
    return idx >= 0 ? name.mid(idx + 1) : name;
}

void Live2DView::setModelScale(double scale)
{
    // m_modelScale 已移除：模型缩放完全由 PetWindow::applyTightSize() 通过窗口大小控制，
    // 这里只触发射频刷新（Live2DView 本身不参与缩放计算）
    Q_UNUSED(scale);
    if (m_model) {
        update();
    }
}

void Live2DView::setEmotion(PetAnimationState emotion)
{
    if (m_model) {
        m_model->setEmotion(emotion);
    }
}

void Live2DView::setHeadTrackingEnabled(bool enabled)
{
    m_headTrackingEnabled = enabled; // 持久保存，模型重载后恢复
    if (m_model) {
        m_model->setHeadTrackingEnabled(enabled);
    }
}

bool Live2DView::headTrackingEnabled() const
{
    return m_headTrackingEnabled;
}

void Live2DView::forceModelParamUpdate()
{
    if (!m_model || !m_glReady) return;
    makeCurrent();
    LAppPal::UpdateTime();
    m_model->forceParamUpdate();
    doneCurrent();
}

// ============================================================================
// 🛠️ 模型参数面板 — 委托给 ModelController
// ============================================================================

int Live2DView::parameterCount() const
{
    return m_model ? m_model->getParameterCount() : 0;
}

QString Live2DView::parameterId(int index) const
{
    return m_model ? m_model->getParameterId(index) : QString();
}

float Live2DView::parameterMinValue(int index) const
{
    return m_model ? m_model->getParameterMinimumValue(index) : 0.0f;
}

float Live2DView::parameterMaxValue(int index) const
{
    return m_model ? m_model->getParameterMaximumValue(index) : 0.0f;
}

float Live2DView::parameterDefaultValue(int index) const
{
    return m_model ? m_model->getParameterDefaultValue(index) : 0.0f;
}

float Live2DView::parameterCurrentValue(int index) const
{
    return m_model ? m_model->getParameterCurrentValue(index) : 0.0f;
}

void Live2DView::setParameterOverride(int index, float value)
{
    if (m_model) m_model->setUserParameterOverride(index, value);
}

void Live2DView::clearParameterOverride(int index)
{
    if (m_model) m_model->clearUserParameterOverride(index);
}

void Live2DView::clearAllParameterOverrides()
{
    if (m_model) m_model->clearAllUserOverrides();
}

bool Live2DView::hasParameterOverrides() const
{
    return m_model && m_model->hasUserOverrides();
}

std::map<int, float> Live2DView::allParameterOverrides() const
{
    return m_model ? m_model->getAllUserOverrides() : std::map<int, float>();
}

void Live2DView::applyParameterOverrides(const std::map<int, float> &overrides)
{
    if (m_model) m_model->applyUserOverrides(overrides);
}

void Live2DView::setStatusMessage(const QString &msg)
{
    m_statusMessage = msg;
    LOG_DEBUG(msg);
}

void Live2DView::loadPendingModelIfNeeded()
{
    // 有模型目录但没有待加载标记 → GL 上下文被重建了，需要重载
    if (!m_hasPendingModel && !m_modelDirectory.isEmpty() && !m_model) {
        m_hasPendingModel = true;
    }
    if (!m_hasPendingModel) return;

    m_hasPendingModel = false;
    m_modelLoadFailed = false;
    m_textureCount = 0;

    const std::string fullDir = m_modelDirectory.toStdString();
    if (fullDir.empty()) {
        setStatusMessage(QStringLiteral("模型目录路径为空"));
        m_modelLoadFailed = true;
        return;
    }

    // 检查目录是否存在
    if (!QDir(m_modelDirectory).exists()) {
        setStatusMessage(QStringLiteral("模型目录不存在: %1").arg(m_modelDirectory));
        m_modelLoadFailed = true;
        return;
    }

    // 检查 model3.json
    QDir modelDir(m_modelDirectory);
    const QStringList modelFiles = modelDir.entryList(QStringList() << "*.model3.json", QDir::Files);
    if (modelFiles.isEmpty()) {
        setStatusMessage(QStringLiteral("目录中未找到 .model3.json 文件: %1").arg(m_modelDirectory));
        m_modelLoadFailed = true;
        return;
    }

    // ★ 自动修复 model3.json：补全缺失的 Motions / Expressions / EyeBlink
    //    这样用户直接把 Live2D 模型丢进 characters/ 就能直接用，不用手动改配置
    autoPatchModelJson(m_modelDirectory);

    // ★ 用实际检测到的 .model3.json 文件基名（不是目录名！）
    //    有些模型文件夹名和 .model3.json 文件名不一致（如 MaidCyrene20251105/ 内有 MaidCyrene.model3.json）
    //    而 ModelController::SetupModel 内部根据此名称拼装 .model3.json 路径
    //    注意：不能使用 QFileInfo::completeBaseName()，因为它对 "Hiyori.model3.json"
    //    只剥离最后的 .json 返回 "Hiyori.model3"，导致后续拼接成 "Hiyori.model3.model3.json"
    const QString modelJsonFile = modelFiles.first();
    QString modelJsonBaseName = modelJsonFile;
    if (modelJsonBaseName.endsWith(QLatin1String(".model3.json")))
        modelJsonBaseName.chop(12); // strlen(".model3.json") == 12
    const std::string modelNameStd = modelJsonBaseName.toStdString();

    LOG_DEBUG(QStringLiteral("开始加载模型，目录：%1，.model3.json：%2，基名：%3，控件尺寸：%4x%5")
              .arg(m_modelDirectory).arg(modelJsonFile).arg(modelJsonBaseName)
              .arg(width()).arg(height()));

    setStatusMessage(QStringLiteral("正在加载模型: %1...").arg(modelJsonBaseName));

    m_model = std::make_unique<ModelController>(modelNameStd, fullDir);
    m_model->SetupModel(
        static_cast<Csm::csmUint32>(width()),
        static_cast<Csm::csmUint32>(height())
    );

    // 验证
    if (m_model->GetModel() == nullptr || m_model->GetModel()->GetCanvasWidth() <= 0.0f) {
        setStatusMessage(QStringLiteral("模型加载后数据为空，可能 model3.json 解析失败"));
        m_model.reset();
        m_modelLoadFailed = true;
        return;
    }

    const auto &bindedTextures = m_model->GetRenderer<Csm::Rendering::CubismRenderer_OpenGLES2>()->GetBindedTextures();
    m_textureCount = bindedTextures.GetSize();

    const float canvasW = m_model->GetModel()->GetCanvasWidth();
    const float canvasH = m_model->GetModel()->GetCanvasHeight();
    LOG_DEBUG(QStringLiteral("模型加载成功，纹理数量：%1，画布尺寸：%2x%3")
              .arg(m_textureCount).arg(canvasW).arg(canvasH));

    // ★ 已验证 IsBlendModeEnabled=0，SetRenderTargetSize 是空操作 → setRendererTargetSize() 已整体移除
    LOG_DEBUG(QStringLiteral("IsBlendModeEnabled=%1 (模型无离屏混合 → SetRenderTargetSize 已移除)")
              .arg(m_model->GetModel()->IsBlendModeEnabled()));

    // 注册到 MouseActionManager
    MouseActionManager::GetInstance()->SetUserModel(m_model.get());

    // ★ 恢复用户设置的状态（新 ModelController 默认为关闭）
    if (m_headTrackingEnabled) {
        m_model->setHeadTrackingEnabled(true);
    }

    // 计算模型实际绘制区域的包围盒（紧贴尺寸），用于命中检测 + PetWindow 窗口大小
    double tightMinX = 0.0, tightMinY = 0.0, tightW = 0.0, tightH = 0.0;
    m_model->getModelBounds(tightMinX, tightMinY, tightW, tightH);
    m_tightMinX = tightMinX;
    m_tightMinY = tightMinY;
    m_tightWidth = tightW;
    m_tightHeight = tightH;
    m_canvasWidth = static_cast<double>(canvasW);
    m_canvasHeight = static_cast<double>(canvasH);

    // 通知 PetWindow 模型已加载，提供画布尺寸和紧贴包围盒尺寸
    emit modelLoaded(static_cast<double>(canvasW), static_cast<double>(canvasH),
                     tightW, tightH);

    // ★ 能力日志：输出模型的预制表情和点击区域概况
    if (m_model) {
        QVector<ModelController::HitAreaAABB> headAreas, bodyAreas;
        m_model->getInteractionAreas(headAreas, bodyAreas);
        const QString header = QStringLiteral("\n  ╔══════════════════════════════════╗\n"
                                              "  ║  Model Capability Report        ║\n"
                                              "  ╚══════════════════════════════════╝");
        qInfo().noquote() << header;
        qInfo().noquote() << QStringLiteral("  Model: %1").arg(modelJsonBaseName);
        qInfo().noquote() << QStringLiteral("  Expressions loaded: %1").arg(m_model->expressionCount());
        qInfo().noquote() << QStringLiteral("  Head HitAreas: %1").arg(headAreas.size());
        qInfo().noquote() << QStringLiteral("  Body HitAreas: %1").arg(bodyAreas.size());
        qInfo().noquote() << QStringLiteral("  Head expression click enabled: %1")
                             .arg(m_model->hasExpressions() ? QStringLiteral("yes") : QStringLiteral("no"));
    }

    setStatusMessage(QString()); // 成功 → 清空状态信息，不再显示覆盖层

    // 置标志：下次 computeAndCacheHeadAnchor 后写一次锚点摘要日志
    m_needsAnchorLog = true;
    m_lastAnchorSource.clear();

    // ★ 切换模型后恢复 TTS 说话状态
    //    新 ModelController 的 loadLipSyncGroup() 已在 SetupModel 中调用，
    //    需要重新传递当前的 m_isSpeaking
    if (m_isSpeaking && m_model && m_model->isLipSyncSupported()) {
        m_model->setSpeaking(true);
        qInfo() << "[LipSync] 模型切换后继承说话状态";
    }

    // ★ 等 idle 动画启动后重新读取包围盒（500ms 足够 idle 开始播放）
    //    初始姿态（T-pose）的包围盒偏大，idle 动画播放后更接近实际视觉大小。
    //    切换角色时也会重新触发此延迟，每个角色都用自己的稳定包围盒。
    QTimer::singleShot(500, this, [this]() {
        if (!m_model) return;
        double minX = 0.0, minY = 0.0, tightW = 0.0, tightH = 0.0;
        m_model->getModelBounds(minX, minY, tightW, tightH);
        if (tightW > 0.0 && tightH > 0.0) {
            m_tightMinX = minX;
            m_tightMinY = minY;
            m_tightWidth = tightW;
            m_tightHeight = tightH;
            emit stableBoundsReady(tightW, tightH);
            LOG_DEBUG(QStringLiteral("[稳定包围盒] tight=%1x%2 minPos=%3,%4")
                      .arg(tightW, 0, 'f', 4).arg(tightH, 0, 'f', 4)
                      .arg(minX, 0, 'f', 4).arg(minY, 0, 'f', 4));
        }
    });

    // ★ 新模型 → 旧 Alpha 快照失效（渲染内容已变）
    invalidateAlphaCapture();
}

// ============================================================================
// OpenGL 生命周期
// ============================================================================

void Live2DView::initializeGL()
{
    LOG_DEBUG(QStringLiteral("initializeGL，控件尺寸：%1x%2，devicePixelRatio=%3")
              .arg(width()).arg(height()).arg(devicePixelRatio()));

    glewExperimental = GL_TRUE;
    const GLenum glewResult = glewInit();
    if (glewResult != GLEW_OK) {
        setStatusMessage(QStringLiteral("glewInit 失败：%1")
            .arg(reinterpret_cast<const char *>(glewGetErrorString(glewResult))));
        return;
    }
    while (glGetError() != GL_NO_ERROR) {}

    if (!g_glfwInitialized) {
        if (glfwInit() == GLFW_FALSE) {
            setStatusMessage(QStringLiteral("glfwInit 失败"));
            return;
        }
        g_glfwInitialized = true;
    }

    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
#if defined(GLFW_NO_API)
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
#endif
    // ★ WS_POPUP（无装饰）：避免 OS 窗口尺寸约束（否则 SetWindowPos 会被限制最大高度）
    glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
    m_hiddenGlfwWindow = glfwCreateWindow(
        qMax(1, width()), qMax(1, height()), "", nullptr, nullptr
    );
    if (!m_hiddenGlfwWindow) {
        setStatusMessage(QStringLiteral("创建隐藏 GLFW 窗口失败"));
        return;
    }

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    if (g_cubismFrameworkRefCount == 0) {
        static Csm::CubismFramework::Option option;
        option.LogFunction = LAppPal::PrintMessage;
        option.LoggingLevel = Csm::CubismFramework::Option::LogLevel_Verbose;
        option.LoadFileFunction = LAppPal::LoadFileAsBytes;
        option.ReleaseBytesFunction = LAppPal::ReleaseBytes;

        Csm::CubismFramework::StartUp(&g_cubismAllocator, &option);
        Csm::CubismFramework::Initialize();
    }
    ++g_cubismFrameworkRefCount;

    m_glReady = true;

    // 如果 GL 上下文被重建（setWindowFlags 等导致），旧模型的 GL 资源已失效，需要重载
    if (m_model) {
        m_model.reset();
    }

    // 背景纹理 ID 在旧上下文中已失效，需要重新上传
    m_backgroundTexture = 0;
    if (!m_backgroundImage.isNull()) {
        m_backgroundTexDirty = true;
    }

    MouseActionManager::GetInstance()->Initialize(width(), height());

    loadPendingModelIfNeeded();
}

void Live2DView::resizeGL(int w, int h)
{
    const qreal dpr = devicePixelRatio();
    const int pxW = qRound(w * dpr);
    const int pxH = qRound(h * dpr);
    glViewport(0, 0, pxW, pxH);

    LOG_DEBUG(QStringLiteral("resizeGL 逻辑=%1x%2 像素=%3x%4 dpr=%5")
              .arg(w).arg(h).arg(pxW).arg(pxH).arg(dpr));

    if (m_hiddenGlfwWindow) {
        glfwSetWindowSize(m_hiddenGlfwWindow, qMax(1, pxW), qMax(1, pxH));
    }
    // （IsBlendModeEnabled=0 已确认，SetRenderTargetSize 是空操作，移除调用）

    // ★ Alpha 快照尺寸不再匹配 → 失效
    invalidateAlphaCapture();

    // 记录 GLFW 窗口实际尺寸（用于排查渲染不一致）
    if (m_hiddenGlfwWindow) {
        int actualGlfwW, actualGlfwH;
        glfwGetWindowSize(m_hiddenGlfwWindow, &actualGlfwW, &actualGlfwH);
        if (actualGlfwW != pxW || actualGlfwH != pxH) {
            LOG_DEBUG(QStringLiteral("⚠ resizeGL: glfwSetWindowSize 后 GLFW=%1x%2 (期望 %3x%4)")
                      .arg(actualGlfwW).arg(actualGlfwH).arg(pxW).arg(pxH));
        }
    }
}

// ============================================================================
// 渲染 — 只画模型，没有诊断覆盖层（模型正常时）
// ============================================================================

void Live2DView::paintGL()
{
    ++m_frameSequence;
    QPainter painter(this);
    painter.beginNativePainting();
    {
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // ── 🏞️ 渲染半透明背景图（OpenGL 纹理）──
        //     截图模式下跳过，只截模型本身
        if (!m_screenshotMode && !m_backgroundImage.isNull() && m_backgroundOpacity > 0.001f) {
            // 延迟上传纹理（在 GL 上下文中）
            if (m_backgroundTexDirty) {
                uploadBackgroundTexture();
            }
            if (m_backgroundTexture != 0) {
                drawBackgroundQuad();
            }
        }

        if (m_model && m_hiddenGlfwWindow) {
            LAppPal::UpdateTime();

            GLint drawFboBefore = 0, readFboBefore = 0;
            glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawFboBefore);
            glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &readFboBefore);

            m_model->ModelOnUpdate(m_hiddenGlfwWindow);

            // ★ 每帧更新头部锚点缓存（供浮动文字实时跟踪，零额外开销）
            //    getInteractionAreas 遍历 drawable 的成本已融入渲染帧，不额外占事件循环
            computeAndCacheHeadAnchor();

            // ★ 模型加载/切换后写一次锚点摘要日志
            if (m_needsAnchorLog)
                writeAnchorSummaryLog();

            GLint drawFboAfter = 0, readFboAfter = 0;
            glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawFboAfter);
            glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &readFboAfter);

            if (drawFboAfter != drawFboBefore)
                glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(drawFboBefore));
            if (readFboAfter != readFboBefore)
                glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(readFboBefore));
        }
    }
    painter.endNativePainting();

    // ── 🔬 Phase 2: Alpha Frame Capture ──
    // 在 paintGL 末尾执行 glReadPixels（此时 context 为 current，FBO 含最终渲染结果）。
    // 不在此处调用 setMask 或 emit 信号同步连接，避免递归触发 repaint。
    if (m_pendingAlphaCapture) {
        captureFramebufferAlpha();
    }

    // 模型正常 → 不画覆盖层；只有加载失败时才显示错误信息
    if (m_modelLoadFailed) {
        drawErrorOverlay(painter);
    }
}

// ============================================================================
// 📸 截图：只截取模型，不包含背景图
//     临时调高 widget 渲染尺寸 → 利用标准渲染管线 → 超级采样抗锯齿
// ============================================================================
QImage Live2DView::grabModelOnly()
{
    m_screenshotMode = true;

    const QSize origSize = size();
    if (origSize.width() <= 0 || origSize.height() <= 0) {
        m_screenshotMode = false;
        return QImage();
    }

    static constexpr int ssFactor = 2;  // 2x 超级采样 → 缩回 1x 时抗锯齿
    const QSize hiSize(origSize.width() * ssFactor, origSize.height() * ssFactor);

    QImage img;

    // 临时调大 widget + 隐藏窗口 → resizeGL 同步更新 viewport 和投影
    // 禁止视觉更新避免闪烁
    setUpdatesEnabled(false);
    resize(hiSize);
    repaint();                    // 同步渲染到高分辨率 FBO
    img = grabFramebuffer();      // 读取高分辨率像素
    resize(origSize);             // 恢复原始尺寸
    setUpdatesEnabled(true);

    m_screenshotMode = false;
    repaint();     // 同步重绘 → computeAndCacheHeadAnchor 以原始尺寸更新缓存

    if (img.isNull()) {
        // 回落：正常尺寸截图
        repaint();
        img = grabFramebuffer();
        update();
        return img;
    }

    // 平滑缩放到 2x 原始大小保存（超级采样 = 2x 渲染 → 1x 采样 + 2x 保存）
    img = img.scaled(origSize.width() * 2, origSize.height() * 2,
                     Qt::KeepAspectRatio, Qt::SmoothTransformation);

    // 清理透明像素 RGB 残留
    if (img.format() == QImage::Format_ARGB32_Premultiplied)
        img = img.convertToFormat(QImage::Format_ARGB32);
    if (img.format() == QImage::Format_ARGB32) {
        for (int y = 0; y < img.height(); ++y) {
            auto *line = reinterpret_cast<QRgb *>(img.scanLine(y));
            for (int x = 0; x < img.width(); ++x)
                if (qAlpha(line[x]) == 0) line[x] = 0;
        }
    }

    return img;
}

void Live2DView::updateEyeTracking()
{
    if (!m_model) return;

    // 全局鼠标位置 → 相对于本控件的局部坐标
    const QPoint local = mapFromGlobal(QCursor::pos());

    // 钳制到控件边界内，再归一化到 [-1, 1]
    const float clampedX = qBound(0.0f, static_cast<float>(local.x()), static_cast<float>(width()));
    const float clampedY = qBound(0.0f, static_cast<float>(local.y()), static_cast<float>(height()));

    float nx = 0.0f, ny = 0.0f;
    if (width() > 0)  nx = (clampedX / static_cast<float>(width()))  * 2.0f - 1.0f;
    if (height() > 0) ny = (clampedY / static_cast<float>(height())) * -2.0f + 1.0f;

    m_model->setEyeTarget(nx, ny);
}

// ============================================================================
// 错误覆盖层（仅模型加载失败时显示）
// ============================================================================

void Live2DView::drawErrorOverlay(QPainter &painter)
{
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), QColor(0, 0, 0, 180));

    painter.setPen(Qt::white);
    QFont font = painter.font();
    font.setPointSize(11);
    painter.setFont(font);
    painter.drawText(QRect(10, 10, width() - 20, 25), Qt::AlignLeft,
                     QStringLiteral("✗ 模型加载失败"));

    font.setPointSize(9);
    painter.setFont(font);
    painter.setPen(QColor(255, 180, 100));
    painter.drawText(QRect(10, 40, width() - 20, height() - 50),
                     Qt::AlignLeft | Qt::AlignTop,
                     QStringLiteral("错误：%1\n\n可能的原因：\n"
                                    "1. 模型目录路径不正确\n"
                                    "2. 缺少 .model3.json 文件\n"
                                    "3. 模型文件损坏或不兼容\n\n"
                                    "详情请查看 live2d_debug_log.txt")
                     .arg(m_statusMessage));
}

// ============================================================================
// 🏞️ 自定义背景 — OpenGL 纹理渲染
// ============================================================================

void Live2DView::uploadBackgroundTexture()
{
    if (m_backgroundTexture) {
        glDeleteTextures(1, &m_backgroundTexture);
        m_backgroundTexture = 0;
    }
    if (m_backgroundImage.isNull()) {
        m_backgroundTexDirty = false;
        return;
    }

    // 转为 RGBA8888 格式以便 OpenGL 使用
    const QImage image = m_backgroundImage.convertToFormat(QImage::Format_RGBA8888);

    glGenTextures(1, &m_backgroundTexture);
    glBindTexture(GL_TEXTURE_2D, m_backgroundTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
                 image.width(), image.height(), 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, image.bits());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    m_backgroundTexDirty = false;
    LOG_DEBUG(QStringLiteral("背景图已上传为 OpenGL 纹理：%1x%2")
              .arg(image.width()).arg(image.height()));
}

void Live2DView::drawBackgroundQuad()
{
    // 计算居中裁剪（cover）纹理坐标
    const double imgW = static_cast<double>(m_backgroundImage.width());
    const double imgH = static_cast<double>(m_backgroundImage.height());
    const double winW = static_cast<double>(width());
    const double winH = static_cast<double>(height());

    const double imgAspect = imgW / imgH;
    const double winAspect = winW / winH;

    float texX1, texY1, texX2, texY2;
    if (imgAspect > winAspect) {
        // 图片更宽 → 水平居中裁剪
        const float crop = static_cast<float>((1.0 - winAspect / imgAspect) * 0.5);
        texX1 = crop;    texY1 = 0.0f;
        texX2 = 1.0f - crop; texY2 = 1.0f;
    } else {
        // 图片更高 → 垂直居中裁剪
        const float crop = static_cast<float>((1.0 - imgAspect / winAspect) * 0.5);
        texX1 = 0.0f;    texY1 = crop;
        texX2 = 1.0f;    texY2 = 1.0f - crop;
    }

    // 保存并设置矩阵状态
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0.0, winW, winH, 0.0, -1.0, 1.0);

    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    // 保存其他状态
    glPushAttrib(GL_ENABLE_BIT | GL_CURRENT_BIT | GL_TEXTURE_BIT | GL_DEPTH_BUFFER_BIT);

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glBindTexture(GL_TEXTURE_2D, m_backgroundTexture);
    glColor4f(1.0f, 1.0f, 1.0f, static_cast<GLfloat>(m_backgroundOpacity));

    glBegin(GL_QUADS);
    glTexCoord2f(texX1, texY1); glVertex2f(0.0f,       0.0f);
    glTexCoord2f(texX2, texY1); glVertex2f(winW, 0.0f);
    glTexCoord2f(texX2, texY2); glVertex2f(winW, winH);
    glTexCoord2f(texX1, texY2); glVertex2f(0.0f,       winH);
    glEnd();

    // 恢复状态
    glPopAttrib();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
}

void Live2DView::setBackgroundImage(const QString &filePath)
{
    if (filePath.isEmpty()) {
        clearBackground();
        return;
    }

    QImage image(filePath);
    if (image.isNull()) {
        LOG_DEBUG(QStringLiteral("无法加载背景图：%1").arg(filePath));
        return;
    }

    m_backgroundImage = image;
    m_backgroundImagePath = filePath;
    m_backgroundTexDirty = true; // 在 paintGL 的 GL 上下文中上传
    update();

    LOG_DEBUG(QStringLiteral("已设置背景图：%1 （尺寸：%2x%3）")
              .arg(filePath).arg(image.width()).arg(image.height()));
}

void Live2DView::clearBackground()
{
    if (m_backgroundTexture) {
        // 必须在有效的 GL 上下文中删除纹理
        makeCurrent();
        glDeleteTextures(1, &m_backgroundTexture);
        m_backgroundTexture = 0;
        doneCurrent();
    }

    m_backgroundImage = QImage();
    m_backgroundImagePath.clear();
    m_backgroundTexDirty = false;
    update();

    LOG_DEBUG(QStringLiteral("已清除背景图"));
}

void Live2DView::setBackgroundOpacity(double opacity)
{
    m_backgroundOpacity = qBound(0.0, opacity, 1.0);
}

void Live2DView::playTapMotion()
{
    if (m_model) {
        const auto handle = m_model->startRandomTapMotion();
    }
}

bool Live2DView::playRandomExpression()
{
    if (m_model) {
        return m_model->startRandomExpression();
    }
    return false;
}

void Live2DView::playMotionGroup(const QString &group, int no)
{
    if (m_model) {
        const std::string g = group.toStdString();
        m_model->startMotionByGroup(g.c_str(), no, LAppDefine::PriorityForce);
    }
}

// ============================================================================
// 鼠标事件 — 转发给 Cubism MouseActionManager
// ============================================================================

// ============================================================================
// 模型命中检测 — 只响应顶点紧贴包围盒（tight bounds），排除画布空白区
// ============================================================================
bool Live2DView::computeModelArea(float &outX0, float &outY0,
                                   float &outW, float &outH) const
{
    if (width() <= 0 || height() <= 0) return false;
    if (m_tightWidth <= 0.0 || m_tightHeight <= 0.0 ||
        m_canvasWidth <= 0.0 || m_canvasHeight <= 0.0) {
        // 模型未加载或无顶点数据 → 回退到全窗口（兜底，实际不会触发点击）
        outX0 = outY0 = 0.0f;
        outW = static_cast<float>(width());
        outH = static_cast<float>(height());
        return true;
    }

    // 1. 计算画布在视口中的实际像素范围（匹配 ModelController::ModelOnUpdate 的投影）
    //    SetWidth(2.0) → Scale(1.0, w/h) → ScaleRelative(comp, comp)
    //    → 画布像素宽 = comp * width（居中）
    //    → 画布像素高 = comp * width * canvasHeight/canvasWidth（居中，不是 height * comp！）
    constexpr float marginRatio = static_cast<float>(PetWindow::kAnimMarginRatio);
    constexpr float enlargeFactor = 1.0f + marginRatio * 2.0f;
    constexpr float comp = 1.0f / enlargeFactor;            // 投影补偿  ≈0.980

    const float canvasW = static_cast<float>(width()) * comp;
    const float canvasX0 = (static_cast<float>(width()) - canvasW) * 0.5f;

    const float canvasAspect = static_cast<float>(m_canvasHeight / m_canvasWidth);
    const float canvasH = canvasW * canvasAspect;
    const float canvasY0 = (static_cast<float>(height()) - canvasH) * 0.5f;

    // 2. 在画布区域内，将顶点紧贴包围盒（tight bounds）从画布坐标映射到像素坐标
    //    Cubism 画布坐标系：原点 (0,0) 在中心，X→右，Y→上 (OpenGL 惯例)
    //    → 先转换到 [0,1] 归一化坐标（从左下角算起）
    const float halfW = static_cast<float>(m_canvasWidth) * 0.5f;
    const float halfH = static_cast<float>(m_canvasHeight) * 0.5f;
    // tight bounds 的归一化左下角 (tNormL, tNormB) 和尺寸 (tNormW, tNormH)
    const float tNormL = static_cast<float>((m_tightMinX + halfW) / m_canvasWidth);
    const float tNormB = static_cast<float>((m_tightMinY + halfH) / m_canvasHeight);
    const float tNormW = static_cast<float>(m_tightWidth / m_canvasWidth);
    const float tNormH = static_cast<float>(m_tightHeight / m_canvasHeight);

    // X 方向：归一化 0~1 映射到像素左~右
    outX0 = canvasX0 + tNormL * canvasW;
    outW  = tNormW * canvasW;
    // Y 方向：Y-up → 像素 Y-down，归一化底部映射到像素底部
    outY0 = canvasY0 + (1.0f - tNormB - tNormH) * canvasH;
    outH  = tNormH * canvasH;
    return true;
}

// ============================================================================
// 🎯 像素 ↔ 画布坐标转换
// ============================================================================

bool Live2DView::pixelToCanvas(float px, float py,
                                float &outCx, float &outCy) const
{
    if (width() <= 0 || height() <= 0) return false;
    if (m_canvasWidth <= 0.0 || m_canvasHeight <= 0.0) return false;

    constexpr float marginRatio = static_cast<float>(PetWindow::kAnimMarginRatio);
    constexpr float enlargeFactor = 1.0f + marginRatio * 2.0f;
    constexpr float comp = 1.0f / enlargeFactor;

    // Compute canvas area in pixel space (same as computeModelArea projection)
    const float canvasW_px = static_cast<float>(width()) * comp;
    const float canvasX0 = (static_cast<float>(width()) - canvasW_px) * 0.5f;
    const float canvasAspect = static_cast<float>(m_canvasHeight / m_canvasWidth);
    const float canvasH_px = canvasW_px * canvasAspect;
    const float canvasY0 = (static_cast<float>(height()) - canvasH_px) * 0.5f;

    // Map: pixel → Cubism canvas (origin at center, X→右, Y→上)
    outCx = (px - canvasX0) / canvasW_px * static_cast<float>(m_canvasWidth)
            - static_cast<float>(m_canvasWidth) * 0.5f;
    outCy = -(py - canvasY0) / canvasH_px * static_cast<float>(m_canvasHeight)
            + static_cast<float>(m_canvasHeight) * 0.5f;
    return true;
}

// ============================================================================
// 🎯 获取模型头部锚点 — 文字浮动定位用
// ============================================================================

bool Live2DView::getHeadAnchorPoint(float &headCenterX, float &headTopY,
                                     float &modelPixelHeight) const
{
    // ── 只读缓存（每帧由 paintGL → computeAndCacheHeadAnchor 更新）──
    headCenterX = m_cachedHeadCenterX;
    headTopY = m_cachedHeadTopY;
    modelPixelHeight = m_cachedModelPixelHeight;
    return true;
}

void Live2DView::computeAndCacheHeadAnchor()
{
    if (width() <= 0 || height() <= 0) return;

    // Collect interaction areas (same as performHitTest)
    QVector<ModelController::HitAreaAABB> headAreas, bodyAreas;
    if (m_model) {
        m_model->getInteractionAreas(headAreas, bodyAreas);
    }

    const bool hasHead = !headAreas.isEmpty();
    const bool hasBody = !bodyAreas.isEmpty();

    // ── Layer 1 & 2: HitArea-based head anchor ──
    if (hasHead || hasBody)
    {
        float anchorMinX = 0.0f, anchorMaxX = 0.0f, anchorMaxY = 0.0f;
        bool anchorValid = false;

        if (hasHead) {
            m_lastAnchorSource = QStringLiteral("Head HitArea");
            anchorMinX = headAreas[0].minX;
            anchorMaxX = headAreas[0].maxX;
            anchorMaxY = headAreas[0].maxY;
            for (int i = 1; i < headAreas.size(); i++) {
                if (headAreas[i].minX < anchorMinX) anchorMinX = headAreas[i].minX;
                if (headAreas[i].maxX > anchorMaxX) anchorMaxX = headAreas[i].maxX;
                if (headAreas[i].maxY > anchorMaxY) anchorMaxY = headAreas[i].maxY;
            }
            anchorValid = true;
        } else if (hasBody) {
            float bodyMinX = bodyAreas[0].minX;
            float bodyMaxX = bodyAreas[0].maxX;
            float bodyMaxY = bodyAreas[0].maxY;
            for (int i = 1; i < bodyAreas.size(); i++) {
                if (bodyAreas[i].minX < bodyMinX) bodyMinX = bodyAreas[i].minX;
                if (bodyAreas[i].maxX > bodyMaxX) bodyMaxX = bodyAreas[i].maxX;
                if (bodyAreas[i].maxY > bodyMaxY) bodyMaxY = bodyAreas[i].maxY;
            }
            anchorMinX = bodyMinX;
            anchorMaxX = bodyMaxX;

            // ── Body-only fallback: scan visible drawables for visual head top ──
            // Body HitArea may not cover the head (e.g. Hiyori's "HitArea" drawable
            // only spans the torso).  Scan visible drawables within the body's
            // horizontal range (expanded 25 % each side) to find the actual
            // topmost visual element.  Oversized full-canvas drawables are
            // excluded so they cannot pull the anchor upward.
            {
                const float bodyW = bodyMaxX - bodyMinX;
                const float xLo = bodyMinX - bodyW * 0.25f;
                const float xHi = bodyMaxX + bodyW * 0.25f;
                float candidateMaxY = bodyMaxY;
                QStringList candidateNames;

                if (auto *rawModel = m_model ? m_model->GetModel() : nullptr) {
                    const int dc = rawModel->GetDrawableCount();
                    for (int d = 0; d < dc; d++) {
                        if (!rawModel->GetDrawableDynamicFlagIsVisible(d)) continue;
                        if (rawModel->GetDrawableOpacity(d) < 0.01f) continue;

                        const auto *verts = rawModel->GetDrawableVertexPositions(d);
                        const int vc = rawModel->GetDrawableVertexCount(d);
                        if (vc < 3) continue;

                        float dMinX = verts[0].X, dMaxX = verts[0].X;
                        float dMinY = verts[0].Y, dMaxY = verts[0].Y;
                        for (int v = 1; v < vc; v++) {
                            if (verts[v].X < dMinX) dMinX = verts[v].X;
                            if (verts[v].X > dMaxX) dMaxX = verts[v].X;
                            if (verts[v].Y < dMinY) dMinY = verts[v].Y;
                            if (verts[v].Y > dMaxY) dMaxY = verts[v].Y;
                        }

                        // Gate: must overlap with expanded body X range
                        if (dMaxX < xLo || dMinX > xHi) continue;

                        // Exclude oversized full-canvas drawables
                        const float dW = dMaxX - dMinX;
                        const float dH = dMaxY - dMinY;
                        if (dW > m_canvasWidth * 0.8 && dH > m_canvasHeight * 0.8)
                            continue;

                        if (dMaxY > candidateMaxY) {
                            candidateMaxY = dMaxY;
                            const auto *id = rawModel->GetDrawableId(d);
                            if (id)
                                candidateNames << QString::fromUtf8(
                                                   id->GetString().GetRawString());
                        }
                    }
                }

                if (candidateMaxY > bodyMaxY) {
                    anchorMaxY = candidateMaxY;
                    m_lastAnchorSource = QStringLiteral("BodyVisualTop");
                } else {
                    anchorMaxY = bodyMaxY;
                    m_lastAnchorSource = QStringLiteral("BodyFallback");
                }
            }
            anchorValid = true;
        }

        if (anchorValid && m_canvasWidth > 0.0 && m_canvasHeight > 0.0)
        {
            const float anchorCx = (anchorMinX + anchorMaxX) * 0.5f;

            constexpr float marginRatio = static_cast<float>(PetWindow::kAnimMarginRatio);
            constexpr float enlargeFactor = 1.0f + marginRatio * 2.0f;
            constexpr float comp = 1.0f / enlargeFactor;

            const float canvasW_px = static_cast<float>(width()) * comp;
            const float canvasX0 = (static_cast<float>(width()) - canvasW_px) * 0.5f;
            const float canvasAspect = static_cast<float>(m_canvasHeight / m_canvasWidth);
            const float canvasH_px = canvasW_px * canvasAspect;
            const float canvasY0 = (static_cast<float>(height()) - canvasH_px) * 0.5f;

            m_cachedHeadCenterX = canvasX0
                + (anchorCx + static_cast<float>(m_canvasWidth) * 0.5f)
                  / static_cast<float>(m_canvasWidth) * canvasW_px;

            m_cachedHeadTopY = canvasY0
                + (static_cast<float>(m_canvasHeight) * 0.5f - anchorMaxY)
                  / static_cast<float>(m_canvasHeight) * canvasH_px;

            float mx = 0.0f, my = 0.0f, mw = 0.0f, mh = 0.0f;
            if (computeModelArea(mx, my, mw, mh) && mh > 0.0f) {
                m_cachedModelPixelHeight = mh;
            } else {
                m_cachedModelPixelHeight = canvasH_px;
            }

            return;
        }
    }

    // ── Layer 3: Tight bounds fallback ──
    float mx = 0.0f, my = 0.0f, mw = 0.0f, mh = 0.0f;
    if (computeModelArea(mx, my, mw, mh) && mh > 0.0f) {
        m_lastAnchorSource = QStringLiteral("Tight Bounds");
        m_cachedHeadCenterX = mx + mw * 0.5f;
        m_cachedHeadTopY = my;
        m_cachedModelPixelHeight = mh;
        return;
    }

    // ── Ultimate fallback: window center top ──
    m_lastAnchorSource = QStringLiteral("Window Top Fallback");
    m_cachedHeadCenterX = static_cast<float>(width()) * 0.5f;
    m_cachedHeadTopY = 0.0f;
    m_cachedModelPixelHeight = static_cast<float>(height());
}

// ============================================================================
// 摘要日志：模型加载/切换时写一次锚点信息
// ============================================================================

void Live2DView::writeAnchorSummaryLog()
{
    if (!m_needsAnchorLog) return;
    m_needsAnchorLog = false;

    // 模型名：取目录末尾
    QString modelName = m_modelDirectory;
    {
        int idx = modelName.lastIndexOf(QLatin1Char('/'), -2);
        if (idx >= 0) modelName = modelName.mid(idx + 1);
        if (modelName.endsWith(QLatin1Char('/'))) modelName.chop(1);
    }

    QFile f(QStringLiteral("live2d_debug_log.txt"));
    if (f.open(QIODevice::Append | QIODevice::Text)) {
        QTextStream s(&f);
        s << QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz"))
          << " [anchor] model=" << modelName
          << " source=" << m_lastAnchorSource
          << " headTopY=" << QString::number(m_cachedHeadTopY, 'f', 1)
          << "\n";
    }
}

// ============================================================================
// 🎯 命中检测：HitAreas → drawable 匹配 → 高度比例 fallback
// ============================================================================

bool Live2DView::performHitTest(float px, float py, bool &outIsHead) const
{
    // Collect HitAreas / drawable-matched areas
    QVector<ModelController::HitAreaAABB> headAreas, bodyAreas;
    if (m_model) {
        m_model->getInteractionAreas(headAreas, bodyAreas);
    }

    const bool hasHead = !headAreas.isEmpty();
    const bool hasBody = !bodyAreas.isEmpty();

    // ── Layer 1: HitArea AABB 检测（精确，跟随动画变形） ──
    if (hasHead || hasBody)
    {
        float cx = 0.0f, cy = 0.0f;
        const bool canvasOk = pixelToCanvas(px, py, cx, cy);

        if (canvasOk)
        {
            // 1a. Head AABB 检测（最高优先级）
            for (const auto &area : headAreas)
            {
                if (cx >= area.minX && cx <= area.maxX &&
                    cy >= area.minY && cy <= area.maxY)
                {
                    outIsHead = true;
                    return true;
                }
            }

            // 1b. Body AABB 检测 — 仅当 Head 也明确存在时做标准 Body 匹配
            //     避免只有 Body HitArea 时身体区域吞掉头部（如 Hiyori/Rice/Wanko）
            if (hasHead && hasBody)
            {
                for (const auto &area : bodyAreas)
                {
                    if (cx >= area.minX && cx <= area.maxX &&
                        cy >= area.minY && cy <= area.maxY)
                    {
                        outIsHead = false;
                        return true;
                    }
                }
            }
        }

        // ── Layer 2: Body-only 模型 → Body AABB 内 35%/65% 高度分割 ──
        // 模型只有 Body HitArea（无 Head），但 Body 碰撞盒通常覆盖全身，
        // 取顶部 35% 为头、下部 65% 为身，确保头部点击不会误判为身体
        //
        // 注意：即使 canvasOk=false 也尝试检测（body AABB 不依赖画布坐标）
        if (!hasHead && hasBody && canvasOk)
        {
            for (const auto &area : bodyAreas)
            {
                if (cx >= area.minX && cx <= area.maxX &&
                    cy >= area.minY && cy <= area.maxY)
                {
                    // Cubism 画布 Y 向上：maxY = 顶部，minY = 底部
                    const float bodyHeight = area.maxY - area.minY;
                    const float headThreshold = area.maxY - bodyHeight * 0.35f;
                    outIsHead = (cy > headThreshold);
                    return true;
                }
            }
        }

        // 有 HitArea 但点在所有 AABB 之外 → 不拦截，继续进 Layer 3 兜底
    }

    // ── Layer 3: 兜底 — tight bounds 35%/65% 高度分割（始终可达） ──
    // 覆盖：无 HitArea 的模型 (xiaoyue)、有 HitArea 但点在区域外的空隙
    float mx, my, mw, mh;
    if (computeModelArea(mx, my, mw, mh))
    {
        if (px >= mx && px < mx + mw && py >= my && py < my + mh)
        {
            outIsHead = (py - my < mh * 0.35f);  // 前 35% = 头部
            return true;
        }
    }

    outIsHead = false;
    return false;
}

// ============================================================================
// 🔍 Alpha 检测：读取点击位置像素透明度，过滤空白点击
// ============================================================================

bool Live2DView::checkPixelAlpha(float px, float py, unsigned char threshold)
{
    if (width() <= 0 || height() <= 0) return true;  // can't check → assume valid

    const qreal dpr = devicePixelRatio();
    const int wPx = qRound(static_cast<float>(width()) * static_cast<float>(dpr));
    const int hPx = qRound(static_cast<float>(height()) * static_cast<float>(dpr));

    GLint pixelX = qRound(px * static_cast<float>(dpr));
    GLint pixelY = hPx - qRound(py * static_cast<float>(dpr));  // flip Y for OpenGL
    pixelX = qBound(0, pixelX, wPx - 1);
    pixelY = qBound(0, pixelY, hPx - 1);

    // Temporarily make GL context current to read pixel
    makeCurrent();
    GLubyte pixel[4] = {0};
    glReadPixels(pixelX, pixelY, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    doneCurrent();

    return pixel[3] >= threshold;
}

bool Live2DView::isPointInModelArea(const QPoint &localPos) const
{
    float mx, my, mw, mh;
    if (!computeModelArea(mx, my, mw, mh)) return false;
    const float px = static_cast<float>(localPos.x());
    const float py = static_cast<float>(localPos.y());
    return (px >= mx && px < mx + mw && py >= my && py < my + mh);
}

bool Live2DView::hitTestAt(float px, float py, bool &outIsHead) const
{
    return performHitTest(px, py, outIsHead);
}

bool Live2DView::alphaTestAt(float px, float py, unsigned char threshold)
{
    return checkPixelAlpha(px, py, threshold);
}

bool Live2DView::getModelPixelRect(float &outX0, float &outY0,
                                    float &outW, float &outH) const
{
    return computeModelArea(outX0, outY0, outW, outH);
}

// ============================================================================
// 📦 获取所有可见 Drawable 的紧贴包围盒 — 用于构建 setMask 的 QRegion
// ============================================================================

void Live2DView::getVisibleDrawableRects(QVector<QRect> &outRects) const
{
    outRects.clear();
    if (!m_model || !m_model->GetModel()) return;
    if (width() <= 0 || height() <= 0) return;
    if (m_canvasWidth <= 0.0 || m_canvasHeight <= 0.0) return;

    // 投影链匹配 ModelController::ModelOnUpdate：
    //   SetWidth(2.0) → Scale(1.0, w/h) → ScaleRelative(comp, comp)
    constexpr float marginRatio = static_cast<float>(PetWindow::kAnimMarginRatio);
    constexpr float enlargeFactor = 1.0f + marginRatio * 2.0f;
    constexpr float comp = 1.0f / enlargeFactor;

    const float canvasW_px = static_cast<float>(width()) * comp;
    const float canvasX0 = (static_cast<float>(width()) - canvasW_px) * 0.5f;
    const float canvasAspect = static_cast<float>(m_canvasHeight / m_canvasWidth);
    const float canvasH_px = canvasW_px * canvasAspect;
    const float canvasY0 = (static_cast<float>(height()) - canvasH_px) * 0.5f;

    const float halfW = static_cast<float>(m_canvasWidth) * 0.5f;
    const float halfH = static_cast<float>(m_canvasHeight) * 0.5f;

    auto *model = m_model->GetModel();
    const int drawableCount = model->GetDrawableCount();

    for (int i = 0; i < drawableCount; ++i)
    {
        // 跳过不可见的 Drawable
        if (!model->GetDrawableDynamicFlagIsVisible(i)) continue;

        // 跳过透明度 >50% 的 Drawable（半透明 → 仍然有视觉存在，保留）
        // 跳过透明度近乎 0 的 Drawable（完全透明 → 排除）
        if (model->GetDrawableOpacity(i) < 0.01f) continue;

        const auto *vertices = model->GetDrawableVertexPositions(i);
        const int vertexCount = model->GetDrawableVertexCount(i);
        if (vertexCount < 3) continue;

        // 遍历顶点求紧贴包围盒（画布坐标系 Y-up），使用共享纯函数处理 NaN/Inf
        float dMinX = FLT_MAX, dMaxX = -FLT_MAX;
        float dMinY = FLT_MAX, dMaxY = -FLT_MAX;
        int validVerts = 0;
        for (int j = 0; j < vertexCount; ++j) {
            DrawableBounds::AccumulateVertex(
                vertices[j].X, vertices[j].Y,
                dMinX, dMinY, dMaxX, dMaxY, validVerts);
        }
        if (!DrawableBounds::FinalizeDrawable(dMinX, dMinY, dMaxX, dMaxY, validVerts))
            continue;

        const float drawW = dMaxX - dMinX;
        const float drawH = dMaxY - dMinY;

        // 画布坐标 → 像素坐标（与 computeModelArea 的投影链一致）
        // 归一化 → 映射到画布像素区域
        const float tNormL = (dMinX + halfW) / static_cast<float>(m_canvasWidth);
        const float tNormB = (dMinY + halfH) / static_cast<float>(m_canvasHeight);
        const float tNormW = drawW / static_cast<float>(m_canvasWidth);
        const float tNormH = drawH / static_cast<float>(m_canvasHeight);

        float px = canvasX0 + tNormL * canvasW_px;
        float py = canvasY0 + (1.0f - tNormB - tNormH) * canvasH_px;
        float pw = tNormW * canvasW_px;
        float ph = tNormH * canvasH_px;

        // 约束到控件边界
        QRect dr(qRound(px), qRound(py), qRound(pw), qRound(ph));
        dr = dr.intersected(QRect(0, 0, width(), height()));
        if (dr.width() > 0 && dr.height() > 0) {
            outRects.append(dr);
        }
    }
}

// ============================================================================
// 📐 精确输入 Region 构建 — 基于三角形网格的 point-in-triangle 检测
// ============================================================================

QRegion Live2DView::buildPreciseInputRegion(int cellSizePx) const
{
    if (!m_model || !m_model->GetModel()) {
        return QRegion();
    }
    if (width() <= 0 || height() <= 0) {
        return QRegion();
    }
    if (m_canvasWidth <= 0.0 || m_canvasHeight <= 0.0) {
        return QRegion();
    }

    // ── 投影参数（与 getVisibleDrawableRects 一致） ──
    constexpr float marginRatio = static_cast<float>(PetWindow::kAnimMarginRatio);
    constexpr float enlargeFactor = 1.0f + marginRatio * 2.0f;
    constexpr float comp = 1.0f / enlargeFactor;

    const float canvasW_px = static_cast<float>(width()) * comp;
    const float canvasX0 = (static_cast<float>(width()) - canvasW_px) * 0.5f;
    const float canvasAspect = static_cast<float>(m_canvasHeight / m_canvasWidth);
    const float canvasH_px = canvasW_px * canvasAspect;
    const float canvasY0 = (static_cast<float>(height()) - canvasH_px) * 0.5f;
    const float halfW = static_cast<float>(m_canvasWidth) * 0.5f;
    const float halfH = static_cast<float>(m_canvasHeight) * 0.5f;

    auto *model = m_model->GetModel();
    const int drawableCount = model->GetDrawableCount();

    // 收集命中的 grid cell
    QVector<QPair<int,int>> hitCells;

    for (int d = 0; d < drawableCount; ++d) {
        // 跳过不可见 Drawable
        if (!model->GetDrawableDynamicFlagIsVisible(d))
            continue;
        // 跳过完全透明的 Drawable
        if (model->GetDrawableOpacity(d) < 0.01f)
            continue;

        const auto *verts = model->GetDrawableVertexPositions(d);
        const int vc = model->GetDrawableVertexCount(d);
        if (vc < 3)
            continue;

        const auto *indices = model->GetDrawableVertexIndices(d);
        const int ic = model->GetDrawableVertexIndexCount(d);
        const int triCount = (ic > 0) ? (ic / 3) : (vc / 3);
        if (triCount < 1)
            continue;

        // ── 投影全部顶点到像素坐标 ──
        // 使用栈分配的小缓冲区（常见 Live2D drawable 顶点数 < 512）
        // 避免通用 QVector 分配开销
        constexpr int kStackVerts = 256;
        QPointF stackBuf[kStackVerts];
        QVector<QPointF> heapBuf;
        QPointF *pixelVerts = stackBuf;

        if (vc > kStackVerts) {
            heapBuf.resize(vc);
            pixelVerts = heapBuf.data();
        }

        for (int v = 0; v < vc; ++v) {
            const float tX = (verts[v].X + halfW) / static_cast<float>(m_canvasWidth);
            const float tY = (verts[v].Y + halfH) / static_cast<float>(m_canvasHeight);
            pixelVerts[v] = QPointF(
                canvasX0 + tX * canvasW_px,
                canvasY0 + (1.0f - tY) * canvasH_px
            );
        }

        // ── 遍历每个三角形 ──
        for (int t = 0; t < triCount; ++t) {
            int i0, i1, i2;
            if (ic > 0) {
                i0 = indices[t * 3 + 0];
                i1 = indices[t * 3 + 1];
                i2 = indices[t * 3 + 2];
            } else {
                i0 = t * 3 + 0;
                i1 = t * 3 + 1;
                i2 = t * 3 + 2;
            }

            if (i0 >= vc || i1 >= vc || i2 >= vc)
                continue;

            const QPointF &p0 = pixelVerts[i0];
            const QPointF &p1 = pixelVerts[i1];
            const QPointF &p2 = pixelVerts[i2];

            // ── Broad phase：三角形 AABB（像素空间） ──
            float tMinX = std::min({p0.x(), p1.x(), p2.x()});
            float tMinY = std::min({p0.y(), p1.y(), p2.y()});
            float tMaxX = std::max({p0.x(), p1.x(), p2.x()});
            float tMaxY = std::max({p0.y(), p1.y(), p2.y()});

            // 钳制到窗口边界（像素空间）
            tMinX = std::max(0.0f, tMinX);
            tMinY = std::max(0.0f, tMinY);
            tMaxX = std::min(static_cast<float>(width() - 1), tMaxX);
            tMaxY = std::min(static_cast<float>(height() - 1), tMaxY);

            if (tMaxX <= tMinX || tMaxY <= tMinY)
                continue;

            // 退化三角形（像素空间面积接近零）
            const float area = std::abs(
                (p1.x() - p0.x()) * (p2.y() - p0.y()) -
                (p2.x() - p0.x()) * (p1.y() - p0.y()));
            if (area < 0.5f)
                continue;

            // ── 在三角形 AABB 内遍历 grid cell ──
            const int colStart = static_cast<int>(tMinX) / cellSizePx;
            const int colEnd = (static_cast<int>(tMaxX) + cellSizePx - 1) / cellSizePx;
            const int rowStart = static_cast<int>(tMinY) / cellSizePx;
            const int rowEnd = (static_cast<int>(tMaxY) + cellSizePx - 1) / cellSizePx;

            for (int r = rowStart; r < rowEnd; ++r) {
                const float cy = static_cast<float>(r * cellSizePx + cellSizePx / 2);
                // 基于 AABB 的快速提前剔除
                if (cy < tMinY || cy > tMaxY)
                    continue;

                for (int c = colStart; c < colEnd; ++c) {
                    const float cx = static_cast<float>(c * cellSizePx + cellSizePx / 2);

                    if (cx < tMinX || cx > tMaxX)
                        continue;

                    // ── Point-in-triangle 精确检测：先测中心点 ──
                    bool cellHit = InputRegionBuilder::pointInTriangle(
                        cx, cy,
                        p0.x(), p0.y(),
                        p1.x(), p1.y(),
                        p2.x(), p2.y());

                    // ── 中心未命中 → 测试 cell 四个角点 ──
                    // 当三角形窄于 cellSizePx 时，cell 中心可能在三角形外，
                    // 但角点落在三角形内。测试四个角点确保薄三角形不被漏检。
                    if (!cellHit) {
                        const float x0 = static_cast<float>(c * cellSizePx);
                        const float y0 = static_cast<float>(r * cellSizePx);
                        const float x1 = static_cast<float>((c + 1) * cellSizePx);
                        const float y1 = static_cast<float>((r + 1) * cellSizePx);

                        cellHit =
                            InputRegionBuilder::pointInTriangle(
                                x0, y0, p0.x(), p0.y(), p1.x(), p1.y(), p2.x(), p2.y()) ||
                            InputRegionBuilder::pointInTriangle(
                                x1, y0, p0.x(), p0.y(), p1.x(), p1.y(), p2.x(), p2.y()) ||
                            InputRegionBuilder::pointInTriangle(
                                x0, y1, p0.x(), p0.y(), p1.x(), p1.y(), p2.x(), p2.y()) ||
                            InputRegionBuilder::pointInTriangle(
                                x1, y1, p0.x(), p0.y(), p1.x(), p1.y(), p2.x(), p2.y());
                    }

                    if (cellHit) {
                        hitCells.append(qMakePair(c, r));
                    }
                }
            }
        }

    }

    if (hitCells.isEmpty()) {
        return QRegion();
    }

    const QRegion result = InputRegionBuilder::mergeCellRects(hitCells, cellSizePx, width(), height());
    return result;
}

// ============================================================================
// 🔬 Phase 2: Alpha Region — 整帧 Alpha 捕获与 Region 构建
// ============================================================================

void Live2DView::invalidateAlphaCapture()
{
    m_alphaCaptureValid = false;
    m_alphaBuffer.clear();
    // ★ 不重置 m_pendingAlphaCapture：
    //   resizeGL 等场景中即使尺寸变化，捕获请求仍在当前 generation 有效，
    //   下一次 paintGL 应以新尺寸执行捕获。
    //   若清除此标志，则 paintGL → captureFramebufferAlpha 立即返回，
    //   导致 Alpha 捕获被跳过 → 无 alphaCaptureReady 信号 → 等待 3s → watchdog fallback。
    // m_pendingAlphaCapture = false;
}

void Live2DView::requestAlphaCapture(uint64_t generation)
{
    // 同 generation 已存在 pending 请求时不重复排队（避免无限 update/paintGL 循环）
    if (m_pendingAlphaCapture && m_alphaCaptureRequestGen == generation)
        return;

    m_alphaCaptureRequestGen = generation;
    m_alphaCaptureRetryCount = 0;  // 新 generation 重置重试计数
    if (!m_model || !m_glReady) {
        // GL 尚未就绪（show() 前调用）→ 保持 pending 标记，
        // 使 initializeGL → 首次 paintGL 时执行捕获。
        // ★ 不重置 m_pendingAlphaCapture，否则请求被静默丢弃，
        //   无 alphaCaptureReady → watchdog 3s 超时 → geometric-only fallback。
        m_pendingAlphaCapture = true;
        return;
    }
    if (!m_pendingAlphaCapture) {
        m_pendingAlphaCapture = true;
        update();  // 请求下一次 paintGL
    }
}

void Live2DView::captureFramebufferAlpha()
{
    if (!m_pendingAlphaCapture)
        return;
    m_pendingAlphaCapture = false;

    const int w = width();
    const int h = height();
    if (w <= 0 || h <= 0)
        return;

    const qreal dpr = devicePixelRatio();
    const int fbW = qRound(w * dpr);
    const int fbH = qRound(h * dpr);
    if (fbW <= 0 || fbH <= 0)
        return;

    // ── glReadPixels 整帧 RGBA ──

    // 此时 context 应为 current（paintGL 调用链）
    // 分配临时 RGBA 缓冲区用于 glReadPixels
    const int pixelCount = fbW * fbH;
    QVector<unsigned char> rgba(pixelCount * 4);

    // 确保 READ_FRAMEBUFFER 绑定正确
    GLint prevReadFbo = 0;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevReadFbo);

    glReadPixels(0, 0, fbW, fbH, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());

    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        // ── glReadPixels 失败 → 有限重试 ──
        // 不发出 alphaCaptureReady，保留 pending 使 paintGL 重试。
        // 超过最大重试次数后停止，让 watchdog 兜底 geometric-only fallback。
        m_alphaCaptureValid = false;
        m_alphaBuffer.clear();

        m_alphaCaptureRetryCount++;
        if (m_alphaCaptureRetryCount < kAlphaCaptureMaxRetries) {
            // 保留 pending 使下一帧 paintGL 重试
            m_pendingAlphaCapture = true;
            // 不发出 alphaCaptureReady（无有效数据，让 timer/watchdog 继续等待）
        }
        // 超过重试上限：m_pendingAlphaCapture 保持 false（已在函数入口清除），
        // 无 alphaCaptureReady 信号发出，timer 会在下一次到期时走 capture-failed 路径。
        return;
    }

    // ── 提取 Alpha 通道 ──
    m_alphaBuffer.resize(pixelCount);
    for (int i = 0; i < pixelCount; ++i)
        m_alphaBuffer[i] = rgba[i * 4 + 3];

    // ── 记录捕获元数据 ──
    m_alphaCaptureGen = m_alphaCaptureRequestGen;  // 标记快照所属 generation
    m_alphaCaptureW  = fbW;
    m_alphaCaptureH  = fbH;
    m_alphaCaptureWidgetW = w;
    m_alphaCaptureWidgetH = h;
    m_alphaCaptureDPR = dpr;
    m_alphaCaptureFrameSeq = m_frameSequence;
    m_alphaCaptureValid = true;
    m_alphaCaptureRetryCount = 0;  // 捕获成功 → 重试计数清零

    // ── 通过 QueuedConnection 通知 InputProxyWindow 构建 Alpha Region ──
    // paintGL 内不调用 setMask，不 emit 同步信号，防止递归触发 repaint
    QMetaObject::invokeMethod(this, "alphaCaptureReady", Qt::QueuedConnection);
}

QRegion Live2DView::buildAlphaRegion(int cellSizePx) const
{
    if (!m_alphaCaptureValid || m_alphaBuffer.isEmpty())
        return QRegion();

    const int w = width();
    const int h = height();
    if (w <= 0 || h <= 0)
        return QRegion();

    const qreal dpr = devicePixelRatio();

    // ── 一致性检查：快照必须匹配当前尺寸 ──
    if (m_alphaCaptureWidgetW != w || m_alphaCaptureWidgetH != h ||
        qAbs(m_alphaCaptureDPR - dpr) > 0.001 ||
        m_alphaCaptureW != qRound(w * dpr) ||
        m_alphaCaptureH != qRound(h * dpr))
    {
        return QRegion();
    }

    const int fbW = m_alphaCaptureW;
    const int fbH = m_alphaCaptureH;

    // ════════════════════════════════════════════════════════════════
    //  Phase 1: framebuffer Alpha 像素扫描 + cell 可见性生成
    // ════════════════════════════════════════════════════════════════

    const int cols = (w + cellSizePx - 1) / cellSizePx;
    const int rows = (h + cellSizePx - 1) / cellSizePx;

    QVector<QPair<int,int>> alphaCells;

    for (int r = 0; r < rows; ++r) {
        const int cellT = r * cellSizePx;
        const int cellB = qMin((r + 1) * cellSizePx, h);

        for (int c = 0; c < cols; ++c) {
            const int cellL = c * cellSizePx;
            const int cellR = qMin((c + 1) * cellSizePx, w);

            // cell → framebuffer 物理像素范围
            const int fbL = qMax(0, qRound(cellL * dpr));
            const int fbT = qMax(0, qRound(cellT * dpr));
            const int fbR = qMin(fbW, qRound(cellR * dpr));
            const int fbB = qMin(fbH, qRound(cellB * dpr));

            if (fbR <= fbL || fbB <= fbT)
                continue;

            bool hasAlpha = false;

            // 扫描 cell 内所有物理像素
            for (int py = fbT; py < fbB; ++py) {
                const int bufRow = fbH - 1 - py;
                const int rowOffset = bufRow * fbW;

                for (int px = fbL; px < fbR; ++px) {
                    if (px < 0 || px >= fbW)
                        continue;

                    if (m_alphaBuffer[rowOffset + px] >= 30) {
                        hasAlpha = true;
                        break;
                    }
                }
                if (hasAlpha)
                    break;
            }

            if (hasAlpha)
                alphaCells.append(qMakePair(c, r));
        }
    }

    if (alphaCells.isEmpty())
        return QRegion();

    std::sort(alphaCells.begin(), alphaCells.end());
    alphaCells.erase(std::unique(alphaCells.begin(), alphaCells.end()), alphaCells.end());

    QRegion result;
    const int rowCount = alphaCells.size();
    int idx = 0;

    while (idx < rowCount) {
        const int row = alphaCells[idx].second;

        int spanStart = alphaCells[idx].first;
        int spanEnd = spanStart + 1;
        ++idx;

        // 同一行中合并连续 col
        while (idx < rowCount && alphaCells[idx].second == row) {
            if (alphaCells[idx].first == spanEnd) {
                spanEnd = alphaCells[idx].first + 1;
                ++idx;
            } else {
                break;
            }
        }

        // 收集矩形，等 Phase 4 统一构造
        int rx = spanStart * cellSizePx;
        int rw = (spanEnd - spanStart) * cellSizePx;
        int ry = row * cellSizePx;
        int rh = cellSizePx;

        if (rx < 0) { rw += rx; rx = 0; }
        if (ry < 0) { rh += ry; ry = 0; }
        if (rx < w && ry < h) {
            rw = std::min(rw, w - rx);
            rh = std::min(rh, h - ry);
            if (rw > 0 && rh > 0)
                result |= QRegion(rx, ry, rw, rh);
        }
    }

    return result;
}

bool Live2DView::isAlphaCaptureValid() const
{
    if (!m_alphaCaptureValid || m_alphaBuffer.isEmpty())
        return false;

    const int w = width();
    const int h = height();
    if (w <= 0 || h <= 0)
        return false;

    const qreal dpr = devicePixelRatio();
    return m_alphaCaptureWidgetW == w && m_alphaCaptureWidgetH == h &&
           qAbs(m_alphaCaptureDPR - dpr) < 0.001 &&
           m_alphaCaptureW == qRound(w * dpr) &&
           m_alphaCaptureH == qRound(h * dpr);
}


// ============================================================================
// 🗣️ TTS 嘴型同步 — 转发说话状态到 ModelController
// ============================================================================

void Live2DView::onSpeakingStarted()
{
    m_isSpeaking = true;
    if (m_model) {
        m_model->setSpeaking(true);
    }
}

void Live2DView::onSpeakingFinished()
{
    m_isSpeaking = false;
    if (m_model) {
        m_model->setSpeaking(false);
    }
}

void Live2DView::onSpeakingInterrupted()
{
    m_isSpeaking = false;
    if (m_model) {
        m_model->setSpeaking(false);
    }
}

// ============================================================================
// 鼠标事件 — 转发给 Cubism MouseActionManager
// ============================================================================

void Live2DView::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && m_model) {
        m_mouseCaptured = true;
        MouseActionManager::GetInstance()->OnTouchesBegan(
            static_cast<float>(event->position().x()),
            static_cast<float>(event->position().y()));

        const float px = static_cast<float>(event->position().x());
        const float py = static_cast<float>(event->position().y());

        // ── 新的多级命中检测系统 ──
        // 1. HitAreas from model3.json (精确区域)
        // 2. Drawable 名称匹配 (常见命名规则)
        // 3. Fallback: tight bounds 高度 35%/65% 分割
        bool isHead = false;
        if (performHitTest(px, py, isHead))
        {
            // ── Alpha 检测：过滤透明像素内的点击（如头发间隙） ──
            if (checkPixelAlpha(px, py)) {
                m_pressInHeadArea = isHead;
                m_pressInBodyArea = !isHead;
            } else {
                m_pressInHeadArea = false;
                m_pressInBodyArea = false;
            }
        }
        else
        {
            m_pressInHeadArea = false;
            m_pressInBodyArea = false;
        }

        m_pressStartPos = event->pos();
    }
    event->ignore(); // 冒泡给 PetWindow（拖拽）
}

void Live2DView::mouseMoveEvent(QMouseEvent *event)
{
    // 转发给 MouseActionManager（保留点击测试等能力）
    // 眼球追踪已由 updateEyeTracking() 通过全局光标位置驱动
    if (m_model) {
        MouseActionManager::GetInstance()->OnTouchesMoved(
            static_cast<float>(event->position().x()),
            static_cast<float>(event->position().y()));
    }

    // 按下状态下移动超过容差 → 判定为拖拽，取消点击资格
    if (m_pressInHeadArea || m_pressInBodyArea) {
        const int dist = (event->pos() - m_pressStartPos).manhattanLength();
        if (dist > 6) {
            m_pressInHeadArea = false;
            m_pressInBodyArea = false;
        }
    }

    event->ignore();
}

void Live2DView::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && m_mouseCaptured && m_model) {
        m_mouseCaptured = false;
        MouseActionManager::GetInstance()->OnTouchesEnded(
            static_cast<float>(event->position().x()),
            static_cast<float>(event->position().y()));

        // 松手时判定点击区域
        if (m_pressInHeadArea) {
            m_pressInHeadArea = false;
            qInfo() << "[Live2DView] headClicked";
            emit headClicked();
        } else if (m_pressInBodyArea) {
            m_pressInBodyArea = false;
            qInfo() << "[Live2DView] bodyClicked";
            emit bodyClicked();
        }
    }

    m_pressInHeadArea = false; // 安全兜底
    m_pressInBodyArea = false;
    event->ignore();
}
