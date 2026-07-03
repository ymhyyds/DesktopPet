#pragma once

/**
 * @brief 桌宠情绪/动画状态枚举。
 *
 * 由 EmotionAnalyzer 根据 AI 回复文本关键词匹配得出，
 * 未来可接入 Live2D 的表情参数驱动。
 */
enum class PetAnimationState
{
    Idle,
    Happy,
    Angry,
    Shy,
    Sleepy,
    Thinking
};
