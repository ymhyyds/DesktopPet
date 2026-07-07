#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

// ====================================================================
// EmotionParameterResolver — 纯逻辑数据结构与函数。
// 不依赖 OpenGL / Live2D / Cubism SDK，仅 Qt6::Core。
// ====================================================================

/// 运行时 resolved 情绪参数
struct ResolvedEmotionParameter {
    QString id;              ///< 参数 ID 字符串（如 "ParamMouthOpenY"）
    int index = -1;          ///< 模型参数索引（-1 = 不存在 = 该通道 disabled）
    float minimum    = 0.0f; ///< 模型参数最小值
    float maximum    = 1.0f; ///< 模型参数最大值
    float defaultValue = 0.0f;///< 模型参数默认值
    bool mapped = false;     ///< false = 该通道不写入模型

    // ── 嘴部专用 ──
    bool   isMouth = false;  ///< 是否为嘴部参数
    float  closed  = 0.0f;   ///< 闭嘴映射值（仅 ExternalConfig 有效）
    float  open    = 1.0f;   ///< 张嘴映射值（仅 ExternalConfig 有效）
    float  weight  = 1.0f;   ///< 缩放因子（仅 ExternalConfig 有效）
};

/// Emotion mouthOpen 参数来源（与 LipSync 来源对应但增加了直接 ParamMouthOpenY fallback）
enum class EmotionMouthSource {
    Disabled,               ///< 无有效嘴部参数
    ExternalLipSyncConfig,  ///< 外部 *.lipsync.json 配置
    StandardLipSyncGroup,   ///< model3.json Groups LipSync 标准组
    ParamMouthOpenY         ///< 真实存在的精确 ParamMouthOpenY
};

/// 完整的情绪参数解析结果
struct ResolvedEmotionParameters {
    EmotionMouthSource mouthSource = EmotionMouthSource::Disabled;

    /// 嘴部参数列表（空 = disabled）。
    /// StandardGroup 可能含多个参数，都将参与写入。
    QVector<ResolvedEmotionParameter> mouthParams;

    // ── 非嘴部情绪通道（index < 0 = disabled）──
    ResolvedEmotionParameter cheek;
    ResolvedEmotionParameter browLY;
    ResolvedEmotionParameter browRY;
    ResolvedEmotionParameter browLAngle;
    ResolvedEmotionParameter browRAngle;

    // ── 当前未写入，仅记录存在性 ──
    ResolvedEmotionParameter eyeLSmile;
    ResolvedEmotionParameter eyeRSmile;
};

// ====================================================================
// 解析函数
// ====================================================================

/**
 * @brief 根据实际模型参数列表和当前 LipSync 来源，解析情绪参数。
 *
 * 纯逻辑函数，不依赖 Cubism SDK。
 * 返回 resolved 参数结构（含名称 + mapped 标志），
 * 但不填充 index/minimum/maximum/defaultValue（那是绑定层的工作）。
 *
 * 规则：
 * 1. mouth 优先级：ExternalConfig > StandardGroup > ParamMouthOpenY > Disabled
 * 2. cheek/brow* 只在实际存在时写入
 * 3. 不猜测 AJ*、ParamMouthForm、ParamBrowForm
 * 4. 不按模型名称硬编码
 * 5. EyeSmile 当前不参与写入
 *
 * @param actualParameterNames  模型中真实存在的所有参数 ID 字符串列表
 * @param lipSyncSource         当前 LipSync 参数来源
 * @param standardLipSyncIds    标准 LipSync 组参数 ID（仅 lipSyncSource==StandardGroup 时有效）
 * @param enableExternalConfig  是否启用外部配置（lipSyncSource==ExternalConfig 时）
 * @return 解析结果
 */
ResolvedEmotionParameters resolveEmotionParameters(
    const QStringList& actualParameterNames,
    int lipSyncSource,                    // LipSyncParameterSource 的整数值
    const QStringList& standardLipSyncIds = {},
    bool enableExternalConfig = false);
