#pragma once

#include <QString>
#include <QProcess>

// ── Mock server helper ─────────────────────────────────────────────────────
struct MockServer {
    QProcess *proc = nullptr;
    int port = 0;

    bool start(const QString &scriptPath);
    void stop();
    void setDelay(int ms);
    void setError(int httpCode, const QString &target = QStringLiteral("both"));
    void setResponseType(const QString &target, const QString &type);
    void clearError();
    QString baseUrl() const;
};
