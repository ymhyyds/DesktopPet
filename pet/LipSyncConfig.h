#pragma once

#include <QString>
#include <QVector>
#include <QByteArray>
#include <QHash>
#include <algorithm>
#include <cmath>

// ====================================================================
// LipSyncConfig — 纯逻辑数据结构与函数，不依赖 OpenGL / Live2D / Cubism
// ====================================================================

/// 当前 LipSync 参数来源
enum class LipSyncParameterSource {
    Disabled,        ///< 无有效来源
    StandardGroup,   ///< model3.json Groups LipSync 标准组（至少一个有效真实参数）
    ExternalConfig   ///< *.lipsync.json sidecar 配置（至少一个有效 resolved 参数）
};

/// 配置文件中单个参数条目
struct LipSyncParameterConfig {
    QString id;        ///< 参数 ID（如 "ParamMouthOpenY"）
    double closed = 0.0;   ///< 闭嘴时的值
    double open   = 1.0;   ///< 张嘴时的值
    double weight = 1.0;   ///< 包络缩放因子
};

/// 完整配置文件内容
struct LipSyncConfig {
    int version = 1;
    QVector<LipSyncParameterConfig> parameters;
};

/// 运行时 resolved 参数（与实际模型参数绑定）
struct ResolvedLipSyncParam {
    QString id;               ///< 参数 ID 字符串
    int index = -1;           ///< 模型参数索引
    float modelMin    = 0.0f; ///< 模型参数最小值
    float modelMax    = 1.0f; ///< 模型参数最大值
    float modelDefault = 0.0f;///< 模型参数默认值
    float closed      = 0.0f; ///< 配置 closed（已钳制到模型范围）
    float open        = 1.0f; ///< 配置 open（已钳制到模型范围）
    float weight      = 1.0f; ///< 配置 weight
};

// ====================================================================
// 纯函数
// ====================================================================

/**
 * @brief 从 JSON 字节解析 LipSync 配置文件。
 * @param json    输入 JSON 字节
 * @param errorOut 可选输出，成功时为空，失败时包含错误描述
 * @return 解析结果。即使返回的 config 中 parameters 为空也不视为解析失败，
 *         只有当 JSON 格式错误导致无法解析时才视为失败。
 *
 * 解析规则（见设计文档第四节）：
 *  - 文件不存在 → 不是解析错误
 *  - JSON 损坏 / 根节点非对象 / version 异常 / parameters 缺失 → 拒绝
 *  - parameters 为空 → 配置有效但无参数
 *  - 单个参数项无效 → 跳过该条目并警告
 */
LipSyncConfig parseLipSyncConfig(const QByteArray& json, QString* errorOut = nullptr);

/**
 * @brief 从实际加载的 model3 文件路径推导 sidecar 路径。
 * @param model3Dir      模型目录（含尾部分隔符）
 * @param model3BaseName model3 文件基名（不含 ".model3.json" 后缀）
 * @return sidecar JSON 文件完整路径
 *
 * 例：
 *   ("characters/Hiyori/", "Hiyori") → "characters/Hiyori/Hiyori.lipsync.json"
 *   ("characters/草神/", "草神")     → "characters/草神/草神.lipsync.json"
 */
QString deriveLipSyncSidecarPath(const QString& model3Dir,
                                  const QString& model3BaseName);

/**
 * @brief 将包络值通过映射公式转换为参数值。
 *
 * u = clamp(envelope * weight, 0.0, 1.0)
 * value = closed + u * (open - closed)
 *
 * closed > open 自然表示反向参数。
 */
inline float mapLipSyncEnvelope(float envelope, const LipSyncParameterConfig& config)
{
    const float u = std::clamp(envelope * static_cast<float>(config.weight), 0.0f, 1.0f);
    return static_cast<float>(config.closed)
           + u * (static_cast<float>(config.open) - static_cast<float>(config.closed));
}

/// 映射已 resolved 的参数（等价的数学运算）
inline float mapLipSyncEnvelope(float envelope, const ResolvedLipSyncParam& param)
{
    const float u = std::clamp(envelope * param.weight, 0.0f, 1.0f);
    return param.closed + u * (param.open - param.closed);
}

/**
 * @brief 判断 fallback 当前帧是否应写入参数。
 * @param speaking      当前说话状态
 * @param envelopeValue 当前包络值
 * @param epsilon       包络接近零的阈值
 * @return true=应写入
 *
 * 说话期间即使包络为 0（闭嘴阶段）也要写入 mapped closed 值。
 * speaking 从 true 变 false 后包络平滑归零期间继续写入。
 * speaking==false 且包络已归零时返回 false。
 */
inline bool lipSyncShouldWrite(bool speaking, float envelopeValue,
                                float epsilon = 0.005f)
{
    return speaking || envelopeValue > epsilon;
}

/**
 * @brief 根据标准组和 sidecar 的可用性选择参数来源。
 * @param standardGroupCount  标准组声明的参数个数（GetLipSyncParameterCount）
 * @param validStandardCount  标准组中真正有效的参数个数（在模型中存在）
 * @param sidecarExists       sidecar 文件是否存在
 * @param sidecarHasValidParams sidecar 是否有至少一个有效参数
 * @return 选定的源
 *
 * 优先顺序：
 * 1. 标准组至少一个有效参数 → StandardGroup
 * 2. sidecar 存在且有效     → ExternalConfig
 * 3. 否则                  → Disabled
 */
LipSyncParameterSource selectLipSyncSource(
    int standardGroupCount,
    int validStandardCount,
    bool sidecarExists,
    bool sidecarHasValidParams);

/**
 * @brief 在实际参数索引映射中查找候选参数 ID。
 * @param paramMap     由枚举 _model->GetParameterId() 建立的 { 指针 → index } 映射
 * @param candidateId  候选参数 ID 指针
 * @return 实际索引，-1 表示参数在模型中不存在
 *
 * 纯指针比较，类型无关。
 * 生产代码中映射 key 为 CubismIdHandle（const CubismId*），隐式转为 const void*。
 * 测试代码中可用任意唯一定址的变量地址模拟。
 */
inline int findParamIndexInMap(const QHash<const void*, int>& paramMap, const void* candidateId)
{
    return paramMap.value(candidateId, -1);
}
