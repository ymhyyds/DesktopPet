#pragma once

#include <QObject>

/**
 * @brief AffectionSystem 管理桌宠好感度。
 *
 * 满好感度 300，通过右键交互行为累积：
 *  - 打招呼 +1
 *  - 摸摸头 +3
 *  - 喂食 +2
 *  - 晚安 +1
 *
 * 好感度仅在内存中维护，退出后重置（后续可扩展持久化到 MemoryDB）。
 */
class AffectionSystem : public QObject
{
    Q_OBJECT

public:
    explicit AffectionSystem(QObject *parent = nullptr);

    /// 增加好感度（自动封顶到 maxValue）。
    void add(int points);

    /// 当前好感度值。
    int current() const { return m_value; }

    /// 好感度上限。
    static constexpr int maxValue() { return 300; }

    /// 重置好感度。
    void reset();

    /// 直接设置好感度值（用于从持久化存储恢复，不发射 changed 信号）。
    void setValue(int value);

signals:
    /// 好感度变化时触发。newValue = 变化后的值，delta = 本次变化量。
    void changed(int newValue, int delta);

private:
    int m_value = 0;
};
