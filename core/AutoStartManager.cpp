#include "AutoStartManager.h"

#include <QDebug>
#include <QDir>

// Windows headers
#include <windows.h>
#include <shlobj.h>      // SHGetKnownFolderPath, FOLDERID_Startup
#include <shlwapi.h>     // PathFileExistsW
#include <objbase.h>     // CoCreateInstance
// CLSID_ShellLink, IID_IShellLinkW, IID_IPersistFile from shell32.lib (default link)
#include <shlguid.h>

// ═══════════════════════════════════════════════════════════════════════════
// AutoStartManager — 开机自启 Startup 文件夹快捷方式操作的安全封装
// ═══════════════════════════════════════════════════════════════════════════

namespace AutoStartManager {

// ── 辅助：QString → 以 null 结尾的 wchar_t 字符串 ──
// toStdWString() 保证正确的 UTF-16 编码和 null 终止。
// 注意：返回的 std::wstring 是临时值，仅在完整表达式内有效。
// 使用方式：qw(str) 作为 Win32 API 的 LPCWSTR 参数。
static inline std::wstring qw(const QString &s) { return s.toStdWString(); }

// 辅助：规范化 Windows 路径，统一分隔符为后斜杠
static inline QString normalizePath(const QString &path) {
    return QDir::toNativeSeparators(QDir::cleanPath(path));
}

// ---------------------------------------------------------------------------
// 获取 Startup 文件夹路径
// ---------------------------------------------------------------------------
QString getStartupFolderPath()
{
    PWSTR path = nullptr;
    HRESULT hr = SHGetKnownFolderPath(FOLDERID_Startup, 0, nullptr, &path);
    if (FAILED(hr)) {
        qWarning() << "[AutoStartManager] SHGetKnownFolderPath failed:" << hr;
        if (path) CoTaskMemFree(path);
        return {};
    }

    QString result = QString::fromWCharArray(path);
    CoTaskMemFree(path);

    // 确保以反斜杠结尾
    if (!result.endsWith(QLatin1Char('\\'))) {
        result.append(QLatin1Char('\\'));
    }
    return result;
}

// ---------------------------------------------------------------------------
// 构建快捷方式完整路径
// ---------------------------------------------------------------------------
QString shortcutPath()
{
    const QString startup = getStartupFolderPath();
    if (startup.isEmpty()) return {};
    return startup + QStringLiteral("DesktopPet.lnk");
}

// ---------------------------------------------------------------------------
// 备份现有快捷方式
// ---------------------------------------------------------------------------
ShortcutBackup backupShortcut(const QString &shortcutPath)
{
    ShortcutBackup backup;
    backup.backupFilePath = shortcutPath + QStringLiteral(".backup");

    if (PathFileExistsW(qw(shortcutPath).c_str())) {
        backup.hadExisting = true;
        if (CopyFileW(qw(shortcutPath).c_str(),
                       qw(backup.backupFilePath).c_str(),
                       FALSE)) {
            qInfo() << "[AutoStartManager] 快捷方式已备份到" << backup.backupFilePath;
        } else {
            qWarning() << "[AutoStartManager] 备份快捷方式失败，错误:" << GetLastError();
        }
    } else {
        backup.hadExisting = false;
        qInfo() << "[AutoStartManager] 原快捷方式不存在，无需备份";
    }

    return backup;
}

// ---------------------------------------------------------------------------
// 创建 .lnk 快捷方式
// ---------------------------------------------------------------------------
bool createShortcut(const QString &shortcutPath,
                    const QString &exePath,
                    const QString &exeDir)
{
    IShellLinkW *psl = nullptr;
    HRESULT hr = CoCreateInstance(
        CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
        IID_IShellLinkW, reinterpret_cast<void**>(&psl));

    if (FAILED(hr) || !psl) {
        qWarning() << "[AutoStartManager] CoCreateInstance(ShellLink) 失败:" << hr;
        return false;
    }

    // 设置目标路径
    hr = psl->SetPath(qw(exePath).c_str());
    if (FAILED(hr)) {
        qWarning() << "[AutoStartManager] SetPath 失败:" << hr;
        psl->Release();
        return false;
    }

    // 设置工作目录
    if (!exeDir.isEmpty()) {
        hr = psl->SetWorkingDirectory(qw(exeDir).c_str());
        if (FAILED(hr)) {
            qWarning() << "[AutoStartManager] SetWorkingDirectory 失败:" << hr;
            psl->Release();
            return false;
        }
    }

    // 设置描述
    psl->SetDescription(L"DesktopPet");

    // 保存到文件
    IPersistFile *ppf = nullptr;
    hr = psl->QueryInterface(IID_IPersistFile, reinterpret_cast<void**>(&ppf));
    if (FAILED(hr) || !ppf) {
        qWarning() << "[AutoStartManager] QueryInterface(IPersistFile) 失败:" << hr;
        psl->Release();
        return false;
    }

    hr = ppf->Save(qw(shortcutPath).c_str(), TRUE);
    ppf->Release();
    psl->Release();

    if (FAILED(hr)) {
        qWarning() << "[AutoStartManager] IPersistFile::Save 失败:" << hr;
        return false;
    }

    qInfo() << "[AutoStartManager] 快捷方式已创建:" << shortcutPath;
    return true;
}

// ---------------------------------------------------------------------------
// 删除快捷方式
// ---------------------------------------------------------------------------
bool removeShortcut(const QString &shortcutPath)
{
    if (!PathFileExistsW(qw(shortcutPath).c_str())) {
        // 文件不存在视为成功（idempotent）
        return true;
    }

    if (DeleteFileW(qw(shortcutPath).c_str())) {
        qInfo() << "[AutoStartManager] 快捷方式已删除:" << shortcutPath;
        return true;
    }

    const DWORD err = GetLastError();
    qWarning() << "[AutoStartManager] DeleteFileW 失败，错误:" << err;
    return false;
}

// ---------------------------------------------------------------------------
// 验证快捷方式
// ---------------------------------------------------------------------------
bool verifyShortcut(const QString &shortcutPath, const QString &exePath)
{
    // 1. 检查文件存在
    if (!PathFileExistsW(qw(shortcutPath).c_str())) {
        return false;
    }

    // 2. 读取快捷方式目标
    IShellLinkW *psl = nullptr;
    HRESULT hr = CoCreateInstance(
        CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
        IID_IShellLinkW, reinterpret_cast<void**>(&psl));

    if (FAILED(hr) || !psl) {
        qWarning() << "[AutoStartManager] verifyShortcut CoCreateInstance 失败:" << hr;
        return false;
    }

    IPersistFile *ppf = nullptr;
    hr = psl->QueryInterface(IID_IPersistFile, reinterpret_cast<void**>(&ppf));
    if (FAILED(hr) || !ppf) {
        psl->Release();
        return false;
    }

    hr = ppf->Load(qw(shortcutPath).c_str(), STGM_READ);
    ppf->Release();

    if (FAILED(hr)) {
        psl->Release();
        qWarning() << "[AutoStartManager] IPersistFile::Load 失败:" << hr;
        return false;
    }

    // 3. 获取目标路径
    WCHAR target[MAX_PATH];
    hr = psl->GetPath(target, MAX_PATH, nullptr, SLGP_UNCPRIORITY);
    psl->Release();

    if (FAILED(hr)) {
        qWarning() << "[AutoStartManager] GetPath 失败:" << hr;
        return false;
    }

    const QString actualExePath = QString::fromWCharArray(target);

    // 4. 大小写不敏感比较（Windows 路径不区分大小写）
    // 先规范化分隔符，避免 Qt 前斜杠与 Windows API 后斜杠导致误判
    const QString normActual = normalizePath(actualExePath);
    const QString normExpected = normalizePath(exePath);
    const bool matches = normActual.compare(normExpected, Qt::CaseInsensitive) == 0;
    if (!matches) {
        qDebug() << "[AutoStartManager] 快捷方式目标不匹配:"
                 << "期望=" << normExpected
                 << "实际=" << normActual;
    }
    return matches;
}

// ---------------------------------------------------------------------------
// 从备份恢复
// ---------------------------------------------------------------------------
bool restoreBackup(const QString &shortcutPath, const ShortcutBackup &backup)
{
    if (!backup.hadExisting) {
        // 原状态：快捷方式不存在 → 删除当前文件
        return removeShortcut(shortcutPath);
    }

    // 原状态：有快捷方式 → 从备份拷贝回来
    if (backup.backupFilePath.isEmpty()) {
        qWarning() << "[AutoStartManager] 恢复失败：备份路径为空";
        return false;
    }

    if (!PathFileExistsW(qw(backup.backupFilePath).c_str())) {
        qWarning() << "[AutoStartManager] 备份文件不存在，无法恢复:"
                    << backup.backupFilePath;
        return false;
    }

    if (CopyFileW(qw(backup.backupFilePath).c_str(),
                   qw(shortcutPath).c_str(),
                   FALSE)) {
        qInfo() << "[AutoStartManager] 已从备份恢复:" << shortcutPath;
        // 恢复后删除备份文件
        DeleteFileW(qw(backup.backupFilePath).c_str());
        return true;
    }

    qWarning() << "[AutoStartManager] CopyFileW 恢复失败，错误:" << GetLastError();
    return false;
}

// ---------------------------------------------------------------------------
// 验证恢复结果
// ---------------------------------------------------------------------------
bool verifyRestore(const QString &shortcutPath, const ShortcutBackup &backup)
{
    if (backup.hadExisting) {
        return PathFileExistsW(qw(shortcutPath).c_str());
    }
    return !PathFileExistsW(qw(shortcutPath).c_str());
}

} // namespace AutoStartManager
