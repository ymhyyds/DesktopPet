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
#include <Id/CubismIdManager.hpp>

#include "LAppPal.hpp"
#include "LAppDefine.hpp"
#include "MouseActionManager.hpp"

#include "ModelController.hpp"

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

    // 预加载蒙层
    for (csmInt32 i = 0; i < _modelJson->GetMotionGroupCount(); i++)
    {
        const csmChar* group = _modelJson->GetMotionGroupName(i);
        PreloadMotionGroup(group);
    }

    _motionManager->StopAllMotions();

    // 创建渲染器
    CreateRenderer(width, height);

    // 纹理
    SetupTextures();

    // 自动加载未在 model3.json 中引用的 .motion3.json 文件作为 TapBody 动作
    loadAutoTapMotions();

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
                //   改用主动交叉淡入淡出闭环（ModelParamUpdate 中结束前 0.5s 用 PriorityForce 预启动）
                tmpMotion->SetLoop(false);
                tmpMotion->SetFadeInTime(0.5f);   // 淡入 0.5 秒
                tmpMotion->SetFadeOutTime(0.5f);  // 淡出 0.5 秒（被 Tap 中断或主动重启时平滑过渡）
                m_idleDuration = tmpMotion->GetDuration();  // 保存实际时长用于预重启判断
            }
            _motions[name] = tmpMotion;
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

        CSM_FREE(buffer);
    }

    if (m_autoTapMotionCount > 0)
    {
        LAppPal::PrintMessage(("[ModelController] 共自动注册 "
                               + std::to_string(m_autoTapMotionCount) + " 个点击动作\n").c_str());
    }
}

// ====================================================================
// ModelParamUpdate — 参数更新（原版逻辑不变）
// ====================================================================

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
            StartMotion(LAppDefine::MotionGroupIdle, 0, LAppDefine::PriorityIdle);
            m_lastIdleRestartTime = _userTimeSeconds;
            m_inProactiveIdleLoop = true;
        }
    }
    else
    {
        _motionUpdated = _motionManager->UpdateMotion(_model, deltaTimeSeconds);

        // ★ 主动交叉淡入淡出闭环
        //    在 Idle 结束前 ~0.5s 用 PriorityForce 预启动下一个 Idle，
        //    SDK 自动处理旧→新的淡出/淡入，避免 IsFinished → StartMotion 之间的参数跳变
        //    仅当确实在 Idle 循环中时（非 Tap 播放中）才触发
        if (m_inProactiveIdleLoop && m_idleDuration > 0.0f)
        {
            const float elapsed = _userTimeSeconds - m_lastIdleRestartTime;
            const float restartLead = 0.5f;  // 提前 0.5 秒开始交叉过渡
            if (elapsed >= m_idleDuration - restartLead) {
                StartMotion(LAppDefine::MotionGroupIdle, 0, LAppDefine::PriorityForce);
                m_lastIdleRestartTime = _userTimeSeconds;
                // Tap 播放中时 m_inProactiveIdleLoop 为 false，因此不会误进入此处
                m_inProactiveIdleLoop = true;
            }
        }
    }

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

    // 情绪表情再补一次（安全兜底）
    _model->SetParameterValue(_idParamMouthOpenY, m_currentMouthOpen);
    _model->SetParameterValue(_idParamCheek,      m_currentCheek);
    _model->SetParameterValue(_idParamBrowLY,     m_currentBrowLY);
    _model->SetParameterValue(_idParamBrowRY,     m_currentBrowRY);
    _model->SetParameterValue(_idParamBrowLAngle, m_currentBrowAngle);
    _model->SetParameterValue(_idParamBrowRAngle, m_currentBrowAngle);

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

    // 退出主动 Idle 循环，防止 ModelParamUpdate 中误触发 Idle 预重启打断 Tap
    m_inProactiveIdleLoop = false;

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
        return StartMotion(LAppDefine::MotionGroupIdle, 0, LAppDefine::PriorityForce);
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
                return StartMotion(tapGroups[g].name.GetRawString(),
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
        _motionManager->SetReservePriority(LAppDefine::PriorityForce);
        return _motionManager->StartMotionPriority(
            _motions[motionName], false, LAppDefine::PriorityForce);
    }

    return Csm::InvalidMotionQueueEntryHandleValue;
}

Csm::CubismMotionQueueEntryHandle ModelController::startMotionByGroup(const Csm::csmChar* group, Csm::csmInt32 no, Csm::csmInt32 priority)
{
    // 非 Idle 动作开始时退出主动 Idle 循环
    if (strcmp(group, LAppDefine::MotionGroupIdle) != 0) {
        m_inProactiveIdleLoop = false;
    }
    return StartMotion(group, no, priority);
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

    float minX =  FLT_MAX, minY =  FLT_MAX;
    float maxX = -FLT_MAX, maxY = -FLT_MAX;

    for (Csm::csmInt32 i = 0; i < drawableCount; i++)
    {
        const auto* vertices = _model->GetDrawableVertexPositions(i);
        const Csm::csmInt32 vertexCount = _model->GetDrawableVertexCount(i);

        for (Csm::csmInt32 v = 0; v < vertexCount; v++)
        {
            const float vx = vertices[v].X;
            const float vy = vertices[v].Y;
            if (vx < minX) minX = vx;
            if (vy < minY) minY = vy;
            if (vx > maxX) maxX = vx;
            if (vy > maxY) maxY = vy;
        }
    }

    outMinX   = static_cast<double>(minX);
    outMinY   = static_cast<double>(minY);
    outWidth  = static_cast<double>(maxX - minX);
    outHeight = static_cast<double>(maxY - minY);

    // 安全检查：如果计算结果异常（所有顶点重合等），回退到画布尺寸
    if (outWidth <= 0.0 || outHeight <= 0.0)
    {
        outMinX   = 0.0;
        outMinY   = 0.0;
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

    _model->SetParameterValue(_idParamMouthOpenY, m_currentMouthOpen);
    _model->SetParameterValue(_idParamCheek,      m_currentCheek);
    _model->SetParameterValue(_idParamBrowLY,     m_currentBrowLY);
    _model->SetParameterValue(_idParamBrowRY,     m_currentBrowRY);
    _model->SetParameterValue(_idParamBrowLAngle, m_currentBrowAngle);
    _model->SetParameterValue(_idParamBrowRAngle, m_currentBrowAngle);

    // ------------------------------------------------
    // 4. 🛠️ 用户手动覆写参数 — 在情绪/眼动系统之后施加，优先级最高
    // ------------------------------------------------
    for (const auto &[index, value] : m_userParameterOverrides) {
        _model->SetParameterValue(static_cast<Csm::csmInt32>(index), value);
    }
}

// ====================================================================
// lerp
// ====================================================================

float ModelController::lerp(float from, float to, float t)
{
    if (t >= 1.0f) return to;
    return from + (to - from) * t;
}
