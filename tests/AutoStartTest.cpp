#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QCoreApplication>

#include "../core/AutoStartManager.h"

// Windows headers for direct IShellLinkW verification in tests
#include <windows.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <objbase.h>

/**
 * @brief AutoStartTest — 开机自启 Startup 文件夹快捷方式操作单元测试。
 *
 * 使用 QTemporaryDir 作为模拟 Startup 目录，不污染真实 shell:startup。
 * 所有测试需要 COM 支持（通过 initTestCase / cleanupTestCase 管理）。
 */
class AutoStartTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    // 每个测试前后隔离：仅清理临时 Startup 目录中的 DesktopPet.lnk 残留
    void init();
    void cleanup();

    // 基本创建/删除/验证
    void testCreateShortcut();
    void testRemoveShortcut();
    void testVerifyWithWrongPath();
    void testVerifyAbsence();

    // 备份与恢复
    void testBackupAndRestore();
    void testBackupWhenNoExisting();
    void testRestoreWhenNoBackup();

    // 路径特殊性
    void testChinesePath();
    void testSpaceInPath();
    void testTrailingBackslashInPath();

    // 边界
    void testRemoveNonExistentIsIdempotent();
    void testCreateOverwriteExisting();
    void testFullRollbackCycle();

private:
    QTemporaryDir *m_tempDir = nullptr;
    QString m_startupDir;
    QString m_shortcutPath;

    // 测试用 EXE 路径
    QString m_exePath;
    QString m_exeDir;
    QString m_otherExePath;

    // 创建临时文件模拟的 "EXE"，供 SetPath 使用
    QString m_fakeExeFilePath;

    /// 直接通过 IShellLinkW 读取 .lnk 目标路径（用于测试 verifyShortcut 的正确性）
    QString readLinkTargetDirect(const QString &linkPath) const;

    /// 辅助：QString → null 终止的 wchar_t 字符串
    static inline std::wstring qw(const QString &s) { return s.toStdWString(); }

    /// 辅助：规范化 Windows 路径分隔符（统一为后斜杠），用于比较
    static inline QString norm(const QString &p) {
        return QDir::toNativeSeparators(QDir::cleanPath(p));
    }
};

// ═══════════════════════════════════════════════════════════════════════════
// 初始化 / 清理
// ═══════════════════════════════════════════════════════════════════════════

void AutoStartTest::initTestCase()
{
    // 初始化 COM（单元测试可能没有自动初始化）
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
        QFAIL("CoInitializeEx 失败");
    }

    // 临时目录作为模拟 Startup 目录
    m_tempDir = new QTemporaryDir();
    QVERIFY(m_tempDir->isValid());
    m_startupDir = m_tempDir->filePath(QStringLiteral("Startup"));
    QDir().mkpath(m_startupDir);
    m_shortcutPath = m_startupDir + QStringLiteral("\\DesktopPet.lnk");

    // 用临时目录下的 fake.exe 作为目标路径
    m_fakeExeFilePath = m_tempDir->filePath(QStringLiteral("DesktopPet.exe"));
    {
        QFile fakeExe(m_fakeExeFilePath);
        fakeExe.open(QIODevice::WriteOnly);
        fakeExe.write("FAKE_EXE");
        fakeExe.close();
    }

    m_exePath = m_fakeExeFilePath;
    m_exeDir = m_tempDir->path();
    m_otherExePath = m_tempDir->filePath(QStringLiteral("OtherPet.exe"));
    {
        QFile otherExe(m_otherExePath);
        otherExe.open(QIODevice::WriteOnly);
        otherExe.write("OTHER");
        otherExe.close();
    }
}

void AutoStartTest::cleanupTestCase()
{
    // cleanup 已在每个测试后执行，此处仅做最终保险清理
    if (!m_shortcutPath.isEmpty()) {
        DeleteFileW(qw(m_shortcutPath).c_str());
        DeleteFileW(qw(m_shortcutPath + QStringLiteral(".backup")).c_str());
    }

    if (m_tempDir) {
        m_tempDir->remove();
        delete m_tempDir;
        m_tempDir = nullptr;
    }

    CoUninitialize();
}

// ═══════════════════════════════════════════════════════════════════════════
// 每个测试前后隔离
// ═══════════════════════════════════════════════════════════════════════════

void AutoStartTest::init()
{
    // initTestCase 已完成一次性设置，每个测试前无需额外操作
}

void AutoStartTest::cleanup()
{
    // 只清理临时 Startup 目录中的测试残留，绝不操作真实 shell:startup
    if (!m_shortcutPath.isEmpty()) {
        DeleteFileW(qw(m_shortcutPath).c_str());
        const QString backupPath = m_shortcutPath + QStringLiteral(".backup");
        DeleteFileW(qw(backupPath).c_str());
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// 工具函数：直接通过 IShellLinkW 读取目标路径
// ═══════════════════════════════════════════════════════════════════════════

QString AutoStartTest::readLinkTargetDirect(const QString &linkPath) const
{
    IShellLinkW *psl = nullptr;
    HRESULT hr = CoCreateInstance(
        CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
        IID_IShellLinkW, reinterpret_cast<void**>(&psl));
    if (FAILED(hr)) return {};

    IPersistFile *ppf = nullptr;
    hr = psl->QueryInterface(IID_IPersistFile, reinterpret_cast<void**>(&ppf));
    if (FAILED(hr)) { psl->Release(); return {}; }

    hr = ppf->Load(qw(linkPath).c_str(), STGM_READ);
    ppf->Release();
    if (FAILED(hr)) { psl->Release(); return {}; }

    WCHAR target[MAX_PATH];
    hr = psl->GetPath(target, MAX_PATH, nullptr, SLGP_UNCPRIORITY);
    psl->Release();

    if (FAILED(hr)) return {};
    return QString::fromWCharArray(target);
}

// ═══════════════════════════════════════════════════════════════════════════
// 基本创建/删除/验证
// ═══════════════════════════════════════════════════════════════════════════

void AutoStartTest::testCreateShortcut()
{
    QVERIFY(!PathFileExistsW(qw(m_shortcutPath).c_str()));

    bool ok = AutoStartManager::createShortcut(m_shortcutPath, m_exePath, m_exeDir);
    QVERIFY(ok);

    // 文件存在
    QVERIFY(PathFileExistsW(qw(m_shortcutPath).c_str()));

    // 用直接读取验证目标路径（规范化后比较，统一分隔符）
    const QString actualTarget = readLinkTargetDirect(m_shortcutPath);
    QCOMPARE(norm(actualTarget), norm(m_exePath));

    // 使用 verifyShortcut 验证
    QVERIFY(AutoStartManager::verifyShortcut(m_shortcutPath, m_exePath));

    // 清理
    QVERIFY(DeleteFileW(qw(m_shortcutPath).c_str()));
}

void AutoStartTest::testRemoveShortcut()
{
    // 先创建
    QVERIFY(AutoStartManager::createShortcut(m_shortcutPath, m_exePath, m_exeDir));
    QVERIFY(PathFileExistsW(qw(m_shortcutPath).c_str()));

    // 删除
    bool ok = AutoStartManager::removeShortcut(m_shortcutPath);
    QVERIFY(ok);

    // 验证已删除
    QVERIFY(!PathFileExistsW(qw(m_shortcutPath).c_str()));
    QVERIFY(!AutoStartManager::verifyShortcut(m_shortcutPath, m_exePath));
}

void AutoStartTest::testVerifyWithWrongPath()
{
    // 创建指向 exePath 的快捷方式
    QVERIFY(AutoStartManager::createShortcut(m_shortcutPath, m_exePath, m_exeDir));

    // 用 m_otherExePath 验证应失败
    QVERIFY(!AutoStartManager::verifyShortcut(m_shortcutPath, m_otherExePath));

    // 用正确路径验证应通过
    QVERIFY(AutoStartManager::verifyShortcut(m_shortcutPath, m_exePath));

    // 清理
    QVERIFY(AutoStartManager::removeShortcut(m_shortcutPath));
}

void AutoStartTest::testVerifyAbsence()
{
    // 文件不存在时 verify 应返回 false
    QVERIFY(!PathFileExistsW(qw(m_shortcutPath).c_str()));
    QVERIFY(!AutoStartManager::verifyShortcut(m_shortcutPath, m_exePath));

    // 创建后再验证
    QVERIFY(AutoStartManager::createShortcut(m_shortcutPath, m_exePath, m_exeDir));
    QVERIFY(AutoStartManager::verifyShortcut(m_shortcutPath, m_exePath));

    // 删除后再验证
    QVERIFY(AutoStartManager::removeShortcut(m_shortcutPath));
    QVERIFY(!AutoStartManager::verifyShortcut(m_shortcutPath, m_exePath));
}

// ═══════════════════════════════════════════════════════════════════════════
// 备份与恢复
// ═══════════════════════════════════════════════════════════════════════════

void AutoStartTest::testBackupAndRestore()
{
    // 创建快捷方式 A（指向 m_exePath）
    QVERIFY(AutoStartManager::createShortcut(m_shortcutPath, m_exePath, m_exeDir));
    QVERIFY(AutoStartManager::verifyShortcut(m_shortcutPath, m_exePath));

    // 备份
    AutoStartManager::ShortcutBackup backup =
        AutoStartManager::backupShortcut(m_shortcutPath);
    QVERIFY(backup.hadExisting);
    QVERIFY(!backup.backupFilePath.isEmpty());
    QVERIFY(PathFileExistsW(qw(backup.backupFilePath).c_str()));

    // 删除 A
    QVERIFY(AutoStartManager::removeShortcut(m_shortcutPath));
    QVERIFY(!AutoStartManager::verifyShortcut(m_shortcutPath, m_exePath));

    // 创建快捷方式 B（指向 m_otherExePath）
    QVERIFY(AutoStartManager::createShortcut(m_shortcutPath, m_otherExePath, m_exeDir));
    QVERIFY(AutoStartManager::verifyShortcut(m_shortcutPath, m_otherExePath));

    // 恢复 → 应变回 A
    bool restored = AutoStartManager::restoreBackup(m_shortcutPath, backup);
    QVERIFY(restored);
    QVERIFY(AutoStartManager::verifyShortcut(m_shortcutPath, m_exePath));
    QVERIFY(!AutoStartManager::verifyShortcut(m_shortcutPath, m_otherExePath));

    // 验证恢复结果
    QVERIFY(AutoStartManager::verifyRestore(m_shortcutPath, backup));

    // 备份文件应已被删除
    QVERIFY(!PathFileExistsW(qw(backup.backupFilePath).c_str()));

    // 清理
    QVERIFY(AutoStartManager::removeShortcut(m_shortcutPath));
}

void AutoStartTest::testBackupWhenNoExisting()
{
    // 确保不存在
    QVERIFY(!PathFileExistsW(qw(m_shortcutPath).c_str()));

    // 备份
    AutoStartManager::ShortcutBackup backup =
        AutoStartManager::backupShortcut(m_shortcutPath);
    QVERIFY(!backup.hadExisting);

    // 备份文件不应存在
    if (!backup.backupFilePath.isEmpty()) {
        QVERIFY(!PathFileExistsW(qw(backup.backupFilePath).c_str()));
    }
}

void AutoStartTest::testRestoreWhenNoBackup()
{
    // 备份不存在的情况
    AutoStartManager::ShortcutBackup backup;
    backup.hadExisting = true; // 声称原来有
    backup.backupFilePath = m_tempDir->filePath(QStringLiteral("nonexistent.backup"));

    // 尝试恢复（备份文件不存在 → 应失败）
    bool restored = AutoStartManager::restoreBackup(m_shortcutPath, backup);
    QVERIFY(!restored);
}

// ═══════════════════════════════════════════════════════════════════════════
// 路径特殊性
// ═══════════════════════════════════════════════════════════════════════════

void AutoStartTest::testChinesePath()
{
    // 在临时目录下创建包含中文路径的假的 "EXE"
    const QString chineseDir = m_tempDir->filePath(QStringLiteral("桌面宠物"));
    QDir().mkpath(chineseDir);
    const QString chineseExePath = chineseDir + QStringLiteral("\\DesktopPet.exe");
    {
        QFile fakeExe(chineseExePath);
        fakeExe.open(QIODevice::WriteOnly);
        fakeExe.write("FAKE");
        fakeExe.close();
    }

    // 创建快捷方式
    const QString chineseLinkPath = m_startupDir + QStringLiteral("\\DesktopPet_中文.lnk");
    bool ok = AutoStartManager::createShortcut(chineseLinkPath, chineseExePath, chineseDir);
    QVERIFY(ok);

    // 验证
    QVERIFY(AutoStartManager::verifyShortcut(chineseLinkPath, chineseExePath));

    // 直接读取验证
    const QString actualTarget = readLinkTargetDirect(chineseLinkPath);
    QCOMPARE(norm(actualTarget), norm(chineseExePath));

    // 删除
    QVERIFY(AutoStartManager::removeShortcut(chineseLinkPath));
    QVERIFY(!AutoStartManager::verifyShortcut(chineseLinkPath, chineseExePath));
}

void AutoStartTest::testSpaceInPath()
{
    // 在临时目录下创建包含空格的假的 "EXE"
    const QString spacedDir = m_tempDir->filePath(QStringLiteral("My Desktop Pet"));
    QDir().mkpath(spacedDir);
    const QString spacedExePath = spacedDir + QStringLiteral("\\DesktopPet.exe");
    {
        QFile fakeExe(spacedExePath);
        fakeExe.open(QIODevice::WriteOnly);
        fakeExe.write("FAKE");
        fakeExe.close();
    }

    // 创建快捷方式（路径带空格）
    const QString spacedLinkPath = m_startupDir + QStringLiteral("\\My Desktop Pet.lnk");
    bool ok = AutoStartManager::createShortcut(spacedLinkPath, spacedExePath, spacedDir);
    QVERIFY(ok);

    // 验证
    QVERIFY(AutoStartManager::verifyShortcut(spacedLinkPath, spacedExePath));

    // 直接读取验证
    const QString actualTarget = readLinkTargetDirect(spacedLinkPath);
    QCOMPARE(norm(actualTarget), norm(spacedExePath));

    // 删除
    QVERIFY(AutoStartManager::removeShortcut(spacedLinkPath));
    QVERIFY(!AutoStartManager::verifyShortcut(spacedLinkPath, spacedExePath));
}

void AutoStartTest::testTrailingBackslashInPath()
{
    // 验证路径末尾带反斜杠时仍然正常工作
    const QString exeDirWithSlash = m_exeDir + QStringLiteral("\\");
    QVERIFY(AutoStartManager::createShortcut(m_shortcutPath, m_exePath, exeDirWithSlash));
    QVERIFY(AutoStartManager::verifyShortcut(m_shortcutPath, m_exePath));
    QVERIFY(AutoStartManager::removeShortcut(m_shortcutPath));
}

// ═══════════════════════════════════════════════════════════════════════════
// 边界
// ═══════════════════════════════════════════════════════════════════════════

void AutoStartTest::testRemoveNonExistentIsIdempotent()
{
    // 文件不存在时 remove 应返回 true（幂等）
    QVERIFY(!PathFileExistsW(qw(m_shortcutPath).c_str()));
    QVERIFY(AutoStartManager::removeShortcut(m_shortcutPath));
}

void AutoStartTest::testCreateOverwriteExisting()
{
    // 创建指向 m_exePath 的快捷方式
    QVERIFY(AutoStartManager::createShortcut(m_shortcutPath, m_exePath, m_exeDir));
    QVERIFY(AutoStartManager::verifyShortcut(m_shortcutPath, m_exePath));

    // 覆盖创建指向 m_otherExePath 的快捷方式
    QVERIFY(AutoStartManager::createShortcut(m_shortcutPath, m_otherExePath, m_exeDir));

    // 验证已更新
    QVERIFY(AutoStartManager::verifyShortcut(m_shortcutPath, m_otherExePath));
    QVERIFY(!AutoStartManager::verifyShortcut(m_shortcutPath, m_exePath));

    // 直接读取验证
    const QString actualTarget = readLinkTargetDirect(m_shortcutPath);
    QCOMPARE(norm(actualTarget), norm(m_otherExePath));

    // 清理
    QVERIFY(AutoStartManager::removeShortcut(m_shortcutPath));
}

void AutoStartTest::testFullRollbackCycle()
{
    // 模拟完整的使用场景：
    // 1. 原状态：快捷方式指向 m_exePath
    QVERIFY(AutoStartManager::createShortcut(m_shortcutPath, m_exePath, m_exeDir));

    // 2. 备份
    AutoStartManager::ShortcutBackup backup =
        AutoStartManager::backupShortcut(m_shortcutPath);
    QVERIFY(backup.hadExisting);

    // 3. "启用"失败 → 恢复（模拟：创建一个仅备份后立即恢复的周期）
    //    这里我们模拟 createShortcut 成功但后面验证失败 → 恢复的场景

    // 3a. 创建指向另一个路径 → 模拟"切换"
    QVERIFY(AutoStartManager::createShortcut(m_shortcutPath, m_otherExePath, m_exeDir));
    QVERIFY(AutoStartManager::verifyShortcut(m_shortcutPath, m_otherExePath));

    // 3b. "验证失败" → 恢复
    bool restored = AutoStartManager::restoreBackup(m_shortcutPath, backup);
    QVERIFY(restored);

    // 3c. 验证恢复
    QVERIFY(AutoStartManager::verifyRestore(m_shortcutPath, backup));
    QVERIFY(AutoStartManager::verifyShortcut(m_shortcutPath, m_exePath));
    QVERIFY(!AutoStartManager::verifyShortcut(m_shortcutPath, m_otherExePath));

    // 4. 模拟"禁用"场景
    // 4a. 备份
    AutoStartManager::ShortcutBackup backup2 =
        AutoStartManager::backupShortcut(m_shortcutPath);
    QVERIFY(backup2.hadExisting);

    // 4b. 删除（模拟禁用）
    QVERIFY(AutoStartManager::removeShortcut(m_shortcutPath));

    // 4c. "验证失败"（模拟：文件还在但我们要验证它不存在，但它确实不存在，
    //     所以这个不会触发失败。我们模拟 "验证说文件还在" 的情况）
    //     这里直接验证文件已不存在
    QVERIFY(!AutoStartManager::verifyShortcut(m_shortcutPath, m_exePath));

    // 4d. 恢复（模拟禁用操作失败后的回滚）
    bool restored2 = AutoStartManager::restoreBackup(m_shortcutPath, backup2);
    QVERIFY(restored2);
    QVERIFY(AutoStartManager::verifyShortcut(m_shortcutPath, m_exePath));

    // 最终状态：恢复到 m_exePath
    QVERIFY(AutoStartManager::verifyRestore(m_shortcutPath, backup2));

    // 清理
    QVERIFY(AutoStartManager::removeShortcut(m_shortcutPath));

    // 验证原备份文件的备份文件已被删除
    const QString backupFilePath = m_shortcutPath + QStringLiteral(".backup");
    QVERIFY(!PathFileExistsW(qw(backupFilePath).c_str()));
}

// ═══════════════════════════════════════════════════════════════════════════
// 主函数
// ═══════════════════════════════════════════════════════════════════════════

QTEST_GUILESS_MAIN(AutoStartTest)
#include "AutoStartTest.moc"
