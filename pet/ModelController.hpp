#pragma once

#include <functional>
#include <map>
#include <QString>
#include <QVector>

#include <CubismFramework.hpp>
#include <CubismModelSettingJson.hpp>

#include "LAppTextureManager.hpp"
#include "LAppModel_Common.hpp"
#include "PetAnimationState.h"

/**
 * @brief ModelController — CubismUserModelExtend 的增强版。
 *
 * 在原版能力基础上新增：
 *  - 🎯 纯眼动追踪（头部/身体不跟随鼠标转动）
 *  - 🎭 情绪驱动表情参数（由 EmotionAnalyzer 结果驱动）
 *  - 以上参数在 ModelOnUpdate 内部的 update → draw 之间注入，
 *    保证每一帧的渲染都包含正确的表情和视线。
 */
class ModelController : public LAppModel_Common
{
public:
    ModelController(const std::string modelDirectoryName, const std::string currentModelDirectory);
    virtual ~ModelController();

    /**
     * @brief model3.json からモデルを生成する
     */
    void SetupModel(Csm::csmUint32 width, Csm::csmUint32 height);

    /**
     * @brief モデルの更新（参数更新 + 绘制）
     *
     * 在参数更新和绘制之间注入覆写逻辑：
     *  1. 头部/身体角度归零（不受拖拽影响）
     *  2. 眼球位置跟随鼠标（纯眼睛追踪）
     *  3. 情绪表情参数
     */
    void ModelOnUpdate(GLFWwindow* window);

    // ====================================================================
    // 新增公开接口
    // ====================================================================

    /// 设置当前情绪状态 → 驱动表情参数
    void setEmotion(PetAnimationState emotion);

    /// 设置眼球目标位置（归一化 -1 ~ 1，相对于窗口）
    void setEyeTarget(float x, float y);

    /// 设置模型在渲染区域中的偏移量（归一化坐标，-1 ~ 1，正值偏右/下）
    void setPositionOffset(float x, float y);

    /// 计算模型所有绘制网格的紧贴包围盒（模型坐标系，不含画布空白区域）。
    /// 用于 PetWindow 精确调整窗口大小，让窗口紧贴模型内容而非画布。
    /// 同时返回顶点包围盒左上角 (minX, minY)，用于命中检测精确定位。
    void getModelBounds(double &outMinX, double &outMinY,
                        double &outWidth, double &outHeight) const;

    /// 自动注册目录中未被 model3.json 引用的 .motion3.json 文件为 TapBody 动作
    void loadAutoTapMotions();

    // ====================================================================
    // 🛠️ 参数面板接口 — 暴露 Live2D 原生的可调参数
    // ====================================================================

    /// 返回模型中可调参数的总数
    int getParameterCount() const;

    /// 返回指定索引的参数 ID 字符串（如 "ParamEyeLOpen"）
    QString getParameterId(int index) const;

    /// 返回指定索引的参数最小值
    float getParameterMinimumValue(int index) const;

    /// 返回指定索引的参数最大值
    float getParameterMaximumValue(int index) const;

    /// 返回指定索引的参数默认值
    float getParameterDefaultValue(int index) const;

    /// 返回指定索引的当前有效值（叠加了所有覆写后的值）
    float getParameterCurrentValue(int index) const;

    /// 设置用户手动覆写的参数值
    void setUserParameterOverride(int index, float value);

    /// 清除单个参数的用户覆写（恢复为底层值）
    void clearUserParameterOverride(int index);

    /// 清除所有用户参数覆写
    void clearAllUserOverrides();

    /// 是否有活跃的用户参数覆写
    bool hasUserOverrides() const;

    /// 获取所有用户覆写（index → value），用于保存预设
    std::map<int, float> getAllUserOverrides() const;

    /// 批量应用用户覆写（加载预设）
    void applyUserOverrides(const std::map<int, float> &overrides);

    /// 强制运行参数更新（清掉覆写后立即刷新模型参数值，不等下一帧）
    void forceParamUpdate();

    /// 设置头部跟随鼠标模式（开启后眼动追踪关闭，模型头部跟随光标转动）
    void setHeadTrackingEnabled(bool enabled);
    bool headTrackingEnabled() const { return m_headTrackingEnabled; }

    /// 从非 Idle 动作组中随机播放一个动作（TapBody/Tap 等点击动作）
    Csm::CubismMotionQueueEntryHandle startRandomTapMotion();

    /// 直接播放指定动作组/索引的动作（公开封装 StartMotion）
    Csm::CubismMotionQueueEntryHandle startMotionByGroup(const Csm::csmChar* group, Csm::csmInt32 no, Csm::csmInt32 priority);

    // ====================================================================
    // 😊 预制表情 API（*.exp3.json 表情播放）
    // ====================================================================

    /// 是否有成功加载的预制表情
    bool hasExpressions() const;

    /// 成功加载的预制表情数量
    int expressionCount() const;

    /// 所有成功加载的表情名称列表（QStringList 便于 QML/UI 使用）
    QStringList expressionNames() const;

    /// 按索引播放预制表情。越界返回 false。
    bool startExpressionByIndex(int index);

    /// 按名称播放预制表情。名称不存在返回 false。
    bool startExpressionByName(const QString& name);

    /// 随机播放一个已注册预制表情。无表情返回 false。
    /// 多于 1 个时尽量避免连续两次相同表情。
    bool startRandomExpression();

private:
    std::string MakeAssetPath(const std::string& assetFileName);
    void SetAssetDirectory(const std::string& path);
    void LoadAsset(const std::string& fileName,
                   const std::function<void(Csm::csmByte*, Csm::csmSizeInt)>& afterLoadCallback);

    Csm::CubismMotionQueueEntryHandle StartMotion(const Csm::csmChar* group, Csm::csmInt32 no, Csm::csmInt32 priority);
    void ReleaseModelSetting();
    void Draw(Csm::CubismMatrix44& matrix);
    void ModelParamUpdate();
    void SetupTextures();
    void PreloadMotionGroup(const Csm::csmChar* group);

    /// 覆写参数：在 ModelParamUpdate() 之后、Draw() 之前调用
    void applyOverrideParameters();

    /// 获取某个情绪对应的目标参数值表
    void getEmotionTargets(PetAnimationState emotion,
                           float& outMouthOpen, float& outCheek,
                           float& outBrowLY, float& outBrowRY,
                           float& outBrowAngle) const;

    /// 一阶线性插值
    static float lerp(float from, float to, float t);

    std::string _modelDirName;
    std::string _currentModelDirectory;

    Csm::csmFloat32 _userTimeSeconds;
    Csm::CubismModelSettingJson* _modelJson;
    Csm::csmVector<Csm::CubismIdHandle> _eyeBlinkIds;
    Csm::csmMap<Csm::csmString, Csm::ACubismMotion*> _motions;
    Csm::csmMap<Csm::csmString, Csm::ACubismMotion*> _expressions;

    LAppTextureManager* _textureManager;

    // --- 标准参数 ID（继承自 SDK） ---
    const Csm::CubismId* _idParamAngleX;
    const Csm::CubismId* _idParamAngleY;
    const Csm::CubismId* _idParamAngleZ;
    const Csm::CubismId* _idParamBodyAngleX;
    const Csm::CubismId* _idParamEyeBallX;
    const Csm::CubismId* _idParamEyeBallY;

    // --- 表情相关参数 ID（新增） ---
    const Csm::CubismId* _idParamMouthOpenY;
    const Csm::CubismId* _idParamCheek;
    const Csm::CubismId* _idParamBrowLY;
    const Csm::CubismId* _idParamBrowRY;
    const Csm::CubismId* _idParamBrowLAngle;
    const Csm::CubismId* _idParamBrowRAngle;
    const Csm::CubismId* _idParamEyeLSmile;
    const Csm::CubismId* _idParamEyeRSmile;

    Csm::csmBool _motionUpdated;

    // ====================================================================
    // 新增状态
    // ====================================================================

    PetAnimationState m_currentEmotion = PetAnimationState::Idle;

    // 目标值（瞬态设置）
    float m_targetMouthOpen = 0.0f;
    float m_targetCheek     = 0.0f;
    float m_targetBrowLY    = 0.0f;
    float m_targetBrowRY    = 0.0f;
    float m_targetBrowAngle = 0.0f;

    // 当前值（平滑插值用）
    float m_currentMouthOpen = 0.0f;
    float m_currentCheek     = 0.0f;
    float m_currentBrowLY    = 0.0f;
    float m_currentBrowRY    = 0.0f;
    float m_currentBrowAngle = 0.0f;

    // 眼球追踪
    float m_targetEyeX = 0.0f;
    float m_targetEyeY = 0.0f;
    float m_currentEyeX = 0.0f;
    float m_currentEyeY = 0.0f;

    // 头部追踪（头部跟随鼠标模式）
    float m_currentHeadAngleX = 0.0f;
    float m_currentHeadAngleY = 0.0f;
    float m_currentHeadAngleZ = 0.0f;
    float m_currentBodyAngleX = 0.0f;
    bool  m_headTrackingEnabled = false;

    // 当情绪变化时，记录切换时刻用于过渡
    float m_emotionSwitchTime = 0.0f;

    // 模型位置偏移（归一化坐标，用于微调模型在窗口中的位置）
    float m_offsetX = 0.0f;
    float m_offsetY = 0.0f;

    // ====================================================================
    // 🛠️ 用户参数覆写 — 在情绪/眼动系统之后施加
    // ====================================================================
    std::map<int, float> m_userParameterOverrides; // index → value

    // 自动注册的点击动作数量（来自未在 model3.json 中引用的 .motion3.json 文件）
    int m_autoTapMotionCount = 0;

    // ====================================================================
    // 😊 预制表情相关
    // ====================================================================

    /// 成功加载的预制表情名称列表（与 _expressions 映射同步）
    QStringList m_expressionNames;

    /// 上次播放的表情索引，-1 表示从未播放。用于避免连续重复。
    int m_lastExpressionIndex = -1;

    /// 从 _expressions 映射重建 m_expressionNames（排除加载失败的空指针条目）
    void rebuildExpressionList();

    // Idle 循环控制：主动交叉淡入淡出替代 SDK 时间回绕
    float m_idleDuration = 0.0f;            // Idle 动画总时长（秒），取自 motion3.json
    float m_lastIdleRestartTime = 0.0f;     // 上次启动 Idle 的时间点（_userTimeSeconds）
    bool  m_inProactiveIdleLoop = false;    // true=正在 Idle 循环中，false=被 Tap 等打断

public:
    // ====================================================================
    // 🎯 命中检测区域 — model3.json HitAreas / drawable 名称匹配
    // ====================================================================
    struct HitAreaAABB {
        QString name;
        float minX = 0.0f, minY = 0.0f, maxX = 0.0f, maxY = 0.0f;
    };
    void getInteractionAreas(QVector<HitAreaAABB> &headAreas,
                              QVector<HitAreaAABB> &bodyAreas) const;
};
