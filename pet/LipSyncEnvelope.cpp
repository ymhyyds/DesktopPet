#include "LipSyncEnvelope.h"

#include <QRandomGenerator>
#include <algorithm>

LipSyncEnvelope::LipSyncEnvelope() = default;

void LipSyncEnvelope::reset()
{
    m_speaking = false;
    m_currentValue = 0.0f;
    m_targetValue = 0.0f;
    m_cycleTimer = 0.0f;
    m_cycleDuration = 0.0f;
    m_isOpenPhase = false;
    m_hasOpened = false;
    m_releaseTimer = 0.0f;
    m_releasing = false;
}

void LipSyncEnvelope::setSpeaking(bool speaking)
{
    if (m_speaking == speaking) return;

    if (speaking) {
        m_speaking = true;
        m_hasOpened = false;
        m_releasing = false;
        m_releaseTimer = 0.0f;
        // 立即开始一个闭嘴阶段（准备第一个张嘴）
        m_isOpenPhase = false;
        m_cycleTimer = 0.0f;
        m_cycleDuration = 0.06f; // 快速短暂闭嘴后张嘴
        m_targetValue = 0.0f;
    } else {
        // 停止说话 → 进入释放阶段
        m_speaking = false;
        m_releasing = true;
        m_releaseTimer = 0.0f;
        m_targetValue = 0.0f;
    }
}

float LipSyncEnvelope::update(float deltaTime)
{
    // 保护极值
    if (deltaTime <= 0.0f) return m_currentValue;
    if (deltaTime > 0.1f) deltaTime = 0.1f; // 限制单帧最大 100ms

    if (m_releasing) {
        // ── 释放阶段：快速归零 ──
        m_releaseTimer += deltaTime;
        const float t = std::min(1.0f, kReleaseSpeed * deltaTime);
        m_currentValue = m_currentValue + (m_targetValue - m_currentValue) * t;

        // 如果已经回到 0，结束释放
        if (m_currentValue < 0.001f) {
            m_currentValue = 0.0f;
            m_releasing = false;
            m_targetValue = 0.0f;
        }
        return m_currentValue;
    }

    if (!m_speaking) {
        // 非说话状态且不在释放中 → 确保为 0
        if (m_currentValue > 0.001f) {
            const float t = std::min(1.0f, kReleaseSpeed * deltaTime);
            m_currentValue += (0.0f - m_currentValue) * t;
        } else {
            m_currentValue = 0.0f;
        }
        return m_currentValue;
    }

    // ── 说话状态 ──
    m_cycleTimer += deltaTime;

    // 检查是否该切换阶段
    if (m_cycleTimer >= m_cycleDuration) {
        m_cycleTimer = 0.0f;
        startNewCycle();
    }

    // 向目标值平滑插值
    const float speed = m_isOpenPhase ? kLerpSpeed : kLerpSpeed * 1.3f; // 闭嘴略快
    const float t = std::min(1.0f, speed * deltaTime);
    m_currentValue = m_currentValue + (m_targetValue - m_currentValue) * t;

    return m_currentValue;
}

void LipSyncEnvelope::startNewCycle()
{
    if (m_isOpenPhase) {
        // ── 当前张嘴阶段结束 → 切到闭嘴 ──
        m_isOpenPhase = false;

        // 偶尔产生稍长的闭嘴停顿
        if (nextRandom() < kLongPauseChance) {
            m_cycleDuration = kLongPauseDur;
        } else {
            m_cycleDuration = kCloseDurMin + nextRandom() * (kCloseDurMax - kCloseDurMin);
        }
        m_targetValue = kCloseMin + nextRandom() * (kCloseMax - kCloseMin);

    } else {
        // ── 当前闭嘴阶段结束 → 切到张嘴 ──
        m_isOpenPhase = true;
        m_hasOpened = true;

        m_cycleDuration = kOpenDurMin + nextRandom() * (kOpenDurMax - kOpenDurMin);
        m_targetValue = kOpenMin + nextRandom() * (kOpenMax - kOpenMin);
    }
}

float LipSyncEnvelope::nextRandom()
{
    if (m_randomFn) {
        return m_randomFn();
    }
    // 默认全局随机源
    return static_cast<float>(QRandomGenerator::global()->bounded(10000)) / 9999.0f;
}
