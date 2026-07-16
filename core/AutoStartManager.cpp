#include "AutoStartManager.h"

// ═══════════════════════════════════════════════════════════════════════════
// AutoStartManager — 开机自启注册表操作的安全封装
// ═══════════════════════════════════════════════════════════════════════════

AutoStartManager::SavedRegValue AutoStartManager::savePrevious(QSettings &reg, const QString &key)
{
    SavedRegValue state;
    state.existed = reg.contains(key);
    if (state.existed) {
        state.value = reg.value(key).toString();
    }
    return state;
}

bool AutoStartManager::applyAutoStart(QSettings &reg, const QString &key,
                                       bool enabled, const QString &exePath)
{
    if (enabled) {
        reg.setValue(key, QStringLiteral("\"%1\"").arg(exePath));
    } else {
        reg.remove(key);
    }
    reg.sync();
    return reg.status() == QSettings::NoError;
}

bool AutoStartManager::verifyAutoStart(QSettings &reg, const QString &key,
                                        bool enabled, const QString &exePath)
{
    if (enabled) {
        const QString expected = QStringLiteral("\"%1\"").arg(exePath);
        return reg.contains(key) && reg.value(key).toString() == expected;
    }
    return !reg.contains(key);
}

void AutoStartManager::restorePrevious(QSettings &reg, const QString &key,
                                        const SavedRegValue &saved)
{
    if (saved.existed) {
        reg.setValue(key, saved.value);
    } else {
        reg.remove(key);
    }
    reg.sync();
}

bool AutoStartManager::verifyRestore(QSettings &reg, const QString &key,
                                      const SavedRegValue &saved)
{
    if (saved.existed) {
        return reg.contains(key) && reg.value(key).toString() == saved.value;
    }
    return !reg.contains(key);
}
