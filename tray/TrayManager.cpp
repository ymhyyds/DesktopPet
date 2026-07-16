#include "TrayManager.h"

#include <QSystemTrayIcon>
#include <QMenu>
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QStyle>
#include <QIcon>
#include <QSignalBlocker>
#include <QDebug>

#include "../core/AutoStartManager.h"

TrayManager::TrayManager(const QStringList &availableCharacters,
                          const QString &currentCharacter,
                          QObject *parent)
    : QObject(parent)
{
    if (!QSystemTrayIcon::isSystemTrayAvailable()) {
        qWarning() << "[TrayManager] 当前系统不支持托盘图标";
    }

    m_trayIcon = std::make_unique<QSystemTrayIcon>();

    // 优先从 QRC 加载角色专属图标，不存在则退而使用 Qt 系统标准图标。
    // 将自定义图标文件放入 resources/icons/tray.png 后即可生效。
    QIcon icon = QIcon(QStringLiteral(":/icons/tray.png"));
    if (icon.isNull()) {
        icon = QApplication::style()->standardIcon(QStyle::SP_ComputerIcon);
    }
    m_trayIcon->setIcon(icon);
    m_trayIcon->setToolTip(QStringLiteral("桌面宠物"));

    buildMenu(availableCharacters, currentCharacter);
    m_trayIcon->setContextMenu(m_menu);

    // 双击托盘图标也能快速打开聊天窗口，作为双击桌宠本体的备用入口
    connect(m_trayIcon.get(), &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::DoubleClick) {
            emit openChatRequested();
        }
    });
}

TrayManager::~TrayManager() = default;

void TrayManager::buildMenu(const QStringList &availableCharacters, const QString &currentCharacter)
{
    m_menu = new QMenu();

    // --- 切换角色子菜单：每个已扫描到的角色一个可勾选项 ---
    m_characterSubMenu = m_menu->addMenu(QStringLiteral("切换角色"));
    m_characterActionGroup = new QActionGroup(this);
    m_characterActionGroup->setExclusive(true);

    for (const QString &name : availableCharacters) {
        QAction *action = m_characterSubMenu->addAction(name);
        action->setCheckable(true);
        action->setChecked(name == currentCharacter);
        m_characterActionGroup->addAction(action);

        connect(action, &QAction::triggered, this, [this, name]() {
            emit switchCharacterRequested(name);
        });
    }

    // --- 刷新角色列表：手动重新扫描 characters/ ---
    QAction *refreshAction = m_menu->addAction(QStringLiteral("刷新角色"));
    connect(refreshAction, &QAction::triggered, this, &TrayManager::refreshCharactersRequested);

    QAction *screenshotAction2 = m_menu->addAction(QStringLiteral("截图"));
    connect(screenshotAction2, &QAction::triggered, this, &TrayManager::screenshotRequested);

    QAction *paramAction = m_menu->addAction(QStringLiteral("模型参数"));
    connect(paramAction, &QAction::triggered, this, &TrayManager::parameterWindowRequested);

    m_menu->addSeparator();

    QAction *settingsAction = m_menu->addAction(QStringLiteral("设置"));
    connect(settingsAction, &QAction::triggered, this, &TrayManager::settingsRequested);

    // --- 开机自启 ---
    m_autoStartAction = m_menu->addAction(QStringLiteral("开机自启"));
    m_autoStartAction->setCheckable(true);

#ifdef Q_OS_WIN
    // 精确比较 Startup 文件夹快捷方式与当前 EXE 路径（旧路径残留时不勾选）
    const QString autoExePath = QApplication::applicationFilePath().replace('/', '\\');
    const QString autoLinkPath = AutoStartManager::shortcutPath();
    const bool autoStartMatches = !autoLinkPath.isEmpty()
        && AutoStartManager::verifyShortcut(autoLinkPath, autoExePath);
    m_autoStartAction->setChecked(autoStartMatches);
#endif

    connect(m_autoStartAction, &QAction::toggled, this, &TrayManager::autoStartToggled);

    m_menu->addSeparator();

    QAction *quitAction = m_menu->addAction(QStringLiteral("退出"));
    connect(quitAction, &QAction::triggered, this, &TrayManager::quitRequested);
}

void TrayManager::show()
{
    if (m_trayIcon) {
        m_trayIcon->show();
    }
}

void TrayManager::setActiveCharacter(const QString &characterName)
{
    for (QAction *action : m_characterActionGroup->actions()) {
        action->setChecked(action->text() == characterName);
    }
}

void TrayManager::setAutoStartChecked(bool checked)
{
    if (!m_autoStartAction) return;
    QSignalBlocker blocker(m_autoStartAction);
    m_autoStartAction->setChecked(checked);
}

void TrayManager::rebuildCharacterList(const QStringList &availableCharacters,
                                        const QString &currentCharacter)
{
    // 清空旧的角色菜单项
    if (m_characterActionGroup) {
        for (QAction *action : m_characterActionGroup->actions()) {
            m_characterSubMenu->removeAction(action);
            delete action;
        }
    }

    // 重建
    for (const QString &name : availableCharacters) {
        QAction *action = m_characterSubMenu->addAction(name);
        action->setCheckable(true);
        action->setChecked(name == currentCharacter);
        m_characterActionGroup->addAction(action);

        connect(action, &QAction::triggered, this, [this, name]() {
            emit switchCharacterRequested(name);
        });
    }
}
