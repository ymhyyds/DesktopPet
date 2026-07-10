#pragma once

#include <QString>
class QProcess;

// ── Mock server helper ─────────────────────────────────────────────────────
struct MockServer {
    QProcess *proc = nullptr;
    int port = 0;
    QString m_lastError;

    bool start(const QString &scriptPath);
    void stop();
    bool setDelay(int ms);
    bool setError(int httpCode, const QString &target = QStringLiteral("both"));
    bool setResponseType(const QString &target, const QString &type);
    bool clearError();
    bool waitUntilReady(int timeoutMs = 5000);
    QString baseUrl() const;
    QString lastErrorString() const { return m_lastError; }
};
