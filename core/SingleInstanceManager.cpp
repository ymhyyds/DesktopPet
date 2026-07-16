#include "SingleInstanceManager.h"

#include <QLocalServer>
#include <QLocalSocket>
#include <QEventLoop>
#include <QTimer>
#include <QThread>
#include <QDebug>
#include <QDir>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

// ---------------------------------------------------------------------------
// 构造 / 析构
// ---------------------------------------------------------------------------

SingleInstanceManager::SingleInstanceManager(const QString &serverName,
                                             QObject *parent)
    : QObject(parent)
{
    m_baseName = serverName.isEmpty() ? defaultServerName() : serverName;
}

SingleInstanceManager::~SingleInstanceManager()
{
    if (m_server) {
        m_server->close();
        m_server = nullptr;
    }

#ifdef Q_OS_WIN
    if (m_mutexHandle) {
        CloseHandle(m_mutexHandle);
        m_mutexHandle = nullptr;
    }
#endif
}

// ---------------------------------------------------------------------------
// 公开接口
// ---------------------------------------------------------------------------

bool SingleInstanceManager::tryAcquire()
{
#ifdef Q_OS_WIN
    const std::wstring wname = mutexName().toStdWString();

    // CreateMutexW → 立即保存 GetLastError，不穿插任何调用
    HANDLE hMutex = CreateMutexW(nullptr, FALSE, wname.c_str());
    const DWORD createError = GetLastError();

    if (!hMutex) {
        // NULL 句柄 = 系统级失败（权限不足等）
        qCritical() << "[SingleInstance] CreateMutexW 失败，错误代码："
                    << createError << "name=" << m_baseName;
        return false;
    }

    if (createError == ERROR_ALREADY_EXISTS) {
        // ── 第二实例：Mutex 已被首实例持有 ──
        CloseHandle(hMutex);

        qInfo() << "[SingleInstance] Mutex 已存在，当前为第二实例。"
                << "mutex=" << mutexName() << "pipe=" << pipeName();

        // 尝试通知首实例（最佳努力，连不上不影响退出）
        notifyFirstInstance();
        return false;
    }

    // ── 首实例：Mutex 获取成功，保持 HANDLE 到进程退出 ──
    m_mutexHandle = hMutex;
    qInfo() << "[SingleInstance] 首实例已获取 Mutex。"
            << "mutex=" << mutexName() << "pipe=" << pipeName();

    if (!startNotificationServer()) {
        qWarning() << "[SingleInstance] 通知服务启动失败（不影响运行）";
    }

    return true;
#else
    // 非 Windows 回退：仍用 QLocalServer::listen 判定
    QLocalServer::removeServer(pipeName());
    m_server = new QLocalServer(this);
    if (!m_server->listen(pipeName())) {
        // listen 失败 → 可能是第二实例
        QLocalSocket socket;
        socket.connectToServer(pipeName());
        if (socket.waitForConnected(500)) {
            socket.write("raise");
            QEventLoop loop;
            QTimer::singleShot(200, &loop, &QEventLoop::quit);
            loop.exec();
            socket.disconnectFromServer();
        }
        return false;
    }
    connect(m_server, &QLocalServer::newConnection,
            this, &SingleInstanceManager::handleNewConnection);
    return true;
#endif
}

void SingleInstanceManager::releaseMutex()
{
#ifdef Q_OS_WIN
    if (m_mutexHandle) {
        CloseHandle(m_mutexHandle);
        m_mutexHandle = nullptr;
    }
#endif
    if (m_server) {
        m_server->close();
        m_server = nullptr;
    }
}

// ---------------------------------------------------------------------------
// 第二实例：通知首实例
// ---------------------------------------------------------------------------

void SingleInstanceManager::notifyFirstInstance()
{
    // 短时间重试连接通知服务（首实例可能尚未完成 QLocalServer 创建）
    const int maxRetries = 20;  // 20 × 100ms = 2s 上限
    for (int i = 0; i < maxRetries; ++i) {
        QLocalSocket socket;
        socket.connectToServer(pipeName());

        if (socket.waitForConnected(100)) {
            qInfo() << "[SingleInstance] 连接通知服务成功，发送 raise...";
            socket.write("raise");
            {
                QEventLoop loop;
                QTimer::singleShot(200, &loop, &QEventLoop::quit);
                loop.exec();
            }
            socket.disconnectFromServer();
            return;
        }

        // 等待 100ms 后重试
        QThread::msleep(100);
    }

    qWarning() << "[SingleInstance] 无法连接通知服务（首实例可能仍在启动）";
}

// ---------------------------------------------------------------------------
// 首实例：启动通知服务
// ---------------------------------------------------------------------------

bool SingleInstanceManager::startNotificationServer()
{
    m_server = new QLocalServer(this);

    // UserAccessOption：pipe 安全描述符仅允许当前用户访问
    m_server->setSocketOptions(QLocalServer::UserAccessOption);

    if (!m_server->listen(pipeName())) {
        qWarning() << "[SingleInstance] QLocalServer::listen 失败："
                    << m_server->errorString();
        delete m_server;
        m_server = nullptr;
        return false;
    }

    connect(m_server, &QLocalServer::newConnection,
            this, &SingleInstanceManager::handleNewConnection);

    qInfo() << "[SingleInstance] 通知服务已启动。pipe=" << pipeName();
    return true;
}

void SingleInstanceManager::handleNewConnection()
{
    QLocalSocket *clientSocket = m_server->nextPendingConnection();
    if (!clientSocket) return;

    connect(clientSocket, &QLocalSocket::readyRead,
            this, [this, clientSocket]() {
                onReadyRead(clientSocket);
            });

    connect(clientSocket, &QLocalSocket::disconnected,
            clientSocket, &QObject::deleteLater);
}

void SingleInstanceManager::onReadyRead(QLocalSocket *socket)
{
    const QByteArray data = socket->readAll();
    qInfo() << "[SingleInstance] 收到激活消息：" << data;

    if (data.contains("raise")) {
        emit activationRequested();
    }
}

// ---------------------------------------------------------------------------
// 名称生成
// ---------------------------------------------------------------------------

QString SingleInstanceManager::defaultServerName()
{
    // 取当前用户名，过滤掉可能引起问题的字符
    QString user = qEnvironmentVariable("USERNAME");
    QString safe;
    safe.reserve(user.size());
    for (const QChar &c : user) {
        if (c.isLetterOrNumber() || c == QLatin1Char('_') || c == QLatin1Char('-'))
            safe += c;
    }
    if (safe.isEmpty())
        safe = QStringLiteral("DefaultUser");

    return QStringLiteral("DesktopPet-") + safe;
}

QString SingleInstanceManager::mutexName() const
{
    // Local\ 命名空间 = 当前会话隔离（Terminal Services 感知）
    // 不同 Windows 用户自动在不同会话，互不干扰
    return QStringLiteral("Local\\") + m_baseName;
}

QString SingleInstanceManager::pipeName() const
{
    // QLocalServer 在 Windows 上使用 \\.\pipe\{name}
    // 加 -Notify 后缀避免与 Mutex 名称冲突
    return m_baseName + QStringLiteral("-Notify");
}
