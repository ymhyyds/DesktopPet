#pragma once

#include <QObject>
#include <QStringList>
#include <memory>

class QSystemTrayIcon;
class QMenu;
class QAction;
class QActionGroup;

/**
 * @brief TrayManager 封装系统托盘图标和右键菜单。
 *
 * 菜单项：切换角色（子菜单） / 刷新角色 / 截图 / 模型参数 / 设置 / 开机自启 / 退出。
 * 自己不执行任何业务逻辑，全部通过信号通知 App 去做实际的事情。
 */
class TrayManager : public QObject
{
    Q_OBJECT

public:
    /**
     * @param availableCharacters 当前已扫描到的所有角色名
     * @param currentCharacter 当前激活的角色名
     */
    explicit TrayManager(const QStringList &availableCharacters,
                          const QString &currentCharacter,
                          QObject *parent = nullptr);
    ~TrayManager() override;

    void show();

    /// 角色切换成功后，App 调用此方法同步菜单。
    void setActiveCharacter(const QString &characterName);

    /// 重建角色切换子菜单（自动扫描后发现新角色时调用）。
    void rebuildCharacterList(const QStringList &availableCharacters,
                              const QString &currentCharacter);

signals:
    void openChatRequested();
    void switchCharacterRequested(const QString &characterName);
    /// 手动刷新角色列表
    void refreshCharactersRequested();
    void screenshotRequested();
    void parameterWindowRequested();
    void settingsRequested();
    void quitRequested();
    void autoStartToggled(bool enabled);

private:
    void buildMenu(const QStringList &availableCharacters, const QString &currentCharacter);

private:
    std::unique_ptr<QSystemTrayIcon> m_trayIcon;
    QMenu *m_menu = nullptr;
    QMenu *m_characterSubMenu = nullptr;
    QActionGroup *m_characterActionGroup = nullptr;
    QAction *m_autoStartAction = nullptr;
};
