#include "core/App.h"

#include <QApplication>
#include <QDir>
#include <QDebug>

/**
 * 程序入口。
 *
 * 这里只做最基础的事情：
 *  1. 创建 QApplication
 *  2. 设置工作目录为可执行文件所在目录（保证相对路径 characters/、database/ 始终可用，
 *     不管用户是从哪个目录双击启动程序）
 *  3. 创建 App 并初始化
 *  4. 进入事件循环
 *
 * 所有真正的业务逻辑都在 App::initialize() 里完成，main.cpp 保持"瘦"。
 */
int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    QApplication::setApplicationName(QStringLiteral("DesktopPet"));
    QApplication::setApplicationVersion(QStringLiteral("1.0.0"));
    QApplication::setOrganizationName(QStringLiteral("DesktopPetProject"));

    // 关闭最后一个窗口（聊天窗口被关闭）时不要退出程序——
    // 桌宠是靠托盘图标控制生命周期的，不是靠窗口数量。
    QApplication::setQuitOnLastWindowClosed(false);

    // 保证相对路径资源（characters/、database/）总能找到，
    // 无论用户从什么工作目录启动 exe。
    QDir::setCurrent(QApplication::applicationDirPath());

    App petApp;
    if (!petApp.initialize()) {
        qCritical() << "[main] 应用初始化失败，程序退出。";
        return -1;
    }

    return QApplication::exec();
}
