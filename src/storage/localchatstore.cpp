#include "localchatstore.h"

#include <QDir>
#include <QFileInfo>
#include <QDebug>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QUuid>

namespace {
// 实现 `normalizedText` 的核心逻辑。
QString normalizedText(const QString &value) {
  return value.isNull() ? QString("") : value;
}

// 实现 `sqlErrorText` 的核心逻辑。
QString sqlErrorText(const QSqlQuery &query) {
  const QString text = query.lastError().text().trimmed();
  return text.isEmpty() ? QStringLiteral("unknown sqlite error") : text;
}

// 实现 `sqlErrorText` 的核心逻辑。
QString sqlErrorText(const QSqlDatabase &database) {
  const QString text = database.lastError().text().trimmed();
  return text.isEmpty() ? QStringLiteral("unknown sqlite error") : text;
}

// 实现 `bindMessageValues` 的核心逻辑。
void bindMessageValues(QSqlQuery *query, const ChatMessage &message,
                       const QString &ownerUserId, const QString &dedupeKey) {
  query->bindValue(QStringLiteral(":dedupe_key"), normalizedText(dedupeKey));
  query->bindValue(QStringLiteral(":local_id"),
                   normalizedText(message.localId.trimmed()));
  query->bindValue(QStringLiteral(":request_id"),
                   normalizedText(message.requestId.trimmed()));
  query->bindValue(QStringLiteral(":message_id"),
                   normalizedText(message.messageId.trimmed()));
  query->bindValue(QStringLiteral(":seq"), message.seq);
  query->bindValue(QStringLiteral(":sent_at"),
                   normalizedText(message.sentAt.trimmed()));
  query->bindValue(QStringLiteral(":sender_user_id"),
                   normalizedText(message.senderUserId.trimmed()));
  query->bindValue(QStringLiteral(":sender_numeric_id"),
                   normalizedText(message.senderNumericId.trimmed()));
  query->bindValue(QStringLiteral(":sender_username"),
                   normalizedText(message.senderUsername.trimmed()));
  query->bindValue(QStringLiteral(":message_kind"),
                   message.kind == ChatMessageKind::File ? 1 : 0);
  query->bindValue(QStringLiteral(":text_content"), normalizedText(message.text));
  query->bindValue(QStringLiteral(":file_id"),
                   normalizedText(message.file.fileId.trimmed()));
  query->bindValue(QStringLiteral(":file_original_name"),
                   normalizedText(message.file.originalName.trimmed()));
  query->bindValue(QStringLiteral(":file_stored_name"),
                   normalizedText(message.file.storedName.trimmed()));
  query->bindValue(QStringLiteral(":file_size_bytes"), message.file.sizeBytes);
  query->bindValue(QStringLiteral(":file_content_type"),
                   normalizedText(message.file.contentType.trimmed()));
  query->bindValue(QStringLiteral(":file_sha256"),
                   normalizedText(message.file.sha256.trimmed()));
  query->bindValue(QStringLiteral(":owner_user_id"), normalizedText(ownerUserId));
  query->bindValue(QStringLiteral(":conversation_id"),
                   normalizedText(message.conversationId.trimmed()));
}
} // namespace

// 实现 `toString` 的核心逻辑。
LocalChatStore::LocalChatStore()
    : m_connectionName(
          QStringLiteral("local-chat-store-%1")
              .arg(QUuid::createUuid().toString(QUuid::WithoutBraces))) {}

// 清理当前user状态。
LocalChatStore::~LocalChatStore() { clearCurrentUser(); }

// 设置当前user值。
bool LocalChatStore::setCurrentUser(const QString &currentUserId, QString *error) {
  const QString trimmedUserId = currentUserId.trimmed();
  if (trimmedUserId.isEmpty()) {
    if (error) {
      *error = QStringLiteral("current user id is empty");
    }
    return false;
  }
  if (m_currentUserId == trimmedUserId && m_database.isOpen()) {
    return true;
  }

  clearCurrentUser();
  if (!openDatabase(trimmedUserId, error)) {
    return false;
  }
  if (!ensureSchema(error)) {
    clearCurrentUser();
    return false;
  }

  m_currentUserId = trimmedUserId;
  qInfo().noquote() << "[LocalChatStore] initialized user_id=" << m_currentUserId
                    << "db_path=" << m_database.databaseName();
  return true;
}

// 清理当前user状态。
void LocalChatStore::clearCurrentUser() {
  if (m_database.isValid()) {
    m_database.close();
  }
  m_database = QSqlDatabase();
  if (!m_connectionName.isEmpty() && QSqlDatabase::contains(m_connectionName)) {
    QSqlDatabase::removeDatabase(m_connectionName);
  }
  m_currentUserId.clear();
}

// 保存消息数据。
bool LocalChatStore::saveMessage(const ChatMessage &message, QString *error) {
  if (!message.isValid()) {
    if (error) {
      *error = QStringLiteral("message is invalid");
    }
    return false;
  }
  if (!isReady(error)) {
    qWarning().noquote() << "[LocalChatStore] save skipped: store not ready"
                         << "conversation_id=" << message.conversationId
                         << "request_id=" << message.requestId
                         << "message_id=" << message.messageId;
    return false;
  }

  if (updateExistingMessage(message, error)) {
    QString cleanupError;
    if (!cleanupDuplicateRequestRows(message, &cleanupError)) {
      qWarning().noquote() << "[LocalChatStore] duplicate request cleanup failed"
                           << "conversation_id=" << message.conversationId
                           << "request_id=" << message.requestId
                           << "message_id=" << message.messageId
                           << "error=" << cleanupError;
    }
    qInfo().noquote() << "[LocalChatStore] updated existing message"
                      << "conversation_id=" << message.conversationId
                      << "request_id=" << message.requestId
                      << "message_id=" << message.messageId
                      << "seq=" << message.seq;
    return true;
  }
  if (error && !error->isEmpty()) {
    qWarning().noquote() << "[LocalChatStore] update existing failed"
                         << "conversation_id=" << message.conversationId
                         << "request_id=" << message.requestId
                         << "message_id=" << message.messageId
                         << "error=" << *error;
    return false;
  }

  QSqlQuery query(m_database);
  query.prepare(QStringLiteral(
      "INSERT INTO chat_messages ("
      " owner_user_id, conversation_id, dedupe_key, local_id, request_id, message_id,"
      " seq, sent_at, sender_user_id, sender_numeric_id, sender_username,"
      " message_kind, text_content, file_id, file_original_name, file_stored_name,"
      " file_size_bytes, file_content_type, file_sha256"
      ") VALUES ("
      " :owner_user_id, :conversation_id, :dedupe_key, :local_id, :request_id,"
      " :message_id, :seq, :sent_at, :sender_user_id, :sender_numeric_id,"
      " :sender_username, :message_kind, :text_content, :file_id,"
      " :file_original_name, :file_stored_name, :file_size_bytes,"
      " :file_content_type, :file_sha256"
      ")"
      " ON CONFLICT(dedupe_key) DO UPDATE SET"
      " local_id=excluded.local_id,"
      " request_id=excluded.request_id,"
      " message_id=excluded.message_id,"
      " seq=excluded.seq,"
      " sent_at=excluded.sent_at,"
      " sender_user_id=excluded.sender_user_id,"
      " sender_numeric_id=excluded.sender_numeric_id,"
      " sender_username=excluded.sender_username,"
      " message_kind=excluded.message_kind,"
      " text_content=excluded.text_content,"
      " file_id=excluded.file_id,"
      " file_original_name=excluded.file_original_name,"
      " file_stored_name=excluded.file_stored_name,"
      " file_size_bytes=excluded.file_size_bytes,"
      " file_content_type=excluded.file_content_type,"
      " file_sha256=excluded.file_sha256"));

  bindMessageValues(&query, message, m_currentUserId, dedupeKeyForMessage(message));
  if (!query.exec()) {
    if (error) {
      *error = sqlErrorText(query);
    }
    qWarning().noquote() << "[LocalChatStore] insert failed"
                         << "conversation_id=" << message.conversationId
                         << "request_id=" << message.requestId
                         << "message_id=" << message.messageId
                         << "error=" << (error ? *error : QString());
    return false;
  }

  qInfo().noquote() << "[LocalChatStore] inserted message"
                    << "conversation_id=" << message.conversationId
                    << "request_id=" << message.requestId
                    << "message_id=" << message.messageId
                    << "kind=" << (message.kind == ChatMessageKind::File ? "file" : "text")
                    << "sent_at=" << message.sentAt;

  return true;
}

// 更新existing消息状态。
bool LocalChatStore::updateExistingMessage(const ChatMessage &message,
                                           QString *error) {
  const QString dedupeKey = dedupeKeyForMessage(message);

  if (message.seq > 0) {
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "UPDATE chat_messages SET"
        " dedupe_key = :dedupe_key,"
        " local_id = :local_id,"
        " request_id = :request_id,"
        " message_id = :message_id,"
        " seq = :seq,"
        " sent_at = :sent_at,"
        " sender_user_id = :sender_user_id,"
        " sender_numeric_id = :sender_numeric_id,"
        " sender_username = :sender_username,"
        " message_kind = :message_kind,"
        " text_content = :text_content,"
        " file_id = :file_id,"
        " file_original_name = :file_original_name,"
        " file_stored_name = :file_stored_name,"
        " file_size_bytes = :file_size_bytes,"
        " file_content_type = :file_content_type,"
        " file_sha256 = :file_sha256"
        " WHERE owner_user_id = :owner_user_id"
        "   AND conversation_id = :conversation_id"
        "   AND seq = :match_seq"));
    bindMessageValues(&query, message, m_currentUserId, dedupeKey);
    query.bindValue(QStringLiteral(":match_seq"), message.seq);
    if (!query.exec()) {
      if (error) {
        *error = sqlErrorText(query);
      }
      return false;
    }
    if (query.numRowsAffected() > 0) {
      if (error) {
        error->clear();
      }
      return true;
    }
  }

  if (!message.messageId.trimmed().isEmpty()) {
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "UPDATE chat_messages SET"
        " dedupe_key = :dedupe_key,"
        " local_id = :local_id,"
        " request_id = :request_id,"
        " message_id = :message_id,"
        " seq = :seq,"
        " sent_at = :sent_at,"
        " sender_user_id = :sender_user_id,"
        " sender_numeric_id = :sender_numeric_id,"
        " sender_username = :sender_username,"
        " message_kind = :message_kind,"
        " text_content = :text_content,"
        " file_id = :file_id,"
        " file_original_name = :file_original_name,"
        " file_stored_name = :file_stored_name,"
        " file_size_bytes = :file_size_bytes,"
        " file_content_type = :file_content_type,"
        " file_sha256 = :file_sha256"
        " WHERE owner_user_id = :owner_user_id"
        "   AND conversation_id = :conversation_id"
        "   AND message_id = :match_message_id"));
    bindMessageValues(&query, message, m_currentUserId, dedupeKey);
    query.bindValue(QStringLiteral(":match_message_id"),
                    normalizedText(message.messageId.trimmed()));
    if (!query.exec()) {
      if (error) {
        *error = sqlErrorText(query);
      }
      return false;
    }
    if (query.numRowsAffected() > 0) {
      if (error) {
        error->clear();
      }
      return true;
    }
  }

  if (!message.requestId.trimmed().isEmpty()) {
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "UPDATE chat_messages SET"
        " dedupe_key = :dedupe_key,"
        " local_id = :local_id,"
        " request_id = :request_id,"
        " message_id = :message_id,"
        " seq = :seq,"
        " sent_at = :sent_at,"
        " sender_user_id = :sender_user_id,"
        " sender_numeric_id = :sender_numeric_id,"
        " sender_username = :sender_username,"
        " message_kind = :message_kind,"
        " text_content = :text_content,"
        " file_id = :file_id,"
        " file_original_name = :file_original_name,"
        " file_stored_name = :file_stored_name,"
        " file_size_bytes = :file_size_bytes,"
        " file_content_type = :file_content_type,"
        " file_sha256 = :file_sha256"
        " WHERE owner_user_id = :owner_user_id"
        "   AND conversation_id = :conversation_id"
        "   AND request_id = :match_request_id"));
    bindMessageValues(&query, message, m_currentUserId, dedupeKey);
    query.bindValue(QStringLiteral(":match_request_id"),
                    normalizedText(message.requestId.trimmed()));
    if (!query.exec()) {
      if (error) {
        *error = sqlErrorText(query);
      }
      return false;
    }
    if (query.numRowsAffected() > 0) {
      if (error) {
        error->clear();
      }
      return true;
    }
  }

  if (error) {
    error->clear();
  }
  return false;
}

// 实现 `cleanupDuplicateRequestRows` 的核心逻辑。
bool LocalChatStore::cleanupDuplicateRequestRows(const ChatMessage &message,
                                                 QString *error) {
  const QString requestId = message.requestId.trimmed();
  if (requestId.isEmpty()) {
    if (error) {
      error->clear();
    }
    return true;
  }

  QSqlQuery query(m_database);
  query.prepare(QStringLiteral(
      "DELETE FROM chat_messages"
      " WHERE owner_user_id = :owner_user_id"
      "   AND conversation_id = :conversation_id"
      "   AND request_id = :request_id"
      "   AND dedupe_key <> :dedupe_key"));
  query.bindValue(QStringLiteral(":owner_user_id"), normalizedText(m_currentUserId));
  query.bindValue(QStringLiteral(":conversation_id"),
                  normalizedText(message.conversationId.trimmed()));
  query.bindValue(QStringLiteral(":request_id"), normalizedText(requestId));
  query.bindValue(QStringLiteral(":dedupe_key"),
                  normalizedText(dedupeKeyForMessage(message)));
  if (!query.exec()) {
    if (error) {
      *error = sqlErrorText(query);
    }
    return false;
  }
  if (error) {
    error->clear();
  }
  return true;
}

// 加载消息数据。
QVector<ChatMessage> LocalChatStore::loadMessages(const QString &conversationId, int limit,
                                                  QString *error) const {
  QVector<ChatMessage> messages;
  if (!isReady(error)) {
    qWarning().noquote() << "[LocalChatStore] load skipped: store not ready"
                         << "conversation_id=" << conversationId;
    return messages;
  }

  const QString trimmedConversationId = conversationId.trimmed();
  if (trimmedConversationId.isEmpty()) {
    if (error) {
      *error = QStringLiteral("conversation id is empty");
    }
    return messages;
  }

  QSqlQuery query(m_database);
  query.prepare(QStringLiteral(
      "SELECT local_id, request_id, message_id, seq, sent_at, sender_user_id,"
      " sender_numeric_id, sender_username, message_kind, text_content, file_id,"
      " file_original_name, file_stored_name, file_size_bytes, file_content_type,"
      " file_sha256"
      " FROM ("
      "   SELECT local_id, request_id, message_id, seq, sent_at, sender_user_id,"
      "          sender_numeric_id, sender_username, message_kind, text_content,"
      "          file_id, file_original_name, file_stored_name, file_size_bytes,"
      "          file_content_type, file_sha256, id AS message_row_id"
      "   FROM chat_messages"
      "   WHERE owner_user_id = :owner_user_id AND conversation_id = :conversation_id"
      "   ORDER BY"
      "     CASE WHEN seq <= 0 THEN 1 ELSE 0 END DESC,"
      "     seq DESC,"
      "     CASE WHEN sent_at = '' THEN 1 ELSE 0 END ASC,"
      "     sent_at DESC,"
      "     id DESC"
      "   LIMIT :limit"
      " ) recent_messages"
      " ORDER BY"
      "   CASE WHEN seq <= 0 THEN 1 ELSE 0 END ASC,"
      "   seq ASC,"
      "   CASE WHEN sent_at = '' THEN 1 ELSE 0 END ASC,"
      "   sent_at ASC,"
      "   message_row_id ASC"));
  query.bindValue(QStringLiteral(":owner_user_id"), normalizedText(m_currentUserId));
  query.bindValue(QStringLiteral(":conversation_id"), trimmedConversationId);
  query.bindValue(QStringLiteral(":limit"), qMax(limit, 1));
  if (!query.exec()) {
    if (error) {
      *error = sqlErrorText(query);
    }
    qWarning().noquote() << "[LocalChatStore] load query failed"
                         << "conversation_id=" << trimmedConversationId
                         << "error=" << (error ? *error : QString());
    return messages;
  }

  while (query.next()) {
    ChatMessage message;
    message.localId = query.value(0).toString().trimmed();
    message.requestId = query.value(1).toString().trimmed();
    message.messageId = query.value(2).toString().trimmed();
    message.seq = query.value(3).toLongLong();
    message.sentAt = query.value(4).toString().trimmed();
    message.senderUserId = query.value(5).toString().trimmed();
    message.senderNumericId = query.value(6).toString().trimmed();
    message.senderUsername = query.value(7).toString().trimmed();
    message.kind = query.value(8).toInt() == 1 ? ChatMessageKind::File
                                               : ChatMessageKind::Text;
    message.text = query.value(9).toString();
    message.file.fileId = query.value(10).toString().trimmed();
    message.file.originalName = query.value(11).toString().trimmed();
    message.file.storedName = query.value(12).toString().trimmed();
    message.file.sizeBytes = query.value(13).toLongLong();
    message.file.contentType = query.value(14).toString().trimmed();
    message.file.sha256 = query.value(15).toString().trimmed();
    message.conversationId = trimmedConversationId;
    if (message.isValid()) {
      messages.push_back(message);
    }
  }

  qInfo().noquote() << "[LocalChatStore] loaded messages"
                    << "conversation_id=" << trimmedConversationId
                    << "count=" << messages.size()
                    << "limit=" << qMax(limit, 1);

  return messages;
}

// 实现 `lastSeqForConversation` 的核心逻辑。
qint64 LocalChatStore::lastSeqForConversation(const QString &conversationId,
                                              QString *error) const {
  if (!isReady(error)) {
    qWarning().noquote() << "[LocalChatStore] last seq skipped: store not ready"
                         << "conversation_id=" << conversationId;
    return 0;
  }

  const QString trimmedConversationId = conversationId.trimmed();
  if (trimmedConversationId.isEmpty()) {
    if (error) {
      *error = QStringLiteral("conversation id is empty");
    }
    return 0;
  }

  QSqlQuery query(m_database);
  query.prepare(QStringLiteral(
      "SELECT COALESCE(MAX(seq), 0)"
      " FROM chat_messages"
      " WHERE owner_user_id = :owner_user_id"
      "   AND conversation_id = :conversation_id"));
  query.bindValue(QStringLiteral(":owner_user_id"), normalizedText(m_currentUserId));
  query.bindValue(QStringLiteral(":conversation_id"), trimmedConversationId);
  if (!query.exec()) {
    if (error) {
      *error = sqlErrorText(query);
    }
    qWarning().noquote() << "[LocalChatStore] last seq query failed"
                         << "conversation_id=" << trimmedConversationId
                         << "error=" << (error ? *error : QString());
    return 0;
  }
  if (!query.next()) {
    if (error) {
      error->clear();
    }
    return 0;
  }
  if (error) {
    error->clear();
  }
  return query.value(0).toLongLong();
}

// 打开database资源或连接。
bool LocalChatStore::openDatabase(const QString &currentUserId, QString *error) {
  const QString databasePath = databasePathForUser(currentUserId);
  QFileInfo info(databasePath);
  QDir directory = info.dir();
  if (!directory.exists() && !directory.mkpath(QStringLiteral("."))) {
    if (error) {
      *error = QStringLiteral("cannot create database directory");
    }
    return false;
  }

  m_database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
  m_database.setDatabaseName(databasePath);
  if (!m_database.open()) {
    if (error) {
      *error = sqlErrorText(m_database);
    }
    return false;
  }

  QSqlQuery pragmaQuery(m_database);
  pragmaQuery.exec(QStringLiteral("PRAGMA journal_mode=WAL"));
  pragmaQuery.exec(QStringLiteral("PRAGMA synchronous=NORMAL"));
  return true;
}

// 实现 `ensureSchema` 的核心逻辑。
bool LocalChatStore::ensureSchema(QString *error) {
  QSqlQuery query(m_database);
  if (!query.exec(QStringLiteral(
          "CREATE TABLE IF NOT EXISTS chat_messages ("
          " id INTEGER PRIMARY KEY AUTOINCREMENT,"
          " owner_user_id TEXT NOT NULL,"
          " conversation_id TEXT NOT NULL,"
          " dedupe_key TEXT NOT NULL UNIQUE,"
          " local_id TEXT NOT NULL DEFAULT '',"
          " request_id TEXT NOT NULL DEFAULT '',"
          " message_id TEXT NOT NULL DEFAULT '',"
          " seq INTEGER NOT NULL DEFAULT 0,"
          " sent_at TEXT NOT NULL DEFAULT '',"
          " sender_user_id TEXT NOT NULL DEFAULT '',"
          " sender_numeric_id TEXT NOT NULL DEFAULT '',"
          " sender_username TEXT NOT NULL DEFAULT '',"
          " message_kind INTEGER NOT NULL DEFAULT 0,"
          " text_content TEXT NOT NULL DEFAULT '',"
          " file_id TEXT NOT NULL DEFAULT '',"
          " file_original_name TEXT NOT NULL DEFAULT '',"
          " file_stored_name TEXT NOT NULL DEFAULT '',"
          " file_size_bytes INTEGER NOT NULL DEFAULT 0,"
          " file_content_type TEXT NOT NULL DEFAULT '',"
          " file_sha256 TEXT NOT NULL DEFAULT ''"
          ")"))) {
    if (error) {
      *error = sqlErrorText(query);
    }
    return false;
  }

  if (!query.exec(QStringLiteral(
          "CREATE INDEX IF NOT EXISTS idx_chat_messages_owner_conversation_sent_at"
          " ON chat_messages(owner_user_id, conversation_id, sent_at, id)"))) {
    if (error) {
      *error = sqlErrorText(query);
    }
    return false;
  }

  if (!query.exec(QStringLiteral(
          "CREATE INDEX IF NOT EXISTS idx_chat_messages_owner_conversation_seq"
          " ON chat_messages(owner_user_id, conversation_id, seq)"))) {
    if (error) {
      *error = sqlErrorText(query);
    }
    return false;
  }

  if (!query.exec(QStringLiteral(
          "CREATE INDEX IF NOT EXISTS idx_chat_messages_owner_conversation_message_id"
          " ON chat_messages(owner_user_id, conversation_id, message_id)"))) {
    if (error) {
      *error = sqlErrorText(query);
    }
    return false;
  }

  return true;
}

// 实现 `databasePathForUser` 的核心逻辑。
QString LocalChatStore::databasePathForUser(const QString &currentUserId) const {
  QString basePath =
      QStandardPaths::writableLocation(QStandardPaths::AppDataLocation).trimmed();
  if (basePath.isEmpty()) {
    basePath = QDir::homePath() + QStringLiteral("/.qt-client");
  }
  return QDir(basePath)
      .filePath(QStringLiteral("chat-history/user-%1.sqlite3")
                    .arg(currentUserId.trimmed()));
}

// 实现 `dedupeKeyForMessage` 的核心逻辑。
QString LocalChatStore::dedupeKeyForMessage(const ChatMessage &message) const {
  const QString conversationId = message.conversationId.trimmed();
  if (message.seq > 0) {
    return QStringLiteral("%1|%2|seq|%3")
        .arg(m_currentUserId, conversationId, QString::number(message.seq));
  }
  if (!message.messageId.trimmed().isEmpty()) {
    return QStringLiteral("%1|%2|message|%3")
        .arg(m_currentUserId, conversationId, message.messageId.trimmed());
  }
  if (!message.requestId.trimmed().isEmpty()) {
    return QStringLiteral("%1|%2|request|%3")
        .arg(m_currentUserId, conversationId, message.requestId.trimmed());
  }
  if (!message.localId.trimmed().isEmpty()) {
    return QStringLiteral("%1|%2|local|%3")
        .arg(m_currentUserId, conversationId, message.localId.trimmed());
  }
  if (message.kind == ChatMessageKind::File) {
    return QStringLiteral("%1|%2|file|%3|%4")
        .arg(m_currentUserId, conversationId, message.file.fileId.trimmed(),
             message.sentAt.trimmed());
  }
  return QStringLiteral("%1|%2|text|%3|%4|%5")
      .arg(m_currentUserId, conversationId, message.senderUserId.trimmed(),
           message.sentAt.trimmed(), message.text.trimmed());
}

// 判断ready条件是否满足。
bool LocalChatStore::isReady(QString *error) const {
  if (m_currentUserId.isEmpty() || !m_database.isValid() || !m_database.isOpen()) {
    if (error) {
      *error = QStringLiteral("local chat store is not initialized");
    }
    return false;
  }
  return true;
}





