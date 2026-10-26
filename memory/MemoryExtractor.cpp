#include "MemoryExtractor.h"

#include <QDateTime>
#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QMap>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSet>
#include <QTimer>
#include <QUrl>
#include <algorithm>

namespace {

constexpr int kRequestTimeoutMs = 45000;
constexpr int kBackfillChunkChars = 8000;
constexpr int kBackfillAssistantMaxChars = 300;
constexpr int kExtractAssistantMaxChars = 500;
constexpr int kMemoryContentMaxChars = 200;

QString normalizeKind(const QString &kind)
{
    static const QSet<QString> allowed = {
        QStringLiteral("profile"),
        QStringLiteral("preference"),
        QStringLiteral("event"),
        QStringLiteral("promise"),
        QStringLiteral("relationship"),
    };
    const QString k = kind.trimmed().toLower();
    return allowed.contains(k) ? k : QStringLiteral("event");
}

QString sanitizeContent(const QString &raw)
{
    QString text = raw.trimmed();
    // 记忆内容会作为注入块的一部分，方括号替换成全角，避免伪造标签
    text.replace(QLatin1Char('['), QStringLiteral("［"));
    text.replace(QLatin1Char(']'), QStringLiteral("］"));
    text.replace(QRegularExpression(QStringLiteral("[\\r\\n]+")), QStringLiteral(" "));
    text.replace(QRegularExpression(QStringLiteral("\\s{2,}")), QStringLiteral(" "));
    if (text.size() > kMemoryContentMaxChars) {
        text = text.left(kMemoryContentMaxChars);
    }
    return text.trimmed();
}

/// 归因校验用的归一化：去掉空白和标点，只留字母数字/汉字。
QString evidenceNormalized(const QString &text)
{
    QString out;
    for (const QChar &ch : text) {
        if (ch.isLetterOrNumber()) out.append(ch.toLower());
    }
    return out;
}

QSet<QString> bigramSet(const QString &text);

/// 归一化后两串的最长公共子串长度（用于归因校验的退化判定）。
/// 用“连续重合”而不是“bigram 命中个数”：后者会被“主人”“抱抱”这类
/// 高频两字词凑够门槛，正是“主人答应早安抱抱”假卡的漏网路径。
int longestCommonRun(const QString &a, const QString &b)
{
    const QString x = evidenceNormalized(a);
    const QString y = evidenceNormalized(b);
    if (x.isEmpty() || y.isEmpty()) return 0;

    QVector<int> prev(y.size() + 1, 0);
    QVector<int> cur(y.size() + 1, 0);
    int best = 0;
    for (int i = 1; i <= x.size(); ++i) {
        cur[0] = 0;
        for (int j = 1; j <= y.size(); ++j) {
            if (x.at(i - 1) == y.at(j - 1)) {
                cur[j] = prev.at(j - 1) + 1;
                best = qMax(best, cur.at(j));
            } else {
                cur[j] = 0;
            }
        }
        prev.swap(cur);
    }
    return best;
}

bool looksLikeInjection(const QString &content)
{
    return content.contains(QStringLiteral("忽略以上"))
        || content.contains(QStringLiteral("忽略上述"))
        || content.contains(QStringLiteral("忽略之前"))
        || content.toLower().contains(QStringLiteral("ignore previous"))
        || content.toLower().contains(QStringLiteral("ignore above"));
}

/// 取 JSON 数组的切片，成功与否用 hasArray 判断（"[]" 也算成功但结果为空）。
bool hasJsonArray(const QString &raw)
{
    const int lb = raw.indexOf(QLatin1Char('['));
    const int rb = raw.lastIndexOf(QLatin1Char(']'));
    return lb >= 0 && rb > lb;
}

QSet<QString> bigramSet(const QString &text)
{
    QSet<QString> grams;
    QString normalized;
    normalized.reserve(text.size());
    for (const QChar &ch : text) {
        if (!ch.isSpace()) normalized.append(ch.toLower());
    }
    if (normalized.size() < 2) {
        if (!normalized.isEmpty()) grams.insert(normalized);
        return grams;
    }
    for (int i = 0; i + 1 < normalized.size(); ++i) {
        grams.insert(normalized.mid(i, 2));
    }
    return grams;
}

} // namespace

MemoryExtractor::MemoryExtractor(MemoryDB *db, QObject *parent)
    : QObject(parent)
    , m_db(db)
{
    m_network = new QNetworkAccessManager(this);

    m_timeoutTimer = new QTimer(this);
    m_timeoutTimer->setSingleShot(true);
    connect(m_timeoutTimer, &QTimer::timeout, this, [this]() {
        if (!m_activeReply) return;
        qWarning() << "[MemoryExtractor] 请求超时，中止";
        m_activeReply->setProperty("memoryTimedOut", true);
        m_activeReply->abort();
    });
}

MemoryExtractor::~MemoryExtractor()
{
    if (m_activeReply) {
        m_activeReply->abort();
        m_activeReply = nullptr;
    }
}

void MemoryExtractor::setApiConfig(const QString &apiKey, const QString &endpoint,
                                   const QString &model)
{
    m_apiKey = apiKey.trimmed();
    if (!endpoint.trimmed().isEmpty()) m_endpoint = endpoint.trimmed();
    if (!model.trimmed().isEmpty()) m_model = model.trimmed();
}

// ═══════════════════════════════════════════════════════════════════════════
// 自动抽取
// ═══════════════════════════════════════════════════════════════════════════

bool MemoryExtractor::extractFromRecentMessages(const QString &characterName, int limit)
{
    if (!m_db || characterName.isEmpty()) return false;
    if (m_busy) {
        qInfo() << "[MemoryExtractor] 已有在途请求，跳过本次抽取";
        return false;
    }
    if (m_apiKey.isEmpty()) {
        emit extractionFailed(QStringLiteral("未配置 DeepSeek API Key"));
        return false;
    }

    const QList<StoredMessage> messages = m_db->loadRecentMessages(characterName, limit);

    // 只取"用户消息及其后的助手回复"：开头连续 assistant（挂机搭话/重试残留）直接跳过，
    // 否则 AI 自说自话的提议会被提炼成"用户事实"。
    QString transcript;
    bool seenUser = false;
    for (const StoredMessage &m : messages) {
        if (m.role == QStringLiteral("user")) {
            seenUser = true;
            transcript += QStringLiteral("主人：")
                          + m.content.left(kExtractAssistantMaxChars).trimmed()
                          + QLatin1Char('\n');
        } else if (m.role == QStringLiteral("assistant") && seenUser) {
            transcript += characterName + QStringLiteral("：")
                          + m.content.left(kExtractAssistantMaxChars).trimmed()
                          + QLatin1Char('\n');
        }
    }

    if (!seenUser || transcript.trimmed().isEmpty()) {
        return false;
    }

    m_requestCharacter = characterName;
    return sendExtractionRequest(transcript, false);
}

// ═══════════════════════════════════════════════════════════════════════════
// 历史回填
// ═══════════════════════════════════════════════════════════════════════════

bool MemoryExtractor::startBackfill(const QString &characterName,
                                    const QString &afterIso,
                                    const QString &beforeIso)
{
    if (!m_db || characterName.isEmpty()) return false;
    if (m_busy) {
        qWarning() << "[MemoryExtractor] 有在途请求，无法开始回填";
        return false;
    }
    if (m_apiKey.isEmpty()) {
        return false;
    }

    const QList<StoredMessage> messages =
        m_db->loadMessagesRange(characterName, afterIso, beforeIso);
    if (messages.isEmpty()) {
        qInfo() << "[MemoryExtractor] 回填范围内没有消息";
        return false;
    }

    // 1) 按自然日聚合成行：用户消息全量，助手消息截断（长回复大多是角色扮演）
    QMap<QString, QStringList> byDay;
    for (const StoredMessage &m : messages) {
        const QString content = m.content.trimmed();
        if (content.isEmpty()) continue;
        const bool isUser = (m.role == QStringLiteral("user"));
        const QString text = isUser
            ? content
            : content.left(kBackfillAssistantMaxChars);
        byDay[m.createdAt.left(10)].append(
            (isUser ? QStringLiteral("主人：") : characterName + QStringLiteral("：")) + text);
    }

    // 2) 按 8000 字符打包
    m_backfillChunks.clear();
    BackfillChunk current;
    const auto flush = [&]() {
        if (!current.text.trimmed().isEmpty()) {
            m_backfillChunks.append(current);
        }
        current = BackfillChunk();
    };

    for (auto it = byDay.constBegin(); it != byDay.constEnd(); ++it) {
        const QString header = QStringLiteral("\n【%1】\n").arg(it.key());
        const QStringList &lines = it.value();
        if (current.text.size() + header.size() + lines.join(QLatin1Char('\n')).size()
            <= kBackfillChunkChars) {
            current.text += header;
            for (const QString &line : lines) {
                current.text += line + QLatin1Char('\n');
                if (line.startsWith(QStringLiteral("主人："))) ++current.userCount;
            }
            continue;
        }

        if (!current.text.trimmed().isEmpty()) flush();

        // 单日超长：按行切块
        current.text += header;
        for (const QString &line : lines) {
            if (current.text.size() + line.size() + 1 > kBackfillChunkChars) {
                flush();
            }
            current.text += line + QLatin1Char('\n');
            if (line.startsWith(QStringLiteral("主人："))) ++current.userCount;
        }
    }
    flush();

    if (m_backfillChunks.isEmpty()) {
        return false;
    }

    // 3) 只保留含用户消息的块（纯助手块没有可信事实）
    QList<BackfillChunk> useful;
    for (const BackfillChunk &c : m_backfillChunks) {
        if (c.userCount > 0) useful.append(c);
    }
    m_backfillChunks = useful;
    if (m_backfillChunks.isEmpty()) {
        return false;
    }

    m_backfillCharacter = characterName;
    m_backfillIndex = 0;
    m_backfillAdded = 0;
    m_backfillUpdated = 0;
    m_backfillFailed = 0;
    m_backfillRunning = true;

    qInfo() << "[MemoryExtractor] 开始回填：" << characterName
            << "消息" << messages.size() << "条，分块" << m_backfillChunks.size() << "个";
    emit backfillProgress(0, m_backfillChunks.size());
    sendNextBackfillChunk();
    return true;
}

void MemoryExtractor::sendNextBackfillChunk()
{
    if (!m_backfillRunning) return;
    if (m_backfillIndex >= m_backfillChunks.size()) {
        finishBackfill();
        return;
    }

    const BackfillChunk chunk = m_backfillChunks.at(m_backfillIndex);
    m_requestCharacter = m_backfillCharacter;
    if (!sendExtractionRequest(chunk.text, true)) {
        qWarning() << "[MemoryExtractor] 回填分块发送失败，索引"
                   << m_backfillIndex;
        ++m_backfillFailed;
        ++m_backfillIndex;
        emit backfillProgress(m_backfillIndex, m_backfillChunks.size());
        sendNextBackfillChunk();
    }
}

void MemoryExtractor::finishBackfill()
{
    m_backfillRunning = false;
    m_backfillChunks.clear();
    qInfo() << "[MemoryExtractor] 回填完成：added=" << m_backfillAdded
            << "updated=" << m_backfillUpdated
            << "failed=" << m_backfillFailed;
    emit backfillProgress(m_backfillIndex, m_backfillIndex);
    emit backfillFinished(m_backfillAdded, m_backfillUpdated, m_backfillFailed);
}

void MemoryExtractor::cancelBackfill()
{
    if (!m_backfillRunning) return;
    m_backfillRunning = false;
    const int done = m_backfillIndex;
    const int total = m_backfillChunks.size();
    if (m_activeReply) {
        m_activeReply->abort(); // finished 回调里会因 !m_backfillRunning 直接返回
    }
    m_backfillChunks.clear();
    qInfo() << "[MemoryExtractor] 回填已取消：" << done << "/" << total;
    emit backfillCancelled(done, total);
}

// ═══════════════════════════════════════════════════════════════════════════
// 手动命令
// ═══════════════════════════════════════════════════════════════════════════

int MemoryExtractor::rememberManually(const QString &characterName, const QString &text)
{
    if (!m_db) return -1;
    const QString content = sanitizeContent(text);
    if (content.isEmpty()) return -1;

    MemoryRecord m;
    m.kind = QStringLiteral("event");
    if (content.contains(QStringLiteral("喜欢")) || content.contains(QStringLiteral("爱吃"))
        || content.contains(QStringLiteral("爱喝")) || content.contains(QStringLiteral("讨厌"))
        || content.contains(QStringLiteral("偏好")) || content.contains(QStringLiteral("口味"))
        || content.contains(QStringLiteral("习惯")) || content.contains(QStringLiteral("不吃"))) {
        m.kind = QStringLiteral("preference");
    } else if (content.contains(QStringLiteral("生日"))
               || content.contains(QStringLiteral("名字"))
               || content.contains(QStringLiteral("住在"))
               || content.contains(QStringLiteral("工作"))
               || content.contains(QStringLiteral("叫"))) {
        m.kind = QStringLiteral("profile");
    } else if (content.contains(QStringLiteral("答应")) || content.contains(QStringLiteral("约好"))
               || content.contains(QStringLiteral("说好")) || content.contains(QStringLiteral("保证"))) {
        m.kind = QStringLiteral("promise");
    }

    m.subject = QStringLiteral("主人");
    m.content = content;
    m.keywords = deriveKeywords(content);
    m.importance = (m.kind == QStringLiteral("preference")
                    || m.kind == QStringLiteral("profile"))
        ? 4 : 3;
    m.status = QStringLiteral("active");

    const QString charName = characterFor(m, characterName);
    m.characterName = charName;

    // 与自动抽取共用同一套去重规则
    double bestSim = 0.0;
    MemoryRecord best;
    const QList<MemoryRecord> similar =
        m_db->findSimilarMemories(charName, m.kind, m.subject);
    for (const MemoryRecord &r : similar) {
        const double s = similarity(content, r.content);
        if (s > bestSim) {
            bestSim = s;
            best = r;
        }
    }

    if (best.isValid() && bestSim >= 0.7) {
        best.content = content;
        best.keywords = m.keywords;
        best.importance = qMax(best.importance, m.importance);
        m_db->updateMemory(best);
        qInfo() << "[MemoryExtractor] /记住 命中已有卡片，已更新 id=" << best.id;
        return best.id;
    }

    const int id = m_db->addMemory(m);
    qInfo() << "[MemoryExtractor] /记住 新增卡片 id=" << id
            << "char=" << charName << "kind=" << m.kind;
    return id;
}

int MemoryExtractor::forgetMatching(const QString &characterName, const QString &keyword)
{
    if (!m_db || keyword.trimmed().isEmpty()) return 0;

    const QList<MemoryRecord> matches =
        m_db->matchMemories(characterName, keyword.trimmed());
    int removed = 0;
    for (const MemoryRecord &m : matches) {
        if (m_db->deleteMemory(m.id)) ++removed;
    }
    qInfo() << "[MemoryExtractor] /忘掉" << keyword << "删除" << removed << "条";
    return removed;
}

// ═══════════════════════════════════════════════════════════════════════════
// 网络请求
// ═══════════════════════════════════════════════════════════════════════════

bool MemoryExtractor::sendExtractionRequest(const QString &transcript, bool backfill)
{
    if (m_busy || m_apiKey.isEmpty()) return false;

    // 记录本请求里的主人原话，供 applyExtraction 做归因校验
    m_activeUserText.clear();
    for (const QString &line : transcript.split(QLatin1Char('\n'))) {
        const QString trimmed = line.trimmed();
        if (trimmed.startsWith(QStringLiteral("主人："))
            || trimmed.startsWith(QStringLiteral("主人:"))) {
            m_activeUserText += trimmed.mid(3) + QLatin1Char('\n');
        }
    }

    QJsonArray messages;
    messages.append(QJsonObject{
        { QStringLiteral("role"), QStringLiteral("system") },
        { QStringLiteral("content"), buildSystemPrompt(backfill) },
    });
    messages.append(QJsonObject{
        { QStringLiteral("role"), QStringLiteral("user") },
        { QStringLiteral("content"),
          buildUserPrompt(transcript, m_requestCharacter, backfill) },
    });

    QJsonObject body;
    body.insert(QStringLiteral("model"), m_model);
    body.insert(QStringLiteral("messages"), messages);
    body.insert(QStringLiteral("stream"), false);
    body.insert(QStringLiteral("temperature"), 0.2);
    body.insert(QStringLiteral("max_tokens"), 3000);

    QNetworkRequest request{QUrl(m_endpoint)};
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/json"));
    request.setRawHeader("Authorization", "Bearer " + m_apiKey.toUtf8());

    m_busy = true;
    QNetworkReply *reply =
        m_network->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    m_activeReply = reply;
    m_timeoutTimer->start(kRequestTimeoutMs);

    connect(reply, &QNetworkReply::finished, this, [this, reply, backfill]() {
        m_timeoutTimer->stop();
        if (m_activeReply == reply) m_activeReply = nullptr;
        m_busy = false;
        handleReply(reply, backfill);
        reply->deleteLater();
    });

    return true;
}

void MemoryExtractor::handleReply(QNetworkReply *reply, bool backfill)
{
    if (backfill && !m_backfillRunning) {
        return; // 用户取消后到达的迟到结果
    }

    const bool timedOut = reply->property("memoryTimedOut").toBool();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray raw = reply->readAll();

    QString error;
    if (timedOut) {
        error = QStringLiteral("请求超时");
    } else if (status >= 400) {
        error = QStringLiteral("HTTP %1").arg(status);
    } else if (reply->error() != QNetworkReply::NoError) {
        error = reply->errorString();
    }

    if (error.isEmpty()) {
        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(raw, &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
            error = QStringLiteral("响应不是合法 JSON");
        } else {
            const QJsonArray choices = doc.object().value(QStringLiteral("choices")).toArray();
            if (choices.isEmpty()) {
                error = QStringLiteral("响应缺少 choices");
            } else {
                const QString content = choices.first().toObject()
                    .value(QStringLiteral("message")).toObject()
                    .value(QStringLiteral("content")).toString();
                if (content.trimmed().isEmpty()) {
                    error = QStringLiteral("响应内容为空");
                } else if (!hasJsonArray(content)) {
                    error = QStringLiteral("模型没有返回 JSON 数组");
                } else {
                    const QList<MemoryRecord> items = parseExtractionJson(content);
                    int added = 0, updated = 0, deleted = 0;
                    applyExtraction(items, m_requestCharacter, added, updated, deleted);
                    qInfo() << "[MemoryExtractor] 抽取完成："
                            << (backfill ? "回填块" : "自动")
                            << "added=" << added << "updated=" << updated
                            << "deleted=" << deleted;

                    if (backfill) {
                        m_backfillAdded += added;
                        m_backfillUpdated += updated;
                        ++m_backfillIndex;
                        emit backfillProgress(m_backfillIndex, m_backfillChunks.size());
                        sendNextBackfillChunk();
                    } else {
                        emit extractionFinished(added, updated, deleted);
                    }
                    return;
                }
            }
        }
    }

    const QString preview = QString::fromUtf8(raw.left(200)).simplified();
    qWarning() << "[MemoryExtractor] 请求失败：" << error << preview;

    if (backfill) {
        ++m_backfillFailed;
        ++m_backfillIndex;
        emit backfillProgress(m_backfillIndex, m_backfillChunks.size());
        sendNextBackfillChunk();
    } else {
        emit extractionFailed(error);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// 写入 / 去重
// ═══════════════════════════════════════════════════════════════════════════

bool MemoryExtractor::hasUserEvidence(const MemoryRecord &m, const QString &userText)
{
    if (userText.trimmed().isEmpty()) return false;

    const QString evidence = evidenceNormalized(m.evidence);
    if (evidence.size() >= 2) {
        // 模型给出的“主人原话”必须真能在主人发言里找到，否则是它自己编的
        return evidenceNormalized(userText).contains(evidence);
    }
    // promise 必须引用主人原话：模型编“主人答应……”时从不给 evidence，
    // 退化判定救不了它，只能直接丢。
    if (m.kind == QStringLiteral("promise")) return false;
    // event 允许退化：内容与主人原话至少要有 3 个连续汉字重合
    // （“一起去看海”这类短语能过，“每天早安抱抱”这类凑词过不了）。
    return longestCommonRun(m.content, userText) >= 3;
}

QString MemoryExtractor::characterFor(const MemoryRecord &m,
                                      const QString &currentCharacter) const
{
    // 画像/偏好且对象是主人 → 跨角色共享；其余按角色隔离
    const bool aboutOwner = m.subject.trimmed().isEmpty()
        || m.subject == QStringLiteral("主人")
        || m.subject == QStringLiteral("我")
        || m.subject == QStringLiteral("用户");
    if ((m.kind == QStringLiteral("profile")
         || m.kind == QStringLiteral("preference")) && aboutOwner) {
        return QStringLiteral("*");
    }
    return currentCharacter;
}

void MemoryExtractor::applyExtraction(const QList<MemoryRecord> &items,
                                      const QString &currentCharacter,
                                      int &added, int &updated, int &deleted)
{
    if (!m_db) return;

    for (MemoryRecord item : items) {
        item.kind = normalizeKind(item.kind);
        item.subject = item.subject.trimmed().isEmpty()
            ? QStringLiteral("主人") : item.subject.trimmed();
        item.content = sanitizeContent(item.content);
        if (item.content.isEmpty() || looksLikeInjection(item.content)) {
            continue;
        }
        item.importance = qBound(1, item.importance, 5);
        item.confidence = qBound(0.0, item.confidence, 1.0);

        // 归因校验：promise/event 必须有主人原话依据，否则视为助手自说自话丢弃
        if ((item.kind == QStringLiteral("promise")
             || item.kind == QStringLiteral("event"))
            && item.action != QStringLiteral("delete")
            && !hasUserEvidence(item, m_activeUserText)) {
            qInfo() << "[MemoryExtractor] 丢弃无主人依据的卡片：" << item.kind
                    << item.content.left(40);
            continue;
        }

        const QString charName = characterFor(item, currentCharacter);

        // ──── delete ────
        if (item.action == QStringLiteral("delete")) {
            MemoryRecord target;
            bool found = false;
            if (item.id > 0 && m_db->memoryById(item.id, &target)) {
                found = true;
            } else {
                double bestSim = 0.0;
                const QList<MemoryRecord> pool =
                    m_db->findSimilarMemories(charName, item.kind, item.subject);
                for (const MemoryRecord &r : pool) {
                    const double s = similarity(item.content, r.content);
                    if (s > bestSim) {
                        bestSim = s;
                        target = r;
                        found = true;
                    }
                }
                found = found && bestSim >= 0.55;
            }
            if (found && target.isValid()) {
                if (m_db->deleteMemory(target.id)) ++deleted;
            }
            continue;
        }

        // ──── update（按 id） ────
        if (item.action == QStringLiteral("update") && item.id > 0) {
            MemoryRecord existing;
            if (m_db->memoryById(item.id, &existing)
                && (existing.characterName == charName
                    || existing.characterName == QStringLiteral("*"))) {
                existing.content = item.content;
                existing.keywords = item.keywords.trimmed().isEmpty()
                    ? existing.keywords : item.keywords.trimmed();
                existing.importance = qMax(existing.importance, item.importance);
                existing.confidence = qMax(existing.confidence, item.confidence);
                if (m_db->updateMemory(existing)) ++updated;
                continue;
            }
        }

        // ──── 合并去重：同 (char, kind, subject) 且 bigram Jaccard > 0.7 ────
        double bestSim = 0.0;
        MemoryRecord best;
        const QList<MemoryRecord> similar =
            m_db->findSimilarMemories(charName, item.kind, item.subject);
        for (const MemoryRecord &r : similar) {
            const double s = similarity(item.content, r.content);
            if (s > bestSim) {
                bestSim = s;
                best = r;
            }
        }

        if (best.isValid() && bestSim >= 0.7) {
            best.content = item.content;
            if (!item.keywords.trimmed().isEmpty()) {
                best.keywords = item.keywords.trimmed();
            }
            best.importance = qMax(best.importance, item.importance);
            best.confidence = qMax(best.confidence, item.confidence);
            if (m_db->updateMemory(best)) ++updated;
            continue;
        }

        // ──── add ────
        item.characterName = charName;
        item.keywords = item.keywords.trimmed().isEmpty()
            ? deriveKeywords(item.content) : item.keywords.trimmed();
        item.status = QStringLiteral("active");
        item.id = -1;
        item.createdAt.clear();
        if (m_db->addMemory(item) > 0) ++added;
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Prompt 与解析
// ═══════════════════════════════════════════════════════════════════════════

QString MemoryExtractor::buildSystemPrompt(bool backfill) const
{
    QString prompt = QStringLiteral(
        "你是一个记忆抽取器，不参与角色扮演，不回复对话内容，只输出 JSON。\n"
        "任务：从给定的对话片段中，抽取\"主人明确说过或确认过\"的长期事实。\n"
        "规则：\n"
        "1. 只记录主人自己说出的信息；助手单方面的提议、想象、角色扮演内容一律忽略。\n"
        "2. 每条事实一句话，20~80 字，必须是可长期复用的信息（偏好/事实/约定/事件/关系）。\n"
        "3. kind 只能是 profile/preference/event/promise/relationship。\n"
        "4. subject 通常为\"主人\"；涉及特定话题时可写话题名。\n"
        "5. keywords 是 2~5 个空格分隔的检索词。\n"
        "6. importance 1~5：5=身份/生日等重要且稳定，3=普通喜好，1=一次性闲聊。\n"
        "7. action 只能是 add / update / delete；需要修改或删除已有记忆时填 id。\n"
        "8. 没有值得记录的内容就输出 []。\n"
        "9. 归因铁律：只有\"主人：\"开头的发言才算主人的话；助手（角色）的提议、"
        "撒娇、愿望、角色扮演一律不算，尤其禁止写成\"主人答应/主人承诺\"。\n"
        "10. promise / event 类必须附 evidence 字段：从\"主人：\"行里原样摘一小段"
        "（2~30 字，不得改写、不得跨行拼接）。找不到主人原话就不要输出这条。\n"
        "只输出 JSON 数组，不要任何解释或 Markdown。\n"
        "输出格式示例：\n"
        "[{\"kind\":\"preference\",\"subject\":\"主人\",\"content\":\"主人喜欢喝冰美式，不加糖\","
        "\"keywords\":\"咖啡 美式 口味\",\"importance\":3,\"action\":\"add\"},"
        "{\"kind\":\"promise\",\"subject\":\"主人\",\"content\":\"主人答应以后伺候时穿黑丝红色高跟鞋\","
        "\"keywords\":\"高跟鞋 黑丝\",\"importance\":4,\"action\":\"add\","
        "\"evidence\":\"以后伺候我穿黑丝红色高跟鞋\"}]"
    );
    if (backfill) {
        prompt += QStringLiteral(
            "\n\n这是历史对话回填。只抽取用户明确说过/确认过的事实、偏好、约定、事件；"
            "忽略助手单方面的提议和角色扮演内容；没有可记的就返回 []。"
            "对话里\"主人：\"开头的是用户发言，其余是助手发言。"
        );
    }
    return prompt;
}

QString MemoryExtractor::buildUserPrompt(const QString &transcript,
                                         const QString &characterName,
                                         bool backfill) const
{
    QString prompt = QStringLiteral("当前角色：%1\n")
                         .arg(characterName.isEmpty() ? QStringLiteral("*") : characterName);
    if (backfill) {
        prompt += QStringLiteral("以下是历史对话片段：\n");
    } else {
        prompt += QStringLiteral("以下是最近的对话片段：\n");
    }
    prompt += transcript.trimmed();
    prompt += QStringLiteral("\n\n请按规则输出 JSON 数组。");
    return prompt;
}

QList<MemoryRecord> MemoryExtractor::parseExtractionJson(const QString &raw)
{
    QList<MemoryRecord> out;
    const int lb = raw.indexOf(QLatin1Char('['));
    const int rb = raw.lastIndexOf(QLatin1Char(']'));
    if (lb < 0 || rb <= lb) return out;

    QJsonParseError error;
    const QJsonDocument doc =
        QJsonDocument::fromJson(raw.mid(lb, rb - lb + 1).toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isArray()) {
        return out;
    }

    for (const QJsonValue &value : doc.array()) {
        if (!value.isObject()) continue;
        const QJsonObject obj = value.toObject();

        MemoryRecord r;
        r.content = obj.value(QStringLiteral("content")).toString().trimmed();
        if (r.content.isEmpty()) {
            r.content = obj.value(QStringLiteral("fact")).toString().trimmed();
        }
        if (r.content.isEmpty()) continue;

        r.kind = normalizeKind(obj.value(QStringLiteral("kind")).toString());
        r.subject = obj.value(QStringLiteral("subject")).toString().trimmed();
        if (r.subject.isEmpty()) r.subject = QStringLiteral("主人");

        const QJsonValue kw = obj.value(QStringLiteral("keywords"));
        if (kw.isArray()) {
            QStringList parts;
            for (const QJsonValue &k : kw.toArray()) {
                const QString s = k.toString().trimmed();
                if (!s.isEmpty()) parts.append(s);
            }
            r.keywords = parts.join(QLatin1Char(' '));
        } else {
            r.keywords = kw.toString().trimmed();
        }

        r.importance = qBound(1, obj.value(QStringLiteral("importance")).toInt(3), 5);
        r.confidence = qBound(0.0,
                              obj.value(QStringLiteral("confidence")).toDouble(1.0), 1.0);

        int targetId = obj.value(QStringLiteral("id")).toInt(-1);
        if (targetId <= 0) {
            targetId = obj.value(QStringLiteral("target_id")).toInt(-1);
        }
        r.id = targetId;

        r.action = obj.value(QStringLiteral("action"))
                       .toString(QStringLiteral("add")).trimmed().toLower();
        if (r.action != QStringLiteral("add")
            && r.action != QStringLiteral("update")
            && r.action != QStringLiteral("delete")) {
            r.action = QStringLiteral("add");
        }

        r.evidence = obj.value(QStringLiteral("evidence")).toString().trimmed();

        out.append(r);
    }
    return out;
}

double MemoryExtractor::similarity(const QString &a, const QString &b)
{
    const QSet<QString> ga = bigramSet(a.trimmed());
    const QSet<QString> gb = bigramSet(b.trimmed());
    if (ga.isEmpty() || gb.isEmpty()) {
        return a.trimmed() == b.trimmed() ? 1.0 : 0.0;
    }
    int intersection = 0;
    for (const QString &g : ga) {
        if (gb.contains(g)) ++intersection;
    }
    const int uni = ga.size() + gb.size() - intersection;
    return uni > 0 ? static_cast<double>(intersection) / uni : 0.0;
}

QString MemoryExtractor::deriveKeywords(const QString &text)
{
    QStringList terms;
    QString current;
    const auto flush = [&]() {
        if (current.isEmpty()) return;
        if (current.size() <= 4) {
            terms.append(current);
        } else {
            // 全量 bigram（连续两字），保证"冰美式"能切出"冰美"
            for (int i = 0; i + 2 <= current.size(); ++i) {
                terms.append(current.mid(i, 2));
                if (terms.size() >= 12) break;
            }
        }
        current.clear();
    };

    for (const QChar &ch : text) {
        if (ch.isLetterOrNumber()) {
            current.append(ch);
        } else {
            flush();
        }
        if (terms.size() >= 12) break;
    }
    flush();

    QStringList unique;
    for (const QString &t : terms) {
        if (t.size() >= 2 && !unique.contains(t)) unique.append(t);
        if (unique.size() >= 8) break;
    }
    return unique.join(QLatin1Char(' '));
}
