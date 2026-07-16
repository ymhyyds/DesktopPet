#pragma once

#include <QObject>
#include <QString>

class QLocalServer;
class QLocalSocket;

/**
 * @brief 基于 Windows 命名 Mutex + QLocalServer 的当前用户单实例保护。
 *
 * 所有权判断和进程隔离通过 CreateMutexW 原子完成（而非 QLocalServer::listen），
 * QLocalServer 仅用于向首实例发送激活请求（activationRequested 信号）。
 *
 * 使用方式（在 main.cpp 中，App 初始化前）：
 * @code
 * SingleInstanceManager guard;
 * if (!guard.tryAcquire()) {
 *     return 0; // 第二实例，正常退出
 * }
 * // QObject::connect(&guard, &SingleInstanceManager::activationRequested, ...);
 * // ... 继续初始化 App
 * @endcode
 *
 * 设计要点：
 *   - Windows 命名 Mutex（Local\DesktopPet-{safe_user}）原子决定首实例。
 *   - 首实例的 Mutex HANDLE 保持到进程退出，进程崩溃时 OS 自动释放。
 *   - QLocalServer（UserAccessOption + 命名 pipe）仅用于第二实例的激活通知。
 *   - 第二实例即使暂时连不上通知服务，也必须因 Mutex 已存在而退出；
 *     通知连接可短时间重试。
 *   - 服务名按应用和当前用户隔离。
 */
class SingleInstanceManager : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief 构造单实例管理器。
     * @param serverName  基础名称。为空时自动生成 "DesktopPet-{safe_user}"。
     *                    测试时传入唯一名称实现隔离。
     * @param parent      QObject 父对象
     */
    explicit SingleInstanceManager(const QString &serverName = QString(),
                                   QObject *parent = nullptr);
    ~SingleInstanceManager() override;

    /**
     * @brief 尝试获取单实例锁。
     * @return true  → 首实例，应继续执行；
     *         false → 已有实例在运行，调用方应立即退出。
     *
     * Windows 实现：
     *   1. CreateMutexW(Local\name) → 若 ERROR_ALREADY_EXISTS 是第二实例。
     *   2. 第二实例：短时间重试连接通知服务，发送 "raise"，然后返回 false。
     *   3. 首实例：保持 Mutex HANDLE，启动 QLocalServer 监听，返回 true。
     */
    bool tryAcquire();

    /// 释放 Mutex（用于测试中模拟首实例退出）
    void releaseMutex();

signals:
    /**
     * @brief 第二实例尝试启动时发出。
     * 首实例可在收到此信号后激活/前置窗口。
     */
    void activationRequested();

private:
    /// 第二实例：连接通知服务并发送 "raise"（短时重试）
    void notifyFirstInstance();
    /// 首实例：创建并启动 QLocalServer
    bool startNotificationServer();
    /// 处理服务端新连接
    void handleNewConnection();
    /// 读取客户端发送的数据
    void onReadyRead(QLocalSocket *socket);
    /// 生成默认名称
    static QString defaultServerName();
    /// Windows Mutex 名称（带 Local\ 前缀）
    QString mutexName() const;
    /// QLocalServer pipe 名称
    QString pipeName() const;

#ifdef Q_OS_WIN
    void *m_mutexHandle = nullptr;  // HANDLE — 保持到进程退出
#endif
    QLocalServer *m_server = nullptr;
    QString m_baseName;  // 用户传入或自动生成的基础名称
};
