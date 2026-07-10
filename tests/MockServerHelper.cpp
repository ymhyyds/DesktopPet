// ── MockServer helper implementation (included by test_main.cpp) ──────────

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QEventLoop>
#include <QTimer>
#include <QElapsedTimer>
#include <QDebug>

// ── Internal: send a control POST with retry up to deadlineMs ─────────────
// Returns true iff the server responded HTTP 200 within deadline.
static bool sendControlRequest(const QUrl &url, int deadlineMs,
                               QString *outError)
{
    QElapsedTimer clock;
    clock.start();

    int attempt = 0;
    QString lastError;
    int lastHttpStatus = -1;

    while (clock.elapsed() < deadlineMs) {
        ++attempt;
        const int remaining = deadlineMs - static_cast<int>(clock.elapsed());
        const int perAttemptTimeout = qMin(remaining, 1500);

        QNetworkAccessManager mgr;
        QNetworkRequest req(url);
        QNetworkReply *reply = mgr.post(req, QByteArray());

        QEventLoop loop;
        QTimer timer;
        timer.setSingleShot(true);
        QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        timer.start(perAttemptTimeout);
        loop.exec();

        if (!timer.isActive()) {
            // Timer fired before reply finished = timeout
            lastError = QStringLiteral("Timeout after %1ms")
                            .arg(perAttemptTimeout);
            lastHttpStatus = 0;
            reply->abort();
            reply->deleteLater();

            // Brief back-off before retry
            if (clock.elapsed() < deadlineMs) {
                QEventLoop waiter;
                QTimer::singleShot(100, &waiter, &QEventLoop::quit);
                waiter.exec();
            }
            continue;
        }
        timer.stop();

        // Reply finished (or errored) within timeout
        lastError = reply->errorString();
        lastHttpStatus = reply->attribute(
            QNetworkRequest::HttpStatusCodeAttribute).toInt();

        if (reply->error() == QNetworkReply::NoError && lastHttpStatus == 200) {
            reply->deleteLater();
            return true;  // success
        }

        reply->deleteLater();

        // Brief back-off before retry
        if (clock.elapsed() < deadlineMs) {
            QEventLoop waiter;
            QTimer::singleShot(100, &waiter, &QEventLoop::quit);
            waiter.exec();
        }
    }

    if (outError) {
        *outError = QStringLiteral(
            "%1 — attempts=%2 lastError=\"%3\" lastHttp=%4")
                        .arg(url.path())
                        .arg(attempt)
                        .arg(lastError)
                        .arg(lastHttpStatus);
    }
    return false;
}


// ═══════════════════════════════════════════════════════════════════════════
// Public API
// ═══════════════════════════════════════════════════════════════════════════

bool MockServer::start(const QString &scriptPath)
{
    if (proc) {
        m_lastError = QStringLiteral("Already started");
        return false;
    }

    proc = new QProcess();
    proc->setProcessChannelMode(QProcess::ForwardedErrorChannel);
    proc->start(QStringLiteral("python"), {scriptPath});

    if (!proc->waitForStarted(5000)) {
        m_lastError = QStringLiteral("Failed to start: %1").arg(proc->errorString());
        delete proc;
        proc = nullptr;
        return false;
    }

    // Read PORT: line from stdout
    while (port == 0) {
        if (!proc->waitForReadyRead(5000)) {
            m_lastError = QStringLiteral("Timeout waiting for port");
            proc->kill();
            proc->waitForFinished(1000);
            delete proc;
            proc = nullptr;
            return false;
        }
        const QString line = QString::fromUtf8(proc->readLine()).trimmed();
        if (line.startsWith(QStringLiteral("PORT:"))) {
            port = line.mid(5).toInt();
            qDebug() << "[MockServer] Started on port" << port;
            break;
        }
    }

    // Consume any remaining init output
    proc->setReadChannel(QProcess::StandardOutput);

    if (port <= 0) {
        m_lastError = QStringLiteral("Failed to parse port");
        return false;
    }
    return true;
}

void MockServer::stop()
{
    if (proc && proc->state() == QProcess::Running) {
        proc->write("STOP\n");
        if (!proc->waitForFinished(3000)) {
            proc->kill();
            proc->waitForFinished(1000);
        }
    }
    delete proc;
    proc = nullptr;
    port = 0;
    m_lastError.clear();
}

bool MockServer::setDelay(int ms)
{
    if (port == 0) {
        m_lastError = QStringLiteral("Server not running");
        return false;
    }
    QUrl url(QStringLiteral("http://127.0.0.1:%1/_set_delay?ms=%2")
                 .arg(port).arg(ms));
    return sendControlRequest(url, 5000, &m_lastError);
}

bool MockServer::setError(int httpCode, const QString &target)
{
    if (port == 0) {
        m_lastError = QStringLiteral("Server not running");
        return false;
    }
    QUrl url(QStringLiteral("http://127.0.0.1:%1/_set_error?code=%2&target=%3")
                 .arg(port).arg(httpCode).arg(target));
    return sendControlRequest(url, 5000, &m_lastError);
}

bool MockServer::setResponseType(const QString &target, const QString &type)
{
    if (port == 0) {
        m_lastError = QStringLiteral("Server not running");
        return false;
    }
    QUrl url(QStringLiteral("http://127.0.0.1:%1/_set_response?target=%2&type=%3")
                 .arg(port).arg(target).arg(type));
    return sendControlRequest(url, 5000, &m_lastError);
}

bool MockServer::clearError()
{
    return setError(0);
}

bool MockServer::waitUntilReady(int timeoutMs)
{
    QElapsedTimer clock;
    clock.start();

    while (clock.elapsed() < timeoutMs) {
        if (setDelay(0)) {
            return true;
        }
        // setDelay already did back-off; just check deadline
    }
    m_lastError = QStringLiteral("Server not ready after %1ms: %2")
                      .arg(timeoutMs).arg(m_lastError);
    return false;
}

QString MockServer::baseUrl() const
{
    return QStringLiteral("http://127.0.0.1:%1").arg(port);
}
