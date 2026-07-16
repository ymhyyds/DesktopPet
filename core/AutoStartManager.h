#pragma once

#include <QString>

/**
 * @file AutoStartManager.h
 * @brief 开机自启 Startup 文件夹快捷方式操作的安全封装。
 *
 * 使用 SHGetKnownFolderPath(FOLDERID_Startup) 获取启动文件夹路径，
 * IShellLinkW + IPersistFile 创建/读取 .lnk 快捷方式。
 *
 * 提供保存原快捷方式 -> 应用新值 -> 验证 -> 恢复 -> 验证恢复 的完整流程。
 * 所有函数需要调用者确保 COM 已初始化（CoInitializeEx）。
 */

namespace AutoStartManager {

/// 备份信息：原快捷方式是否存在及其备份文件路径。
struct ShortcutBackup {
    bool hadExisting = false;
    QString backupFilePath;   // 备份文件的绝对路径（为空表示无备份）
};

/// 获取 Windows Startup 文件夹路径（FOLDERID_Startup）。
/// 返回路径以 "\\" 结尾（若成功）。失败返回空字符串。
QString getStartupFolderPath();

/// 构建 DesktopPet.lnk 在 Startup 文件夹中的完整路径。
/// 快捷方式名固定为 "DesktopPet.lnk"。
QString shortcutPath();

/// 备份现有快捷方式到 shortcutPath + ".backup"。
/// 如果原文件不存在，hadExisting=false，backupFilePath 仍设置但文件不存在。
ShortcutBackup backupShortcut(const QString &shortcutPath);

/// 创建指向 exePath 的 .lnk 快捷方式。
/// @param shortcutPath  目标 .lnk 完整路径（如 startupDir\\DesktopPet.lnk）
/// @param exePath       EXE 绝对路径（使用 \\ 分隔符）
/// @param exeDir        EXE 所在目录（工作目录）
/// @return true 表示创建成功
bool createShortcut(const QString &shortcutPath,
                    const QString &exePath,
                    const QString &exeDir);

/// 删除指定 .lnk 快捷方式。
/// 文件不存在时视为成功（idempotent）。
bool removeShortcut(const QString &shortcutPath);

/// 验证 .lnk 是否存在且目标路径精确匹配 exePath（大小写不敏感比较）。
bool verifyShortcut(const QString &shortcutPath, const QString &exePath);

/// 从备份恢复快捷方式。
/// 若 backup.hadExisting==true，拷贝备份回 shortcutPath；
/// 若 backup.hadExisting==false，删除 shortcutPath。
bool restoreBackup(const QString &shortcutPath, const ShortcutBackup &backup);

/// 验证恢复结果：hadExisting==true → 文件存在且内容可读；
/// hadExisting==false → 文件不存在。
bool verifyRestore(const QString &shortcutPath, const ShortcutBackup &backup);

} // namespace AutoStartManager
