#pragma once

#include <QSettings>
#include <QString>

/**
 * @file AutoStartManager.h
 * @brief 开机自启注册表操作的安全封装。
 *
 * 提供保存原值 -> 应用新值 -> 验证 -> 恢复 -> 验证恢复 的完整流程，
 * 可同时用于生产环境（NativeFormat / 真实注册表）和单元测试（IniFormat / 文件）。
 */

namespace AutoStartManager {

/// 保存注册表原值及是否存在。
struct SavedRegValue {
    bool existed = false;
    QString value;
};

/// 在修改注册表前保存当前状态。
SavedRegValue savePrevious(QSettings &reg, const QString &key);

/// 应用自启设置（写入或删除），sync 后检查 QSettings::status。
/// 返回 reg.status() == QSettings::NoError。
/// enabled=true ：写入带引号的 EXE 绝对路径。
/// enabled=false：删除对应 Run 项。
bool applyAutoStart(QSettings &reg, const QString &key,
                    bool enabled, const QString &exePath);

/// 精确验证注册表写入值或删除结果。
/// enabled=true ：检查 key 存在且值等于 "\"exePath\""
/// enabled=false：检查 key 不存在
bool verifyAutoStart(QSettings &reg, const QString &key,
                     bool enabled, const QString &exePath);

/// 从 SavedRegValue 恢复注册表原值。
void restorePrevious(QSettings &reg, const QString &key,
                     const SavedRegValue &saved);

/// 验证恢复操作是否成功。
/// saved.existed==true  → key 存在且值等于 saved.value
/// saved.existed==false → key 不存在
bool verifyRestore(QSettings &reg, const QString &key,
                   const SavedRegValue &saved);

} // namespace AutoStartManager
