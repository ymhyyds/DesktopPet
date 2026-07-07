#pragma once

#include <QObject>

/**
 * @brief LipSyncEnvelope — 纯嘴型包络算法，无 Qt 外部依赖，可单元测试。
 *
 * 说话时在"张嘴"和"闭嘴"目标之间随机切换，闭嘴期偶尔产生稍长停顿。
 * 不说话时快速但平滑回到 0。
 *
 * 所有时间单位为秒，值范围为 [0, 1]。
 */
class LipSyncEnvelope final
{
public:
    LipSyncEnvelope();

    /// 清除状态，复位所有内部计时器和当前值
    void reset();

    /// 通知说话状态变化
    void setSpeaking(bool speaking);

    bool isSpeaking() const { return m_speaking; }
    float currentValue() const { return m_currentValue; }

    /**
     * @brief 每帧调用一次，更新嘴型值。
     * @param deltaTime 本帧时间增量（秒）
     * @return 当前嘴型值 [0, 1]
     */
    float update(float deltaTime);

    // ====================================================================
    // 测试支持：注入随机源，默认使用 QRandomGenerator::global()
    // ====================================================================
    using RandomFunc = float (*)();  // 返回 [0, 1)
    void setRandomSource(RandomFunc fn) { m_randomFn = fn; }

private:
    float nextRandom();
    void  startNewCycle();

private:
    bool   m_speaking = false;
    float  m_currentValue = 0.0f;      // 当前插值位置
    float  m_targetValue = 0.0f;       // 当前目标值
    float  m_cycleTimer = 0.0f;        // 当前阶段已过时间
    float  m_cycleDuration = 0.0f;     // 当前阶段总时长
    bool   m_isOpenPhase = false;      // true=张嘴, false=闭嘴
    bool   m_hasOpened = false;        // 本次说话中是否已至少张嘴过一次

    // 定时器：从 setSpeaking(false) 开始的复位时间
    float  m_releaseTimer = 0.0f;
    bool   m_releasing = false;

    RandomFunc m_randomFn = nullptr;

    // ── 常量参数 ──
    static constexpr float kOpenMin     = 0.35f;
    static constexpr float kOpenMax     = 0.80f;
    static constexpr float kCloseMin    = 0.00f;
    static constexpr float kCloseMax    = 0.12f;
    static constexpr float kOpenDurMin  = 0.06f;   // 60ms
    static constexpr float kOpenDurMax  = 0.16f;   // 160ms
    static constexpr float kCloseDurMin = 0.06f;
    static constexpr float kCloseDurMax = 0.16f;
    static constexpr float kLongPauseChance = 0.15f;  // 15% 概率出现长闭
    static constexpr float kLongPauseDur    = 0.35f;  // 长闭持续 350ms
    static constexpr float kLerpSpeed      = 14.0f;   // 插值速度
    static constexpr float kReleaseSpeed   = 18.0f;   // 复位插值速度
};
