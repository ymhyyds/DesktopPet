// ── MockServer helper implementation (included by test_main.cpp) ──────────

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QObject>
#include <QCoreApplication>
#include <QDebug>
#include <QEventLoop>
#include <QTimer>

bool MockServer::start(const QString &scriptPath)
{
    proc = new QProcess();
    proc->setProcessChannelMode(QProcess::ForwardedErrorChannel);
    proc->start(QStringLiteral("python"), {scriptPath});

    if (!proc->waitForStarted(5000)) {
        qWarning() << "[MockServer] Failed to start:" << proc->errorString();
        return false;
    }

    // Read PORT: line from stdout
    while (port == 0) {
        if (!proc->waitForReadyRead(5000)) {
            qWarning() << "[MockServer] Timeout waiting for port";
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

    return port > 0;
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
}

void MockServer::setDelay(int ms)
{
    if (port == 0) return;
    QNetworkAccessManager mgr;
    QNetworkRequest req(QUrl(QStringLiteral("http://127.0.0.1:%1/_set_delay?ms=%2").arg(port).arg(ms)));
    QNetworkReply *reply = mgr.post(req, QByteArray());
    QEventLoop loop;
    QTimer::singleShot(3000, &loop, &QEventLoop::quit);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    reply->deleteLater();
}

void MockServer::setError(int httpCode, const QString &target)
{
    if (port == 0) return;
    QNetworkAccessManager mgr;
    QUrl url(QStringLiteral("http://127.0.0.1:%1/_set_error?code=%2&target=%3")
                 .arg(port).arg(httpCode).arg(target));
    QNetworkRequest req(url);
    QNetworkReply *reply = mgr.post(req, QByteArray());
    QEventLoop loop;
    QTimer::singleShot(3000, &loop, &QEventLoop::quit);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    reply->deleteLater();
}

void MockServer::clearError()
{
    setError(0);
}

void MockServer::setResponseType(const QString &target, const QString &type)
{
    if (port == 0) return;
    QNetworkAccessManager mgr;
    QUrl url(QStringLiteral("http://127.0.0.1:%1/_set_response?target=%2&type=%3")
                 .arg(port).arg(target).arg(type));
    QNetworkRequest req(url);
    QNetworkReply *reply = mgr.post(req, QByteArray());
    QEventLoop loop;
    QTimer::singleShot(3000, &loop, &QEventLoop::quit);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    reply->deleteLater();
}

QString MockServer::baseUrl() const
{
    return QStringLiteral("http://127.0.0.1:%1").arg(port);
}
