#include "AffectionSystem.h"

#include <QDebug>

AffectionSystem::AffectionSystem(QObject *parent)
    : QObject(parent)
{
}

void AffectionSystem::add(int points)
{
    if (points == 0) return;

    const int oldValue = m_value;
    m_value = qBound(0, m_value + points, maxValue());
    const int delta = m_value - oldValue;

    if (delta != 0) {
        const QString sign = (delta > 0) ? QStringLiteral("+") : QString();
        qInfo() << "[Affection] 好感度" << sign << delta << "（当前：" << m_value << "）";
        emit changed(m_value, delta);
    }
}

void AffectionSystem::reset()
{
    m_value = 0;
    emit changed(0, 0);
}

void AffectionSystem::setValue(int value)
{
    m_value = qBound(0, value, maxValue());
    // 不发射 changed 信号，避免启动时触发浮动文字
}
