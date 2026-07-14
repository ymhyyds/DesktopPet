#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include <cfloat>

#include <Utils/CubismString.hpp>
#include <Motion/CubismMotion.hpp>
#include <Physics/CubismPhysics.hpp>
#include <CubismDefaultParameterId.hpp>
#include <Rendering/OpenGL/CubismRenderer_OpenGLES2.hpp>
#include <Rendering/OpenGL/CubismOffscreenManager_OpenGLES2.hpp>
#include <Motion/CubismMotionQueueEntry.hpp>
#include <Motion/CubismMotionJson.hpp>
#include <Id/CubismIdManager.hpp>

#include "LAppPal.hpp"
#include "LAppDefine.hpp"
#include "MouseActionManager.hpp"

#include "ModelController.hpp"
#include "../interaction/DrawableBounds.h"

#include "Motion/CubismBreathUpdater.hpp"
#include "Motion/CubismLookUpdater.hpp"
#include "Motion/CubismExpressionUpdater.hpp"
#include "Motion/CubismEyeBlinkUpdater.hpp"
#include "Motion/CubismPhysicsUpdater.hpp"
#include "Motion/CubismPoseUpdater.hpp"

#include "PetWindow.h"

#include <QRandomGenerator>
#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <QFile>
#include <QElapsedTimer>
#include <QDateTime>
#include <QCoreApplication>
#include <QDebug>

using namespace Live2D::Cubism::Framework;
using namespace Live2D::Cubism::Framework::DefaultParameterId;
using namespace LAppDefine;

// ====================================================================
// 构造 / 析构
// ====================================================================

ModelController::ModelController(const std::string modelDirectoryName, const std::string currentModelDirectory)
    : LAppModel_Common()
    , _modelJson(nullptr)
    , _userTimeSeconds(0.0f)
    , _modelDirName(modelDirectoryName)
    , _currentModelDirectory(currentModelDirectory)
    , _textureManager(new LAppTextureManager())
    , _motionUpdated(false)
{
    auto* idMgr = CubismFramework::GetIdManager();

    // 标准参数 ID
    _idParamAngleX     = idMgr->GetId(ParamAngleX);
    _idParamAngleY     = idMgr->GetId(ParamAngleY);
    _idParamAngleZ     = idMgr->GetId(ParamAngleZ);
    _idParamBodyAngleX = idMgr->GetId(ParamBodyAngleX);
    _idParamEyeBallX   = idMgr->GetId(ParamEyeBallX);
    _idParamEyeBallY   = idMgr->GetId(ParamEyeBallY);

    // 表情参数 ID
    _idParamMouthOpenY = idMgr->GetId(ParamMouthOpenY);
    _idParamCheek      = idMgr->GetId(ParamCheek);
    _idParamBrowLY     = idMgr->GetId(ParamBrowLY);
    _idParamBrowRY     = idMgr->GetId(ParamBrowRY);
    _idParamBrowLAngle = idMgr->GetId(ParamBrowLAngle);
    _idParamBrowRAngle = idMgr->GetId(ParamBrowRAngle);
    _idParamEyeLSmile  = idMgr->GetId(ParamEyeLSmile);
    _idParamEyeRSmile  = idMgr->GetId(ParamEyeRSmile);
}

ModelController::~ModelController()
{
    ReleaseModelSetting();
    delete _textureManager;
}

// ====================================================================
// 路径辅助
// ====================================================================

std::string ModelController::MakeAssetPath(const std::string& assetFileName)
{
    return _currentModelDirectory + assetFileName;
}

void ModelController::SetAssetDirectory(const std::string& path)
{
    _currentModelDirectory = path;
}

void ModelController::LoadAsset(const std::string& fileName,
                                const std::function<void(Csm::csmByte*, Csm::csmSizeInt)>& afterLoadCallback)
{
    Csm::csmSizeInt bufferSize = 0;
    Csm::csmByte* buffer = nullptr;
    if (fileName.empty()) return;

    buffer = LAppPal::LoadFileAsBytes(MakeAssetPath(fileName).c_str(), &bufferSize);
    afterLoadCallback(buffer, bufferSize);
    LAppPal::ReleaseBytes(buffer);
}

// ====================================================================
// SetupModel — 加载模型、注册更新器
// ====================================================================

void ModelController::SetupModel(csmUint32 width, csmUint32 height)
{
    _updating = true;
    _initialized = false;

    // 读取 model3.json
    LoadAsset(_modelDirName + ".model3.json",
        [=](Csm::csmByte* buffer, Csm::csmSizeInt bufferSize) {
            _modelJson = new Csm::CubismModelSettingJson(buffer, bufferSize);
        });

    // 加载 .moc3 数据
    LoadAsset(_modelJson->GetModelFileName(),
        [=](Csm::csmByte* buffer, Csm::csmSizeInt bufferSize) {
            LoadModel(buffer, bufferSize);
        });

    // 表情数据
    for (auto expressionIndex = 0; expressionIndex < _modelJson->GetExpressionCount(); ++expressionIndex)
    {
        LoadAsset(_modelJson->GetExpressionFileName(expressionIndex),
            [=](Csm::csmByte* buffer, Csm::csmSizeInt bufferSize) {
                auto expressionName = _modelJson->GetExpressionName(expressionIndex);
                ACubismMotion* motion = LoadExpression(buffer, bufferSize, expressionName);
                if (motion)
                {
                    if (_expressions[expressionName])
                    {
                        ACubismMotion::Delete(_expressions[expressionName]);
                        _expressions[expressionName] = nullptr;
                    }
                    _expressions[expressionName] = motion;
                }
            });
    }
    {
        CubismExpressionUpdater* expression = CSM_NEW CubismExpressionUpdater(*_expressionManager);
        _updateScheduler.AddUpdatableList(expression);
    }

    // ★ 重建表情名称列表（排除加载失败的空指针）
    rebuildExpressionList();

    // Pose
    LoadAsset(_modelJson->GetPoseFileName(),
        [=](Csm::csmByte* buffer, Csm::csmSizeInt bufferSize) {
            LoadPose(buffer, bufferSize);
        });
    if (_pose != nullptr)
    {
        CubismPoseUpdater* pose = CSM_NEW CubismPoseUpdater(*_pose);
        _updateScheduler.AddUpdatableList(pose);
    }

    // 物理
    LoadAsset(_modelJson->GetPhysicsFileName(),
        [=](Csm::csmByte* buffer, Csm::csmSizeInt bufferSize) {
            LoadPhysics(buffer, bufferSize);
        });
    if (_physics != nullptr)
    {
        CubismPhysicsUpdater* physics = CSM_NEW CubismPhysicsUpdater(*_physics);
        _updateScheduler.AddUpdatableList(physics);
    }

    // 用户数据
    LoadAsset(_modelJson->GetUserDataFile(),
        [=](Csm::csmByte* buffer, Csm::csmSizeInt bufferSize) {
            LoadUserData(buffer, bufferSize);
        });

    // ================================================================
    // 注视/跟随系统 — 仅保留眼球追踪，移除头部/身体角度跟随
    // ================================================================
    {
        _look = CubismLook::Create();

        csmVector<CubismLook::LookParameterData> lookParameters;

        // ★ 移除了以下参数（模型不会转头/转体跟随鼠标）：
        //   ParamAngleX / ParamAngleY / ParamAngleZ / ParamBodyAngleX
        //
        // ★ 仅保留眼球追踪：
        lookParameters.PushBack(CubismLook::LookParameterData(_idParamEyeBallX,   1.0f));
        lookParameters.PushBack(CubismLook::LookParameterData(_idParamEyeBallY,   0.0f, 1.0f));

        _look->SetParameters(lookParameters);

        CubismLookUpdater* look = CSM_NEW CubismLookUpdater(*_look, *_dragManager);
        _updateScheduler.AddUpdatableList(look);
    }

    _updateScheduler.SortUpdatableList();

    // Layout
    csmMap<csmString, csmFloat32> layout;
    _modelJson->GetLayoutMap(layout);
    _modelMatrix->SetupFromLayout(layout);

    _model->SaveParameters();

    // ★ 清除上一模型的 motion 状态（必须在 PreloadMotionGroup 之前）
    clearMotionState();

    // 预加载蒙层
    for (csmInt32 i = 0; i < _modelJson->GetMotionGroupCount(); i++)
    {
        const csmChar* group = _modelJson->GetMotionGroupName(i);
        PreloadMotionGroup(group);
    }

    _motionManager->StopAllMotions();

    // ★ 重置 Idle 状态
    m_lastIdleIndex = -1;

    // 创建渲染器
    CreateRenderer(width, height);

    // 纹理
    SetupTextures();

    // 自动加载未在 model3.json 中引用的 .motion3.json 文件作为 TapBody 动作
    loadAutoTapMotions();

    // ★ 从 model3.json 读取 LipSync 参数组（SDK GetLipSyncParameterCount/Id）
    loadLipSyncGroup();

    // ★ 解析并绑定情绪参数（依赖 loadLipSyncGroup 建立的 m_actualParameterMap 和 lip sync 来源）
    resolveEmotionParams();

    _updating = false;
    _initialized = true;
}

// ====================================================================
// 蒙层预加载
// ====================================================================

void ModelController::PreloadMotionGroup(const csmChar* group)
{
    const csmInt32 count = _modelJson->GetMotionCount(group);
    for (csmInt32 i = 0; i < count; i++)
    {
        csmString name = Utils::CubismString::GetFormatedString("%s_%d", group, i);
        csmString path = _modelJson->GetMotionFileName(group, i);
        path = csmString(_currentModelDirectory.c_str()) + path;

        csmByte* buffer;
        csmSizeInt size;
        buffer = CreateBuffer(path.GetRawString(), &size);
        CubismMotion* tmpMotion = static_cast<CubismMotion*>(
            LoadMotion(buffer, size, name.GetRawString(), NULL, NULL, _modelJson, group, i));

        if (tmpMotion)
        {
            if (_motions[name])
            {
                ACubismMotion::Delete(_motions[name]);
            }
            // 非 Idle 动作（TapBody 等）不应循环，否则点一次动画永远不回 Idle
            if (strcmp(group, LAppDefine::MotionGroupIdle) != 0) {
                tmpMotion->SetLoop(false);
            } else {
                // ★ 不自带 SDK 循环（SDK 时间回绕在 time==endTime 时参数瞬跳），
                //   改用 IsFinished 路径自然淡入淡出
                tmpMotion->SetLoop(false);
                tmpMotion->SetFadeInTime(0.5f);   // 淡入 0.5 秒
                tmpMotion->SetFadeOutTime(0.5f);  // 淡出 0.5 秒（被 Tap 中断时平滑过渡）
            }
            _motions[name] = tmpMotion;
        }

        // ★ 缓存此 Motion 驱动的参数索引（非插入式查询，缓存缺失不影响功能）
        {
            CubismMotionJson json(buffer, size);
            std::set<int> paramIndices;
            for (int c = 0; c < json.GetMotionCurveCount(); c++) {
                if (strcmp(json.GetMotionCurveTarget(c), "Parameter") != 0)
                    continue;
                CubismIdHandle id = json.GetMotionCurveId(c);
                if (!id) continue;
                int idx = _model->GetParameterIndex(id);
                if (idx >= 0)
                    paramIndices.insert(idx);
            }
            m_motionParameterCache[name.GetRawString()] = std::move(paramIndices);
        }

        DeleteBuffer(buffer, path.GetRawString());
    }
}

// ====================================================================
// 自动注册未引用的 .motion3.json 为 TapBody 动作
// ====================================================================

void ModelController::loadAutoTapMotions()
{
    if (!_modelJson || _currentModelDirectory.empty()) return;

    // 收集已在 model3.json 中引用的所有动作文件
    QSet<QString> referencedFiles;
    for (csmInt32 g = 0; g < _modelJson->GetMotionGroupCount(); g++)
    {
        const csmChar* group = _modelJson->GetMotionGroupName(g);
        for (csmInt32 m = 0; m < _modelJson->GetMotionCount(group); m++)
        {
            const csmChar* fileName = _modelJson->GetMotionFileName(group, m);
            if (fileName)
                referencedFiles.insert(QString::fromUtf8(fileName));
        }
    }

    // 扫描目录下的所有 .motion3.json 文件
    QString dirPath = QString::fromStdString(_currentModelDirectory);
    QDir dir(dirPath);
    QStringList motionFiles = dir.entryList(QStringList() << QStringLiteral("*.motion3.json"),
                                            QDir::Files, QDir::Name);

    m_autoTapMotionCount = 0;
    for (const QString &file : motionFiles)
    {
        if (referencedFiles.contains(file))
            continue;

        // 构造完整路径
        const QString fullPath = dir.absoluteFilePath(file);
        const csmString name = Utils::CubismString::GetFormatedString(
            "TapBody_auto_%d", m_autoTapMotionCount);

        // 读取并加载动作
        QFile f(fullPath);
        if (!f.open(QIODevice::ReadOnly))
        {
            LAppPal::PrintMessage(("[ModelController] 无法读取自动动作文件: "
                                   + fullPath.toStdString() + "\n").c_str());
            continue;
        }
        QByteArray data = f.readAll();
        f.close();

        csmByte* buffer = static_cast<csmByte*>(CSM_MALLOC(data.size()));
        memcpy(buffer, data.constData(), data.size());
        csmSizeInt size = data.size();

        CubismMotion* motion = static_cast<CubismMotion*>(
            LoadMotion(buffer, size, name.GetRawString(), nullptr, nullptr, _modelJson,
                       "TapBody", m_autoTapMotionCount));

        if (motion)
        {
            if (_motions[name])
                ACubismMotion::Delete(_motions[name]);
            motion->SetLoop(false);
            _motions[name] = motion;
            m_autoTapMotionCount++;
            LAppPal::PrintMessage(("[ModelController] 自动注册点击动作: "
                                   + file.toStdString() + " → TapBody_auto_"
                                   + std::to_string(m_autoTapMotionCount) + "\n").c_str());
        }
        else
        {
            LAppPal::PrintMessage(("[ModelController] 自动注册动作失败: "
                                   + file.toStdString() + "\n").c_str());
        }

        // ★ 缓存自动动作的参数索引
        {
            CubismMotionJson json(buffer, size);
            std::set<int> paramIndices;
            for (int c = 0; c < json.GetMotionCurveCount(); c++) {
                if (strcmp(json.GetMotionCurveTarget(c), "Parameter") != 0)
                    continue;
                CubismIdHandle id = json.GetMotionCurveId(c);
                if (!id) continue;
                int idx = _model->GetParameterIndex(id);
                if (idx >= 0)
                    paramIndices.insert(idx);
            }
            m_motionParameterCache[name.GetRawString()] = std::move(paramIndices);
        }

        CSM_FREE(buffer);
    }

    if (m_autoTapMotionCount > 0)
    {
        LAppPal::PrintMessage(("[ModelController] 共自动注册 "
                               + std::to_string(m_autoTapMotionCount) + " 个点击动作\n").c_str());
    }
}

// ====================================================================
// 🗣️ 实际参数目录枚举（替代 GetParameterIndex 存在性判断）
// ====================================================================

void ModelController::rebuildActualParameterMap()
{
    m_actualParameterMap.clear();
    if (!_model) {
        qWarning() << "[LipSync] _model 为空，无法枚举参数目录";
        return;
    }

    const Csm::csmInt32 count = _model->GetParameterCount();
    for (Csm::csmInt32 i = 0; i < count; i++) {
        const Csm::CubismIdHandle id = _model->GetParameterId(i);
        if (id) {
            m_actualParameterMap.insert(id, static_cast<int>(i));
        }
    }

    qInfo().noquote()
        << QStringLiteral("[LipSync] 已枚举 %1 个实际模型参数").arg(count);
}

int ModelController::findActualParameterIndex(Csm::CubismIdHandle id) const
{
    return findParamIndexInMap(m_actualParameterMap, id);
}

// ====================================================================
// 🎭 情绪参数解析与绑定（模型加载时调用一次）
// ====================================================================

void ModelController::resolveEmotionParams()
{
    // ── 重置 ──
    m_emotionMouthSource = EmotionMouthSource::Disabled;
    m_emotionMouthParams.clear();
    m_emotionCheek = {};
    m_emotionBrowLY = {};
    m_emotionBrowRY = {};
    m_emotionBrowLAngle = {};
    m_emotionBrowRAngle = {};

    if (!_model) {
        qInfo() << "[Emotion] _model 为空，情绪参数已禁用";
        return;
    }

    // ── 建立参数名列表（从实际模型目录） ──
    QStringList actualNames;
    const Csm::csmInt32 paramCount = _model->GetParameterCount();
    for (Csm::csmInt32 i = 0; i < paramCount; i++) {
        const auto id = _model->GetParameterId(i);
        if (id) {
            actualNames.append(QString::fromUtf8(id->GetString().GetRawString()));
        }
    }

    // ── StandardGroup 参数 ID 字符串列表 ──
    QStringList standardIds;
    for (Csm::csmUint32 i = 0; i < m_lipSyncIds.GetSize(); i++) {
        if (m_lipSyncIds[i]) {
            standardIds.append(QString::fromUtf8(m_lipSyncIds[i]->GetString().GetRawString()));
        }
    }

    // ── 调用纯逻辑解析函数 ──
    const auto resolved = resolveEmotionParameters(
        actualNames,
        static_cast<int>(m_lipSyncSource),
        standardIds,
        m_lipSyncSource == LipSyncParameterSource::ExternalConfig);

    m_emotionMouthSource = resolved.mouthSource;

    // ── 绑定辅助 lambda ──
    auto bindParam = [&](const ResolvedEmotionParameter& stub, bool isMouth = false) -> ResolvedEmotionParameter {
        if (!stub.mapped) return {};  // 未映射 → 返回空（index=-1, mapped=false）
        const auto cubismId = CubismFramework::GetIdManager()
            ->GetId(stub.id.toStdString().c_str());
        const int idx = findActualParameterIndex(cubismId);
        if (idx < 0) return {};
        ResolvedEmotionParameter p;
        p.id     = stub.id;
        p.index  = idx;
        p.minimum = _model->GetParameterMinimumValue(idx);
        p.maximum = _model->GetParameterMaximumValue(idx);
        p.defaultValue = _model->GetParameterDefaultValue(idx);
        p.mapped = true;
        p.isMouth = isMouth;
        // 非 ExternalConfig 的嘴部参数直接使用模型范围；ExternalConfig 在下面单独处理
        p.closed = p.minimum;
        p.open   = p.maximum;
        p.weight = 1.0f;
        return p;
    };

    // ── 嘴部参数绑定 ──
    if (resolved.mouthSource == EmotionMouthSource::ExternalLipSyncConfig) {
        // ExternalConfig：直接复用 m_lipSyncFallbackParams（已有 index/range/closed/open/weight）
        for (const auto& fb : m_lipSyncFallbackParams) {
            ResolvedEmotionParameter p;
            p.id     = fb.id;
            p.index  = fb.index;
            p.minimum = fb.modelMin;
            p.maximum = fb.modelMax;
            p.defaultValue = fb.modelDefault;
            p.mapped = true;
            p.isMouth = true;
            p.closed = fb.closed;
            p.open   = fb.open;
            p.weight = fb.weight;
            m_emotionMouthParams.append(p);
        }
    } else {
        // StandardGroup / ParamMouthOpenY：通过 stub 绑定
        for (const auto& stub : resolved.mouthParams) {
            auto bound = bindParam(stub, true);
            if (bound.mapped) {
                m_emotionMouthParams.append(bound);
            }
        }
    }

    // ── 其他情绪参数绑定 ──
    m_emotionCheek      = bindParam(resolved.cheek);
    m_emotionBrowLY     = bindParam(resolved.browLY);
    m_emotionBrowRY     = bindParam(resolved.browRY);
    m_emotionBrowLAngle = bindParam(resolved.browLAngle);
    m_emotionBrowRAngle = bindParam(resolved.browRAngle);

    // ── 加载阶段日志 ──
    QString mouthSrcStr;
    switch (m_emotionMouthSource) {
        case EmotionMouthSource::StandardLipSyncGroup:  mouthSrcStr = QStringLiteral("StandardLipSyncGroup"); break;
        case EmotionMouthSource::ExternalLipSyncConfig: mouthSrcStr = QStringLiteral("ExternalLipSyncConfig"); break;
        case EmotionMouthSource::ParamMouthOpenY:       mouthSrcStr = QStringLiteral("ParamMouthOpenY"); break;
        default:                                         mouthSrcStr = QStringLiteral("Disabled"); break;
    }

    QStringList mouthNames;
    for (const auto& p : m_emotionMouthParams) {
        mouthNames << QStringLiteral("%1[%2](%3..%4)")
            .arg(p.id).arg(p.index).arg(p.minimum).arg(p.maximum);
    }

    qInfo().noquote()
        << QStringLiteral("[Emotion] resolved mouth source: %1 (params: %2)")
               .arg(mouthSrcStr)
               .arg(m_emotionMouthParams.isEmpty()
                    ? QStringLiteral("none")
                    : mouthNames.join(QStringLiteral(", ")));

    auto logChannel = [](const QString& name, const ResolvedEmotionParameter& p) {
        if (p.mapped) {
            qInfo().noquote()
                << QStringLiteral("[Emotion]   %1 enabled [%2](%3..%4)")
                       .arg(name).arg(p.index).arg(p.minimum).arg(p.maximum);
        } else {
            qInfo().noquote() << QStringLiteral("[Emotion]   %1 disabled").arg(name);
        }
    };

    logChannel(QStringLiteral("cheek"),      m_emotionCheek);
    logChannel(QStringLiteral("browLY"),     m_emotionBrowLY);
    logChannel(QStringLiteral("browRY"),     m_emotionBrowRY);
    logChannel(QStringLiteral("browLAngle"), m_emotionBrowLAngle);
    logChannel(QStringLiteral("browRAngle"), m_emotionBrowRAngle);
}

// ====================================================================
// 🖊️ 情绪通道写入辅助
// ====================================================================

void ModelController::writeEmotionChannel(const ResolvedEmotionParameter& param, float value)
{
    if (!param.mapped || param.index < 0) return;
    const float clamped = qBound(param.minimum, value, param.maximum);
    _model->SetParameterValue(static_cast<Csm::csmInt32>(param.index), clamped);
}

void ModelController::writeEmotionMouth(float value)
{
    if (m_emotionMouthParams.isEmpty()) return;

    for (const auto& param : m_emotionMouthParams) {
        if (!param.mapped || param.index < 0) continue;

        float finalVal;
        if (m_emotionMouthSource == EmotionMouthSource::ExternalLipSyncConfig) {
            // ExternalConfig：使用 closed/open/weight 映射
            LipSyncParameterConfig cfg;
            cfg.id     = param.id;
            cfg.closed = static_cast<double>(param.closed);
            cfg.open   = static_cast<double>(param.open);
            cfg.weight = static_cast<double>(param.weight);
            finalVal = mapLipSyncEnvelope(value, cfg);
        } else {
            // StandardGroup / ParamMouthOpenY：直接使用情绪值
            finalVal = value;
        }

        finalVal = qBound(param.minimum, finalVal, param.maximum);
        _model->SetParameterValue(static_cast<Csm::csmInt32>(param.index), finalVal);
    }
}

// ====================================================================
// 🗣️ LipSync 参数加载与 fallback 配置
// ====================================================================

void ModelController::loadLipSyncGroup()
{
    // ── 清除所有之前的状态 ──
    m_lipSyncIds.Clear();
    m_lipSyncFallbackParams.clear();
    m_lipSyncSidecarPath.clear();
    m_lipSyncSource = LipSyncParameterSource::Disabled;
    m_lipSyncSupported = false;
    m_lipSyncEnvelope.reset();
    m_isSpeaking = false;

    if (!_modelJson) {
        qInfo() << "[LipSync] _modelJson 为空，无法加载";
        return;
    }

    // ★ 重建实际参数目录（替代 GetParameterIndex 存在性判断）
    rebuildActualParameterMap();

    // ── 尝试标准组 ──
    const Csm::csmInt32 declaredCount = _modelJson->GetLipSyncParameterCount();
    int validCount = 0;

    for (Csm::csmInt32 i = 0; i < declaredCount; i++) {
        const Csm::CubismIdHandle id = _modelJson->GetLipSyncParameterId(i);
        if (!id) continue;

        // ★ 通过实际参数目录校验：参数必须在模型中真实存在
        if (findActualParameterIndex(id) >= 0) {
            m_lipSyncIds.PushBack(id);
            validCount++;
        } else {
            qWarning().noquote()
                << QStringLiteral("[LipSync] 标准参数 \"%1\" 在模型中不存在，跳过")
                       .arg(QString::fromUtf8(id->GetString().GetRawString()));
        }
    }

    if (validCount > 0) {
        m_lipSyncSource = LipSyncParameterSource::StandardGroup;
        m_lipSyncSupported = true;
        m_lipSyncEnvelope.reset();
        m_isSpeaking = false;

        // 日志输出
        QStringList idNames;
        for (Csm::csmUint32 i = 0; i < m_lipSyncIds.GetSize(); i++) {
            idNames.append(QString::fromUtf8(m_lipSyncIds[i]->GetString().GetRawString()));
        }
        qInfo().noquote() << QStringLiteral("[LipSync] 标准组有效: %1/%2 个参数")
                             .arg(validCount).arg(declaredCount) << idNames;

        if (validCount < declaredCount) {
            qWarning().noquote()
                << QStringLiteral("[LipSync] 标准组声明 %1 个参数，其中 %2 个在模型中不存在")
                       .arg(declaredCount).arg(declaredCount - validCount);
        }
        return;
    }

    // ── 标准组无效或不存在 → 尝试 sidecar fallback ──
    if (declaredCount > 0) {
        qInfo().noquote()
            << QStringLiteral("[LipSync] 标准组声明 %1 个参数但全部无效，尝试 fallback")
                   .arg(declaredCount);
    } else {
        qInfo() << "[LipSync] 模型无 LipSync 标准组，尝试 fallback";
    }
    loadLipSyncFallback();
}

// ====================================================================
// 🗣️ Sidecar fallback 配置加载
// ====================================================================

void ModelController::loadLipSyncFallback()
{
    // ★ 使用独立函数推导 sidecar 路径
    const QString model3Dir = QString::fromStdString(_currentModelDirectory);
    const QString model3BaseName = QString::fromStdString(_modelDirName);
    const QString sidecarPath = deriveLipSyncSidecarPath(model3Dir, model3BaseName);
    m_lipSyncSidecarPath = sidecarPath;

    qInfo().noquote() << QStringLiteral("[LipSync] sidecar 路径: %1").arg(sidecarPath);

    QFileInfo fi(sidecarPath);
    if (!fi.exists()) {
        qInfo() << "[LipSync] sidecar 不存在，LipSync 已禁用";
        dumpModelParameterDirectory();
        return;
    }

    qInfo() << "[LipSync] sidecar 存在，尝试加载";

    QFile file(sidecarPath);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "[LipSync] 无法读取 sidecar:" << sidecarPath;
        dumpModelParameterDirectory();
        return;
    }

    const QByteArray data = file.readAll();
    file.close();

    // 解析配置
    QString error;
    const LipSyncConfig cfg = parseLipSyncConfig(data, &error);
    if (!error.isEmpty()) {
        qWarning().noquote()
            << QStringLiteral("[LipSync] sidecar 解析失败: %1").arg(error);
        dumpModelParameterDirectory();
        return;
    }

    if (cfg.parameters.isEmpty()) {
        qInfo() << "[LipSync] sidecar 参数列表为空，LipSync 已禁用";
        dumpModelParameterDirectory();
        return;
    }

    // 逐项验证并与实际模型参数绑定
    int resolvedCount = 0;
    for (const auto& paramCfg : cfg.parameters) {
        // 通过 ID 管理器查找或注册该 ID
        const Csm::CubismIdHandle id = CubismFramework::GetIdManager()
                                    ->GetId(paramCfg.id.toStdString().c_str());
        if (!id) {
            qWarning().noquote()
                << QStringLiteral("[LipSync] fallback 无法创建参数 ID \"%1\"，跳过")
                       .arg(paramCfg.id);
            continue;
        }

        // ★ 通过实际参数目录校验，而非 GetParameterIndex
        const Csm::csmInt32 idx = findActualParameterIndex(id);
        if (idx < 0) {
            qWarning().noquote()
                << QStringLiteral("[LipSync] fallback 参数 \"%1\" 在模型中不存在，跳过")
                       .arg(paramCfg.id);
            continue;
        }

        ResolvedLipSyncParam resolved;
        resolved.id          = paramCfg.id;
        resolved.index       = idx;
        resolved.modelMin    = _model->GetParameterMinimumValue(idx);
        resolved.modelMax    = _model->GetParameterMaximumValue(idx);
        resolved.modelDefault = _model->GetParameterDefaultValue(idx);

        // 将 closed/open 钳制到模型真实范围
        resolved.closed = qBound(resolved.modelMin,
                                  static_cast<float>(paramCfg.closed),
                                  resolved.modelMax);
        resolved.open   = qBound(resolved.modelMin,
                                  static_cast<float>(paramCfg.open),
                                  resolved.modelMax);
        resolved.weight = static_cast<float>(paramCfg.weight);

        m_lipSyncFallbackParams.append(resolved);
        resolvedCount++;

        qInfo().noquote()
            << QStringLiteral("[LipSync]   ✓ fallback 参数 \"%1\" index=%2 "
                              "range=[%3,%4] closed=%5 open=%6 weight=%7")
                   .arg(paramCfg.id).arg(idx)
                   .arg(resolved.modelMin).arg(resolved.modelMax)
                   .arg(resolved.closed).arg(resolved.open)
                   .arg(resolved.weight);
    }

    if (resolvedCount > 0) {
        m_lipSyncSource = LipSyncParameterSource::ExternalConfig;
        m_lipSyncSupported = true;
        m_lipSyncEnvelope.reset();
        m_isSpeaking = false;

        qInfo().noquote()
            << QStringLiteral("[LipSync] fallback 已启用: %1 个 resolved 参数")
                   .arg(resolvedCount);
    } else {
        qInfo() << "[LipSync] fallback 无有效参数，LipSync 已禁用";
        dumpModelParameterDirectory();
    }
}

// ====================================================================
// 📋 模型参数目录诊断（无有效 LipSync 时输出）
// ====================================================================

void ModelController::dumpModelParameterDirectory()
{
    if (!_model) {
        qInfo() << "[LipSync] 模型未加载，无法输出参数目录";
        return;
    }

    const Csm::csmInt32 count = _model->GetParameterCount();
    qInfo().noquote()
        << QStringLiteral("[LipSync] ===== 完整模型参数目录 (共 %1 个) =====").arg(count);

    for (Csm::csmInt32 i = 0; i < count; i++) {
        const Csm::CubismIdHandle id = _model->GetParameterId(i);
        if (!id) continue;

        const float minVal  = _model->GetParameterMinimumValue(i);
        const float maxVal  = _model->GetParameterMaximumValue(i);
        const float defVal  = _model->GetParameterDefaultValue(i);
        const float curVal  = _model->GetParameterValue(i);

        qInfo().noquote()
            << QStringLiteral("[LipSync]   [%1] %2  min=%3 max=%4 default=%5 current=%6")
                   .arg(i)
                   .arg(QString::fromUtf8(id->GetString().GetRawString()))
                   .arg(minVal, 0, 'f', 4)
                   .arg(maxVal, 0, 'f', 4)
                   .arg(defVal, 0, 'f', 4)
                   .arg(curVal, 0, 'f', 4);
    }
    qInfo().noquote() << QStringLiteral("[LipSync] =========================================");
}

void ModelController::setSpeaking(bool speaking)
{
    if (m_isSpeaking == speaking) return;
    m_isSpeaking = speaking;
    m_lipSyncEnvelope.setSpeaking(speaking);
}

void ModelController::ModelParamUpdate()
{
    const Csm::csmFloat32 deltaTimeSeconds = LAppPal::GetDeltaTime();
    _userTimeSeconds += deltaTimeSeconds;

    _motionUpdated = false;

    _model->LoadParameters();

    if (_motionManager->IsFinished())
    {
        // 只有当确实没任何动作在播时才重启 Idle（GetCurrentPriority==0 兜底保护）
        if (_motionManager->GetCurrentPriority() == 0) {
            // ★ 使用 Idle 选择 + 生命周期管理启动下一个 Idle
            auto handle = startRandomIdleMotion();
            if (handle == Csm::InvalidMotionQueueEntryHandleValue) {
                // 自然结束，无下一动作 → 恢复 active entry baselines（不 clear pending chain）
                MotionLifecycle::handleNaturalEnd(
                    m_activeMotionState,
                    m_modelGeneration,
                    [this](MotionLifecycle::ParamIndex idx, float val) {
                        _model->SetParameterValue(static_cast<Csm::csmInt32>(idx), val);
                    });
            }
        }
    }
    else
    {
        _motionUpdated = _motionManager->UpdateMotion(_model, deltaTimeSeconds);
    }

    // ★ ★ 参数恢复：将已退出的 Motion 驱动的参数恢复为 entry baseline
    applyPendingRestores();

    // ★ 第 1 遍：头部追踪 + 情绪先施加
    //    这样 Physics / Pose / Look 在 OnLateUpdate 中能感知到转头角度
    applyOverrideParameters();

    _model->SaveParameters();

    // ★ ★ 钳制 deltaTime 传入 Physics/Pose/Look，防止帧率波动导致物理跳跃
    //    60fps ≈ 16.7ms，允许 ±50% 波动：[8ms, 33ms]
    //    超出范围时用极值，避免物理/姿态因掉帧突然大幅变动
    const Csm::csmFloat32 clampedDt = (deltaTimeSeconds < 0.008f) ? 0.008f
                                     : (deltaTimeSeconds > 0.033f) ? 0.033f
                                     : deltaTimeSeconds;
    _updateScheduler.OnLateUpdate(_model, clampedDt);

    // ★ 第 2 遍：重新确保关键参数不被 Physics / Pose / Look 覆盖
    //    包括眼睛回中/眼动追踪、情绪表情、以及用户手动覆写
    {
        const float dt = LAppPal::GetDeltaTime(); // 极短间隔，lerp 几乎不动

        if (m_headTrackingEnabled) {
            // 头部追踪模式：物理/Look 可能把眼球又拉走了，重新居中
            _model->SetParameterValue(_idParamEyeBallX, 0.0f);
            _model->SetParameterValue(_idParamEyeBallY, 0.0f);
            // 头部角度再补一次（防止 Pose/Physics 覆盖了歪头/转头值）
            _model->SetParameterValue(_idParamAngleX, m_currentHeadAngleX);
            _model->SetParameterValue(_idParamAngleY, m_currentHeadAngleY);
            _model->SetParameterValue(_idParamAngleZ, m_currentHeadAngleZ);
            _model->SetParameterValue(_idParamBodyAngleX, m_currentBodyAngleX);
        } else {
            // 眼动追踪模式：重算平滑插值
            m_currentEyeX = lerp(m_currentEyeX, m_targetEyeX, 12.0f * dt);
            m_currentEyeY = lerp(m_currentEyeY, m_targetEyeY, 12.0f * dt);
            _model->SetParameterValue(_idParamEyeBallX, m_currentEyeX);
            _model->SetParameterValue(_idParamEyeBallY, m_currentEyeY);
        }
    }

    // 情绪表情再补一次（安全兜底，使用已解析的真实参数 index）
    writeEmotionMouth(m_currentMouthOpen);
    writeEmotionChannel(m_emotionCheek,      m_currentCheek);
    writeEmotionChannel(m_emotionBrowLY,     m_currentBrowLY);
    writeEmotionChannel(m_emotionBrowRY,     m_currentBrowRY);
    writeEmotionChannel(m_emotionBrowLAngle, m_currentBrowAngle);
    writeEmotionChannel(m_emotionBrowRAngle, m_currentBrowAngle);

    // ★ LipSync 嘴型写入（在情绪之后、用户覆写之前）
    //    StandardGroup 与 ExternalConfig 互斥，永远不同时启用。
    if (m_lipSyncSupported)
    {
        // 更新包络（说话时随机张嘴闭嘴，停止后平滑归零）
        const Csm::csmFloat32 lipDt = LAppPal::GetDeltaTime();
        const float lipVal = m_lipSyncEnvelope.update(lipDt);

        if (m_lipSyncSource == LipSyncParameterSource::StandardGroup)
        {
            // ── 标准组写入 ──
            // 保持现有视觉行为，仅当 envelope > 阈值时写入。
            if (lipVal > 0.005f)
            {
                for (Csm::csmUint32 i = 0; i < m_lipSyncIds.GetSize(); i++)
                {
                    const Csm::CubismIdHandle id = m_lipSyncIds[i];
                    if (!id) continue;

                    // ★ 使用实际参数目录的 index，避免调用 GetParameterIndex
                    const Csm::csmInt32 paramIdx = findActualParameterIndex(id);
                    if (paramIdx < 0) continue;

                    // 约束到模型参数范围
                    const float minVal = _model->GetParameterMinimumValue(paramIdx);
                    const float maxVal = _model->GetParameterMaximumValue(paramIdx);
                    const float clamped = qBound(minVal, lipVal, maxVal);

                    _model->SetParameterValue(paramIdx, clamped);
                }
            }
        }
        else if (m_lipSyncSource == LipSyncParameterSource::ExternalConfig)
        {
            // ── Fallback sidecar 写入 ──
            // 说话期间即使包络为 0（闭嘴阶段）也写入 mapped closed 值，
            // 停止说话后包络平滑归零期间继续写入，归零后停止。
            if (lipSyncShouldWrite(m_isSpeaking, lipVal))
            {
                for (const auto& param : m_lipSyncFallbackParams)
                {
                    const float mapped = mapLipSyncEnvelope(lipVal, param);
                    const float clamped = qBound(param.modelMin, mapped, param.modelMax);
                    _model->SetParameterValue(param.index, clamped);
                }
            }
        }
    }

    // 用户手动覆写最高优先级，始终在最后
    for (const auto &[index, value] : m_userParameterOverrides) {
        _model->SetParameterValue(static_cast<Csm::csmInt32>(index), value);
    }

    _model->Update();
}

// ====================================================================
// ModelOnUpdate — 模型更新 + 绘制（入口）
// ====================================================================

void ModelController::ModelOnUpdate(GLFWwindow* window)
{
    static QElapsedTimer _perfTimer;
    static double _msSetup = 0, _msParam = 0, _msDraw = 0, _msCleanup = 0;
    static int _perfCount = 0;

    _perfTimer.start();

    int width, height;
    glfwGetWindowSize(window, &width, &height);

    Csm::CubismMatrix44 projection;
    projection.LoadIdentity();

    Csm::Rendering::CubismOffscreenManager_OpenGLES2::GetInstance()->BeginFrameProcess();

    // ★ 始终 SetWidth(2.0) + Scale(1.0, width/height)，与标准 Cubism 示例一致
    GetModelMatrix()->SetWidth(2.0f);
    projection.Scale(1.0f, static_cast<float>(width) / static_cast<float>(height));

    if (MouseActionManager::GetInstance()->GetViewMatrix() != NULL)
    {
        projection.MultiplyByMatrix(MouseActionManager::GetInstance()->GetViewMatrix());
    }

    // ★ 动画安全边距补偿
    const float marginRatio = static_cast<float>(PetWindow::kAnimMarginRatio);
    const float animMarginCompensation = 1.0f / (1.0f + marginRatio * 2.0f);
    projection.ScaleRelative(animMarginCompensation, animMarginCompensation);

    // 应用位置偏移
    if (m_offsetX != 0.0f || m_offsetY != 0.0f) {
        projection.TranslateX(m_offsetX);
        projection.TranslateY(m_offsetY);
    }

    _msSetup += _perfTimer.nsecsElapsed() / 1000000.0;  // setup done

    // 参数更新（内部会调用 applyOverrideParameters）
    _perfTimer.restart();
    ModelParamUpdate();
    _msParam += _perfTimer.nsecsElapsed() / 1000000.0;  // param update done

    // 绘制
    _perfTimer.restart();
    Draw(projection);
    _msDraw += _perfTimer.nsecsElapsed() / 1000000.0;   // draw done

    // 后处理
    _perfTimer.restart();
    Csm::Rendering::CubismOffscreenManager_OpenGLES2::GetInstance()->EndFrameProcess();
    Csm::Rendering::CubismOffscreenManager_OpenGLES2::GetInstance()->ReleaseStaleRenderTextures();
    _msCleanup += _perfTimer.nsecsElapsed() / 1000000.0; // cleanup done

    // ★ 每 60 帧输出一次耗时统计
    if (++_perfCount >= 60) {
        const double total = _msSetup + _msParam + _msDraw + _msCleanup;
        const QString line = QStringLiteral("[perf] setup=%1ms  param=%2ms  draw=%3ms  cleanup=%4ms  total=%5ms")
            .arg(_msSetup / 60.0, 0, 'f', 2)
            .arg(_msParam / 60.0, 0, 'f', 2)
            .arg(_msDraw / 60.0, 0, 'f', 2)
            .arg(_msCleanup / 60.0, 0, 'f', 2)
            .arg(total / 60.0, 0, 'f', 2);
        QFile pf(QCoreApplication::applicationDirPath() + QStringLiteral("/perf_log.txt"));
        if (pf.open(QIODevice::Append | QIODevice::Text)) {
            QTextStream(&pf) << QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz"))
                             << " " << line << "\n";
        }
        _msSetup = _msParam = _msDraw = _msCleanup = 0;
        _perfCount = 0;
    }
}

// ====================================================================
// Draw
// ====================================================================

void ModelController::Draw(Csm::CubismMatrix44& matrix)
{
    if (!_model) return;

    matrix.MultiplyByMatrix(_modelMatrix);
    GetRenderer<Csm::Rendering::CubismRenderer_OpenGLES2>()->SetMvpMatrix(&matrix);
    GetRenderer<Csm::Rendering::CubismRenderer_OpenGLES2>()->DrawModel();
}

// ====================================================================
// 纹理设置
// ====================================================================

void ModelController::SetupTextures()
{
    for (csmInt32 modelTextureNumber = 0; modelTextureNumber < _modelJson->GetTextureCount(); modelTextureNumber++)
    {
        if (!strcmp(_modelJson->GetTextureFileName(modelTextureNumber), ""))
            continue;

        csmString texturePath = _modelJson->GetTextureFileName(modelTextureNumber);
        texturePath = csmString(_currentModelDirectory.c_str()) + texturePath;

        LAppTextureManager::TextureInfo* texture = _textureManager->CreateTextureFromPngFile(texturePath.GetRawString());
        const csmInt32 glTextureNumber = texture->id;

        GetRenderer<Rendering::CubismRenderer_OpenGLES2>()->BindTexture(modelTextureNumber, glTextureNumber);
    }

    GetRenderer<Rendering::CubismRenderer_OpenGLES2>()->IsPremultipliedAlpha(false);
}

// ====================================================================
// 蒙层管理
// ====================================================================

Csm::CubismMotionQueueEntryHandle ModelController::StartMotion(const Csm::csmChar* group, Csm::csmInt32 no, Csm::csmInt32 priority)
{
    if (!(_modelJson->GetMotionCount(group)))
        return Csm::InvalidMotionQueueEntryHandleValue;

    if (priority == LAppDefine::PriorityForce)
    {
        _motionManager->SetReservePriority(priority);
    }
    else if (!_motionManager->ReserveMotion(priority))
    {
        return Csm::InvalidMotionQueueEntryHandleValue;
    }

    const Csm::csmString fileName = _modelJson->GetMotionFileName(group, no);
    csmString name = Utils::CubismString::GetFormatedString("%s_%d", group, no);
    // 用 csmString 直接做 key lookup（不用 GetRawString()），确保 hashcode 一致
    CubismMotion* motion = static_cast<CubismMotion*>(_motions[name]);
    csmBool autoDelete = false;

    if (!motion)
    {
        csmString path = fileName;
        path = csmString(_currentModelDirectory.c_str()) + path;

        csmByte* buffer;
        csmSizeInt size;
        buffer = CreateBuffer(path.GetRawString(), &size);
        motion = static_cast<CubismMotion*>(LoadMotion(buffer, size, NULL, NULL, NULL, _modelJson, group, no));

        if (motion)
        {
            // ★ 回退加载路径也必须调 SetLoop + SetFadeIn，保持与 PreloadMotionGroup 一致
            if (strcmp(group, LAppDefine::MotionGroupIdle) != 0) {
                motion->SetLoop(false);
            } else {
                motion->SetLoop(false);
                motion->SetFadeInTime(0.5f);
                motion->SetFadeOutTime(0.5f);
            }
            autoDelete = true;
        }

        DeleteBuffer(buffer, path.GetRawString());
    }

    return _motionManager->StartMotionPriority(motion, autoDelete, priority);
}

// ====================================================================
// 随机点击动作 —— 从非 Idle 动作组中随机选一个播放
// ====================================================================

Csm::CubismMotionQueueEntryHandle ModelController::startRandomTapMotion()
{
    if (!_modelJson) {
        LAppPal::PrintMessage("[ModelController] _modelJson 为空，无法播放动作\n");
        return Csm::InvalidMotionQueueEntryHandleValue;
    }

    // ★ startRandomTapMotion 不再需要退出 Idle 循环（第一版移除了 proactive）

    // 第一步：收集所有非 Idle 动作组及其动作数量
    struct GroupEntry {
        csmString name;
        csmInt32  count;
    };
    csmVector<GroupEntry> tapGroups;
    csmInt32 totalRegular = 0;
    for (csmInt32 i = 0; i < _modelJson->GetMotionGroupCount(); i++) {
        const csmChar* group = _modelJson->GetMotionGroupName(i);
        if (strcmp(group, LAppDefine::MotionGroupIdle) == 0) continue;
        const csmInt32 cnt = _modelJson->GetMotionCount(group);
        if (cnt > 0) {
            tapGroups.PushBack({csmString(group), cnt});
            totalRegular += cnt;
        }
    }

    const csmInt32 totalMotions = totalRegular + m_autoTapMotionCount;

    if (totalMotions == 0) {
        // 根本没有可播放的动作 → 兜底重播 Idle
        LAppPal::PrintMessage(
            "[ModelController] 无可用的点击动作，回退播放 Idle。\n"
            "提示：在 model3.json 的 Motions 中添加 TapBody 组，\n"
            "      或将未引用的 .motion3.json 文件放入模型目录即可自动注册。\n");
        return beginMotionTransition(LAppDefine::MotionGroupIdle, 0, LAppDefine::PriorityForce);
    }

    // 从所有动作（含自动注册）中随机选一个
    const int pick = QRandomGenerator::global()->bounded(totalMotions);

    if (pick < totalRegular) {
        // 选中了模型中的某个非 Idle 动作
        int offset = 0;
        for (csmUint32 g = 0; g < tapGroups.GetSize(); g++) {
            if (pick < offset + tapGroups[g].count) {
                const int motionIdx = pick - offset;
                LAppPal::PrintMessage(("[ModelController] 播放点击动作: "
                                       + std::string(tapGroups[g].name.GetRawString())
                                       + "_" + std::to_string(motionIdx) + "\n").c_str());
                return beginMotionTransition(tapGroups[g].name.GetRawString(),
                                             motionIdx, LAppDefine::PriorityForce);
            }
            offset += tapGroups[g].count;
        }
    }

    // 选中了自动注册的动作
    const int autoIdx = pick - totalRegular;
    csmString motionName = Utils::CubismString::GetFormatedString(
        "TapBody_auto_%d", autoIdx);

    LAppPal::PrintMessage(("[ModelController] 播放自动注册点击动作: TapBody_auto_"
                           + std::to_string(autoIdx) + "\n").c_str());

    if (_motions[motionName]) {
        // ★ 自动动作的生命周期管理（inline，因不经过 StartMotion 的组路径）
        const std::string autoKey = motionName.GetRawString();

        // ⚠️ 查缓存（非插入式），缺失则 fail closed
        auto cacheIt = m_motionParameterCache.find(autoKey);
        if (cacheIt == m_motionParameterCache.end()) {
            LAppPal::PrintMessage(("[ModelController] auto motion cache miss for \""
                                   + autoKey + "\", failing closed\n").c_str());
            return Csm::InvalidMotionQueueEntryHandleValue;
        }
        std::set<int> incomingParams = cacheIt->second;

        // 计算 incoming entryBaselines（含继承，使用共享算法）
        auto incomingBaselines = MotionLifecycle::computeIncomingBaselines(
            incomingParams,
            m_activeMotionState,
            m_modelGeneration,
            m_pendingRestoreChain,
            [this](MotionLifecycle::ParamIndex idx) {
                return _model->GetParameterValue(static_cast<Csm::csmInt32>(idx));
            });

        // 准备 outgoing-only 恢复信息（本地，未推入链，使用共享算法）
        auto [pendingRestoreEntry, hasPendingToPush] = MotionLifecycle::computeOutgoingOnly(
            m_activeMotionState, m_modelGeneration, incomingParams);

        _motionManager->SetReservePriority(LAppDefine::PriorityForce);
        auto handle = _motionManager->StartMotionPriority(
            _motions[motionName], false, LAppDefine::PriorityForce);

        if (handle != Csm::InvalidMotionQueueEntryHandleValue) {
            // ✅ 成功：冲突解决 + 推送 pending restore + 更新 active state
            // 冲突解决必须在任何条件下执行：新 motion 驱动的参数从全部 pending 中移除
            MotionLifecycle::resolveConflict(m_pendingRestoreChain, incomingParams);
            if (hasPendingToPush) {
                m_pendingRestoreChain.push_back(std::move(pendingRestoreEntry));
            }

            m_activeMotionState.motionKey = autoKey;
            m_activeMotionState.handle = handle;
            m_activeMotionState.drivenParams = std::move(incomingParams);
            m_activeMotionState.entryBaselines = std::move(incomingBaselines);
            m_activeMotionState.generation = m_modelGeneration;
            m_activeMotionState.valid = true;
        }
        // ❌ 失败：pendingRestoreEntry 超出作用域丢弃，不修改任何状态

        return handle;
    }

    return Csm::InvalidMotionQueueEntryHandleValue;
}

Csm::CubismMotionQueueEntryHandle ModelController::startMotionByGroup(const Csm::csmChar* group, Csm::csmInt32 no, Csm::csmInt32 priority)
{
    return beginMotionTransition(group, no, priority);
}

// ====================================================================
// 😊 预制表情 API
// ====================================================================

void ModelController::rebuildExpressionList()
{
    m_expressionNames.clear();
    for (auto it = _expressions.Begin(); it != _expressions.End(); ++it)
    {
        if (it->Second != nullptr)
        {
            m_expressionNames.append(QString::fromUtf8(it->First.GetRawString()));
        }
    }
    m_lastExpressionIndex = -1;

    qInfo().noquote() << QStringLiteral("[ModelController] Expressions loaded: %1")
                         .arg(m_expressionNames.size());
    for (const QString &name : m_expressionNames)
    {
        qInfo().noquote() << QStringLiteral("  - %1").arg(name);
    }
}

bool ModelController::hasExpressions() const
{
    return !m_expressionNames.isEmpty();
}

int ModelController::expressionCount() const
{
    return m_expressionNames.size();
}

QStringList ModelController::expressionNames() const
{
    return m_expressionNames;
}

bool ModelController::startExpressionByIndex(int index)
{
    if (!_expressionManager)
    {
        LAppPal::PrintMessage("[ModelController] _expressionManager 为空，无法播放表情\n");
        return false;
    }

    if (index < 0 || index >= m_expressionNames.size())
    {
        LAppPal::PrintMessage("[ModelController] 表情索引越界\n");
        return false;
    }

    const std::string key = m_expressionNames[index].toStdString();
    ACubismMotion* motion = _expressions[key.c_str()];
    if (motion == nullptr)
    {
        LAppPal::PrintMessage("[ModelController] 表情对象为空，无法播放\n");
        return false;
    }

    _expressionManager->StartMotion(motion, false);

    LAppPal::PrintMessage(("[ModelController] 播放预制表情: "
                           + key + "\n").c_str());
    return true;
}

bool ModelController::startExpressionByName(const QString &name)
{
    const int idx = m_expressionNames.indexOf(name);
    if (idx < 0)
    {
        LAppPal::PrintMessage("[ModelController] 表情名称不存在，跳过\n");
        return false;
    }
    return startExpressionByIndex(idx);
}

bool ModelController::startRandomExpression()
{
    const int count = m_expressionNames.size();
    if (count == 0)
    {
        LAppPal::PrintMessage("[ModelController] 无可用预制表情，跳过随机播放\n");
        return false;
    }

    int pick;
    if (count == 1)
    {
        pick = 0;
    }
    else
    {
        do {
            pick = QRandomGenerator::global()->bounded(count);
        } while (pick == m_lastExpressionIndex);
    }

    m_lastExpressionIndex = pick;
    return startExpressionByIndex(pick);
}

void ModelController::ReleaseModelSetting()
{
    for (auto iter = _motions.Begin(); iter != _motions.End(); ++iter)
    {
        Csm::ACubismMotion::Delete(iter->Second);
    }
    _motions.Clear();

    for (auto iter = _expressions.Begin(); iter != _expressions.End(); ++iter)
    {
        Csm::ACubismMotion::Delete(iter->Second);
    }
    _expressions.Clear();
    m_expressionNames.clear();
    m_lastExpressionIndex = -1;

    delete _modelJson;

    Csm::Rendering::CubismOffscreenManager_OpenGLES2::ReleaseInstance();
}

// ====================================================================
// 🛠️ 参数面板接口
// ====================================================================

int ModelController::getParameterCount() const
{
    if (!_model) return 0;
    return static_cast<int>(_model->GetParameterCount());
}

QString ModelController::getParameterId(int index) const
{
    if (!_model) return {};
    const auto id = _model->GetParameterId(static_cast<Csm::csmUint32>(index));
    return QString::fromUtf8(id->GetString().GetRawString());
}

float ModelController::getParameterMinimumValue(int index) const
{
    if (!_model) return 0.0f;
    return _model->GetParameterMinimumValue(static_cast<Csm::csmUint32>(index));
}

float ModelController::getParameterMaximumValue(int index) const
{
    if (!_model) return 0.0f;
    return _model->GetParameterMaximumValue(static_cast<Csm::csmUint32>(index));
}

float ModelController::getParameterDefaultValue(int index) const
{
    if (!_model) return 0.0f;
    return _model->GetParameterDefaultValue(static_cast<Csm::csmUint32>(index));
}

float ModelController::getParameterCurrentValue(int index) const
{
    if (!_model) return 0.0f;
    return _model->GetParameterValue(static_cast<Csm::csmUint32>(index));
}

void ModelController::setUserParameterOverride(int index, float value)
{
    m_userParameterOverrides[static_cast<int>(index)] = value;
}

void ModelController::clearUserParameterOverride(int index)
{
    m_userParameterOverrides.erase(static_cast<int>(index));
}

void ModelController::clearAllUserOverrides()
{
    m_userParameterOverrides.clear();
}

bool ModelController::hasUserOverrides() const
{
    return !m_userParameterOverrides.empty();
}

std::map<int, float> ModelController::getAllUserOverrides() const
{
    return m_userParameterOverrides;
}

void ModelController::applyUserOverrides(const std::map<int, float> &overrides)
{
    for (const auto &[index, value] : overrides) {
        m_userParameterOverrides[index] = value;
    }
}

void ModelController::forceParamUpdate()
{
    ModelParamUpdate();
}

void ModelController::setHeadTrackingEnabled(bool enabled)
{
    m_headTrackingEnabled = enabled;
    LAppPal::PrintMessage((std::string("[ModelController] 头部追踪 ") + (enabled ? "开启" : "关闭") + "\n").c_str());
}

// ====================================================================
// 新增公开接口
// ====================================================================

void ModelController::setEmotion(PetAnimationState emotion)
{
    if (m_currentEmotion == emotion) return;
    m_currentEmotion = emotion;
    m_emotionSwitchTime = _userTimeSeconds;

    float mouthOpen, cheek, browLY, browRY, browAngle;
    getEmotionTargets(emotion, mouthOpen, cheek, browLY, browRY, browAngle);

    m_targetMouthOpen = mouthOpen;
    m_targetCheek     = cheek;
    m_targetBrowLY    = browLY;
    m_targetBrowRY    = browRY;
    m_targetBrowAngle = browAngle;
}

void ModelController::setEyeTarget(float x, float y)
{
    m_targetEyeX = x;
    m_targetEyeY = y;
}

void ModelController::setPositionOffset(float x, float y)
{
    m_offsetX = x;
    m_offsetY = y;
}

void ModelController::getModelBounds(double &outMinX, double &outMinY,
                                      double &outWidth, double &outHeight) const
{
    if (!_model) {
        outMinX = outMinY = outWidth = outHeight = 0.0;
        return;
    }

    const Csm::csmInt32 drawableCount = _model->GetDrawableCount();
    if (drawableCount <= 0) {
        outMinX = outMinY = outWidth = outHeight = 0.0;
        return;
    }

    float mnX = FLT_MAX, mnY = FLT_MAX;
    float mxX = -FLT_MAX, mxY = -FLT_MAX;
    int acceptedDrawables = 0;

    for (Csm::csmInt32 i = 0; i < drawableCount; i++)
    {
        // ── 过滤器 ──
        // 跳过动态不可见的 Drawable（粒子、特效等）
        if (!_model->GetDrawableDynamicFlagIsVisible(i))
            continue;
        // 跳过透明度近乎 0 的 Drawable（完全透明 → 排除）
        if (_model->GetDrawableOpacity(i) < 0.01f)
            continue;

        const auto* vertices = _model->GetDrawableVertexPositions(i);
        const Csm::csmInt32 vertexCount = _model->GetDrawableVertexCount(i);

        // 使用共享的纯函数计算单个 Drawable 的 AABB
        // 内部处理：count < 3、NaN/Inf、退化包围盒
        float dMnX = FLT_MAX, dMnY = FLT_MAX;
        float dMxX = -FLT_MAX, dMxY = -FLT_MAX;
        int validVerts = 0;

        for (Csm::csmInt32 v = 0; v < vertexCount; v++)
        {
            DrawableBounds::AccumulateVertex(
                vertices[v].X, vertices[v].Y,
                dMnX, dMnY, dMxX, dMxY, validVerts);
        }

        if (!DrawableBounds::FinalizeDrawable(dMnX, dMnY, dMxX, dMxY, validVerts))
            continue;

        // 合并到总体 bounds
        if (dMnX < mnX) mnX = dMnX;
        if (dMnY < mnY) mnY = dMnY;
        if (dMxX > mxX) mxX = dMxX;
        if (dMxY > mxY) mxY = dMxY;
        ++acceptedDrawables;
    }

    if (acceptedDrawables == 0)
    {
        // 无有效 Drawable → 回退到画布尺寸
        outMinX = 0.0;
        outMinY = 0.0;
        outWidth  = static_cast<double>(_model->GetCanvasWidth());
        outHeight = static_cast<double>(_model->GetCanvasHeight());
        return;
    }

    outMinX   = static_cast<double>(mnX);
    outMinY   = static_cast<double>(mnY);
    outWidth  = static_cast<double>(mxX - mnX);
    outHeight = static_cast<double>(mxY - mnY);

    // 最终安全检查
    if (outWidth <= 0.0 || outHeight <= 0.0)
    {
        outMinX = outMinY = 0.0;
        outWidth  = static_cast<double>(_model->GetCanvasWidth());
        outHeight = static_cast<double>(_model->GetCanvasHeight());
    }
}

// ====================================================================
// 🎯 命中检测区域 — 优先 model3.json HitAreas，其次 drawable 名称匹配
// ====================================================================

void ModelController::getInteractionAreas(QVector<HitAreaAABB> &headAreas,
                                           QVector<HitAreaAABB> &bodyAreas) const
{
    headAreas.clear();
    bodyAreas.clear();

    auto* model = GetModel();
    if (!model || !_modelJson) return;

    // ── Priority 1: HitAreas from model3.json ──
    const Csm::csmInt32 hitAreaCount = _modelJson->GetHitAreasCount();
    for (Csm::csmInt32 i = 0; i < hitAreaCount; i++)
    {
        // SDK 5 R6: GetHitAreaName 返回 csmChar*，GetHitAreaId 返回 CubismIdHandle
        const Csm::csmChar* areaName = _modelJson->GetHitAreaName(i);
        const auto drawableId = _modelJson->GetHitAreaId(i);
        if (!areaName || !drawableId) continue;

        // Find matching drawable by CubismIdHandle (SDK 保证相同 ID 的 handle 指针相等)
        const Csm::csmInt32 drawableCount = model->GetDrawableCount();
        for (Csm::csmInt32 d = 0; d < drawableCount; d++)
        {
            if (model->GetDrawableId(d) != drawableId) continue;

            // Compute AABB from vertex positions (current frame, follows animation)
            const Csm::csmInt32 vCount = model->GetDrawableVertexCount(d);
            const auto* vertices = model->GetDrawableVertexPositions(d);
            if (vCount <= 0 || !vertices) break;

            float minX = vertices[0].X, minY = vertices[0].Y;
            float maxX = vertices[0].X, maxY = vertices[0].Y;
            for (Csm::csmInt32 v = 1; v < vCount; v++)
            {
                const float vx = vertices[v].X;
                const float vy = vertices[v].Y;
                if (vx < minX) minX = vx;
                if (vx > maxX) maxX = vx;
                if (vy < minY) minY = vy;
                if (vy > maxY) maxY = vy;
            }

            HitAreaAABB area;
            area.name = QString::fromUtf8(areaName);
            area.minX = minX; area.minY = minY;
            area.maxX = maxX; area.maxY = maxY;

            const QString nameLower = area.name.toLower();
            // 匹配头部 HitArea 常见命名（英文/日文/中文）
            static const QStringList kHeadHitAreaNames = {
                QStringLiteral("head"),       // 标准英文
                QStringLiteral("face"),       // 面部
                QStringLiteral("hair"),       // 头发（部分模型头发作为头部区域）
                QStringLiteral("atama"),      // 頭（日文）
                QStringLiteral("头部"),        // 中文
                QStringLiteral("脸部"),        // 中文
                QStringLiteral("头发"),        // 中文
            };
            // 匹配身体 HitArea 常见命名
            static const QStringList kBodyHitAreaNames = {
                QStringLiteral("body"),       // 标准英文
                QStringLiteral("torso"),      // 躯干
                QStringLiteral("bust"),       // 胸部
                QStringLiteral("karada"),     // 体（日文）
                QStringLiteral("身体"),        // 中文
                QStringLiteral("胴体"),        // 中文
            };

            if (kHeadHitAreaNames.contains(nameLower)) {
                headAreas.append(area);
            } else if (kBodyHitAreaNames.contains(nameLower)) {
                bodyAreas.append(area);
            }
            break;
        }
    }

    // If both head and body found from HitAreas, we're done
    if (!headAreas.isEmpty() && !bodyAreas.isEmpty()) return;

    // ── Priority 2: Drawable name matching (for missing area types only) ──
    const Csm::csmInt32 drawableCount = model->GetDrawableCount();
    for (Csm::csmInt32 d = 0; d < drawableCount && (headAreas.isEmpty() || bodyAreas.isEmpty()); d++)
    {
        const auto* id = model->GetDrawableId(d);
        if (!id) continue;

        const QString name = QString::fromUtf8(id->GetString().GetRawString());
        const QString nameLower = name.toLower();

        // Compute AABB
        const Csm::csmInt32 vCount = model->GetDrawableVertexCount(d);
        const auto* vertices = model->GetDrawableVertexPositions(d);
        if (vCount <= 0 || !vertices) continue;

        float minX = vertices[0].X, minY = vertices[0].Y;
        float maxX = vertices[0].X, maxY = vertices[0].Y;
        for (Csm::csmInt32 v = 1; v < vCount; v++)
        {
            const float vx = vertices[v].X;
            const float vy = vertices[v].Y;
            if (vx < minX) minX = vx;
            if (vx > maxX) maxX = vx;
            if (vy < minY) minY = vy;
            if (vy > maxY) maxY = vy;
        }

        // Check if this drawable matches head patterns
        bool isHead = false;
        if (nameLower == QStringLiteral("head") ||
            nameLower == QStringLiteral("face") ||
            nameLower == QStringLiteral("neck") ||
            nameLower == QStringLiteral("atama") ||
            nameLower == QStringLiteral("头部") ||
            nameLower == QStringLiteral("脸部") ||
            nameLower.startsWith(QStringLiteral("head")) ||
            nameLower.startsWith(QStringLiteral("face")) ||
            nameLower.startsWith(QStringLiteral("neck")))
        {
            isHead = true;
        }
        if (!isHead && (nameLower.startsWith(QStringLiteral("hair_front")) ||
                        nameLower.startsWith(QStringLiteral("hair_back")) ||
                        nameLower == QStringLiteral("hairfront") ||
                        nameLower == QStringLiteral("hairback") ||
                        nameLower == QStringLiteral("hair") ||
                        nameLower == QStringLiteral("头发")))
        {
            isHead = true;
        }

        // Check if this drawable matches body patterns
        bool isBody = false;
        if (nameLower == QStringLiteral("body") ||
            nameLower == QStringLiteral("bust") ||
            nameLower == QStringLiteral("torso") ||
            nameLower == QStringLiteral("karada") ||
            nameLower == QStringLiteral("身体") ||
            nameLower.startsWith(QStringLiteral("body")) ||
            nameLower.startsWith(QStringLiteral("arm")) ||
            nameLower.startsWith(QStringLiteral("leg")) ||
            nameLower.startsWith(QStringLiteral("foot")) ||
            nameLower.startsWith(QStringLiteral("skirt")) ||
            nameLower.startsWith(QStringLiteral("hand")))
        {
            isBody = true;
        }

        if (isHead && headAreas.isEmpty())
        {
            HitAreaAABB area;
            area.name = name;
            area.minX = minX; area.minY = minY;
            area.maxX = maxX; area.maxY = maxY;
            headAreas.append(area);
        }
        else if (isBody && bodyAreas.isEmpty())
        {
            HitAreaAABB area;
            area.name = name;
            area.minX = minX; area.minY = minY;
            area.maxX = maxX; area.maxY = maxY;
            bodyAreas.append(area);
        }
    }
}

// ====================================================================
// 情绪参数目标值映射
// ====================================================================

void ModelController::getEmotionTargets(PetAnimationState emotion,
                                         float& outMouthOpen, float& outCheek,
                                         float& outBrowLY, float& outBrowRY,
                                         float& outBrowAngle) const
{
    // 参数值范围参考 Live2D 标准参数定义：
    //   ParamMouthOpenY : 0 ~ 1
    //   ParamCheek      : 0 ~ 1 (脸红)
    //   ParamBrowLY/Y   : -1 ~ 1 (负=下 正=上)
    //   ParamBrowLAngle : -30 ~ 30 (角度)
    switch (emotion)
    {
    case PetAnimationState::Idle:
        outMouthOpen = 0.0f;  outCheek     = 0.0f;
        outBrowLY    = 0.0f;  outBrowRY    = 0.0f;
        outBrowAngle = 0.0f;
        break;

    case PetAnimationState::Happy:
        outMouthOpen = 0.25f; outCheek     = 0.35f;
        outBrowLY    = 0.4f;  outBrowRY    = 0.4f;
        outBrowAngle = 0.2f;
        break;

    case PetAnimationState::Angry:
        outMouthOpen = -0.15f; outCheek     = 0.0f;
        outBrowLY    = -0.5f;  outBrowRY    = -0.5f;
        outBrowAngle = -0.3f;
        break;

    case PetAnimationState::Shy:
        outMouthOpen = 0.0f;  outCheek     = 0.6f;
        outBrowLY    = 0.2f;  outBrowRY    = 0.2f;
        outBrowAngle = 0.1f;
        break;

    case PetAnimationState::Sleepy:
        outMouthOpen = 0.1f;  outCheek     = 0.0f;
        outBrowLY    = -0.2f; outBrowRY    = -0.2f;
        outBrowAngle = 0.0f;
        break;

    case PetAnimationState::Thinking:
        outMouthOpen = 0.0f;  outCheek     = 0.0f;
        outBrowLY    = -0.35f; outBrowRY   = -0.2f;
        outBrowAngle = -0.4f;
        break;
    }
}

// ====================================================================
// applyOverrideParameters — 在每帧 update 和 draw 之间注入
// ====================================================================

void ModelController::applyOverrideParameters()
{
    if (!_model) return;

    const float deltaTime = LAppPal::GetDeltaTime();

    // ------------------------------------------------
    // 1. 头部/眼动追踪模式切换
    //    ★ 头部跟随鼠标模式：头部转向光标方向，眼睛回到正中
    //    ★ 眼动追踪模式（默认）：头部归零，眼睛跟随光标
    // ------------------------------------------------
    if (m_headTrackingEnabled) {
        const float headLerpSpeed = 15.0f;
        m_currentHeadAngleX = lerp(m_currentHeadAngleX, m_targetEyeX * 40.0f, headLerpSpeed * deltaTime);
        m_currentHeadAngleY = lerp(m_currentHeadAngleY, m_targetEyeY * 30.0f, headLerpSpeed * deltaTime);
        m_currentHeadAngleZ = lerp(m_currentHeadAngleZ, m_targetEyeX * -12.0f, headLerpSpeed * deltaTime);
        m_currentBodyAngleX = lerp(m_currentBodyAngleX, m_targetEyeX * 15.0f, headLerpSpeed * deltaTime);

        _model->SetParameterValue(_idParamAngleX, m_currentHeadAngleX);
        _model->SetParameterValue(_idParamAngleY, m_currentHeadAngleY);
        _model->SetParameterValue(_idParamAngleZ, m_currentHeadAngleZ);
        _model->SetParameterValue(_idParamBodyAngleX, m_currentBodyAngleX);

        // 头部跟随模式 → 眼睛回到正中
        _model->SetParameterValue(_idParamEyeBallX, 0.0f);
        _model->SetParameterValue(_idParamEyeBallY, 0.0f);
    } else {
        // 默认：头部/身体角度归零
        _model->SetParameterValue(_idParamAngleX, 0.0f);
        _model->SetParameterValue(_idParamAngleY, 0.0f);
        _model->SetParameterValue(_idParamAngleZ, 0.0f);
        _model->SetParameterValue(_idParamBodyAngleX, 0.0f);

        // 眼动追踪 — 使用平滑插值避免眼睛瞬移
        const float eyeLerpSpeed = 12.0f;
        m_currentEyeX = lerp(m_currentEyeX, m_targetEyeX, eyeLerpSpeed * deltaTime);
        m_currentEyeY = lerp(m_currentEyeY, m_targetEyeY, eyeLerpSpeed * deltaTime);
        _model->SetParameterValue(_idParamEyeBallX, m_currentEyeX);
        _model->SetParameterValue(_idParamEyeBallY, m_currentEyeY);
    }

    // ------------------------------------------------
    // 3. 情绪表情参数 — 平滑过渡
    // ------------------------------------------------
    const float emotionLerpSpeed = 5.0f;
    m_currentMouthOpen = lerp(m_currentMouthOpen, m_targetMouthOpen, emotionLerpSpeed * deltaTime);
    m_currentCheek     = lerp(m_currentCheek,     m_targetCheek,     emotionLerpSpeed * deltaTime);
    m_currentBrowLY    = lerp(m_currentBrowLY,    m_targetBrowLY,    emotionLerpSpeed * deltaTime);
    m_currentBrowRY    = lerp(m_currentBrowRY,    m_targetBrowRY,    emotionLerpSpeed * deltaTime);
    m_currentBrowAngle = lerp(m_currentBrowAngle, m_targetBrowAngle, emotionLerpSpeed * deltaTime);

    // ★ ★ 使用已解析的真实参数 index 写入（不存在的参数不写入）
    writeEmotionMouth(m_currentMouthOpen);
    writeEmotionChannel(m_emotionCheek,      m_currentCheek);
    writeEmotionChannel(m_emotionBrowLY,     m_currentBrowLY);
    writeEmotionChannel(m_emotionBrowRY,     m_currentBrowRY);
    writeEmotionChannel(m_emotionBrowLAngle, m_currentBrowAngle);
    writeEmotionChannel(m_emotionBrowRAngle, m_currentBrowAngle);

    // ------------------------------------------------
    // 4. 🛠️ 用户手动覆写参数 — 在情绪/眼动系统之后施加，优先级最高
    // ------------------------------------------------
    for (const auto &[index, value] : m_userParameterOverrides) {
        _model->SetParameterValue(static_cast<Csm::csmInt32>(index), value);
    }
}

// ====================================================================
// 🎬 Motion 参数生命周期 — 实现
// ====================================================================

int ModelController::selectIdleIndex(int count, int lastIndex)
{
    if (count <= 1) return 0;
    const int boundedValue = QRandomGenerator::global()->bounded(count - 1);
    return MotionLifecycle::selectIdleIndexFromBounded(count, lastIndex, boundedValue);
}

void ModelController::clearMotionState()
{
    m_motionParameterCache.clear();
    MotionLifecycle::clearMotionState(m_activeMotionState, m_pendingRestoreChain, m_modelGeneration);
}

void ModelController::applyPendingRestores()
{
    auto it = m_pendingRestoreChain.begin();
    while (it != m_pendingRestoreChain.end()) {
        // 旧 generation → 废弃
        if (it->generation != m_modelGeneration) {
            it = m_pendingRestoreChain.erase(it);
            continue;
        }

        Csm::CubismMotionQueueEntry* entry =
            _motionManager->GetCubismMotionQueueEntry(it->handle);

        if (entry == nullptr || entry->IsFinished()) {
            // Motion 已从队列移除或已结束 → 一次性恢复后清除
            for (auto& [idx, val] : it->baselines) {
                _model->SetParameterValue(static_cast<Csm::csmInt32>(idx), val);
            }
            it = m_pendingRestoreChain.erase(it);
        } else {
            // Motion 仍在 fadeout → 本帧恢复
            for (auto& [idx, val] : it->baselines) {
                _model->SetParameterValue(static_cast<Csm::csmInt32>(idx), val);
            }
            ++it;
        }
    }
}

Csm::CubismMotionQueueEntryHandle ModelController::beginMotionTransition(
    const Csm::csmChar* group, Csm::csmInt32 no, Csm::csmInt32 priority)
{
    // ── 1. 构建 incoming motion key ──
    csmString name = Utils::CubismString::GetFormatedString("%s_%d", group, no);
    std::string motionKey = name.GetRawString();

    // ── 2. 查 incoming 参数集（非插入式查询） ──
    auto cacheIt = m_motionParameterCache.find(motionKey);
    if (cacheIt == m_motionParameterCache.end()) {
        // ⚠️ 缓存缺失：fail closed，不调用 StartMotion，不修改任何状态
        LAppPal::PrintMessage(("[ModelController] beginMotionTransition: "
                               "cache miss for \"" + motionKey
                               + "\", failing closed\n").c_str());
        return Csm::InvalidMotionQueueEntryHandleValue;
    }
    // cacheIt->second 可能是空 set（该 motion 无 Target="Parameter" 曲线），
    // 这是正常情况，可以继续生命周期管理。
    std::set<int> incomingParams = cacheIt->second;

    // ── 3. 计算 incoming entryBaselines（三层继承链） ──
    auto incomingBaselines = MotionLifecycle::computeIncomingBaselines(
        incomingParams,
        m_activeMotionState,
        m_modelGeneration,
        m_pendingRestoreChain,
        [this](MotionLifecycle::ParamIndex idx) {
            return _model->GetParameterValue(static_cast<Csm::csmInt32>(idx));
        });

    // ── 4. 准备 outgoing-only 恢复信息（本地，未推入链） ──
    auto [pendingRestoreEntry, hasPendingToPush] = MotionLifecycle::computeOutgoingOnly(
        m_activeMotionState, m_modelGeneration, incomingParams);

    // ── 5. 执行 StartMotion ──
    Csm::CubismMotionQueueEntryHandle handle = StartMotion(group, no, priority);

    if (handle != Csm::InvalidMotionQueueEntryHandleValue) {
        // ✅ 成功：冲突解决 + 推送 pending restore + 更新 active state
        // 冲突解决必须在任何条件下执行：新 motion 驱动的参数从全部 pending 中移除
        MotionLifecycle::resolveConflict(m_pendingRestoreChain, incomingParams);
        if (hasPendingToPush) {
            m_pendingRestoreChain.push_back(std::move(pendingRestoreEntry));
        }
        m_activeMotionState.motionKey = motionKey;
        m_activeMotionState.handle = handle;
        m_activeMotionState.drivenParams = std::move(incomingParams);
        m_activeMotionState.entryBaselines = std::move(incomingBaselines);
        m_activeMotionState.generation = m_modelGeneration;
        m_activeMotionState.valid = true;
    }
    // ❌ 失败：pendingRestoreEntry 超出作用域丢弃，不修改任何状态

    return handle;
}

Csm::CubismMotionQueueEntryHandle ModelController::startRandomIdleMotion()
{
    const Csm::csmInt32 count = _modelJson->GetMotionCount(LAppDefine::MotionGroupIdle);
    if (count <= 0)
        return Csm::InvalidMotionQueueEntryHandleValue;

    // 选择 Idle 索引（避免连续重复）
    const Csm::csmInt32 index = (count == 1) ? 0
        : selectIdleIndex(static_cast<int>(count), m_lastIdleIndex);
    m_lastIdleIndex = static_cast<int>(index);

    // 通过生命周期管理启动
    return beginMotionTransition(LAppDefine::MotionGroupIdle, index, LAppDefine::PriorityForce);
}

// ====================================================================
// lerp
// ====================================================================

float ModelController::lerp(float from, float to, float t)
{
    if (t >= 1.0f) return to;
    return from + (to - from) * t;
}
