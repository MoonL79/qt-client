#include "messagesyncclient.h"

#include <QDebug>
#include <QJsonArray>
#include <QJsonObject>
#include <QThread>
#include <QUuid>

#include <algorithm>

namespace {
constexpr const char *kTypeMessage = "MESSAGE";
constexpr const char *kActionPull = "PULL";
constexpr const char *kActionAck = "ACK";
constexpr const char *kActionSend = "SEND";
constexpr int kDefaultPullLimit = 100;
constexpr int kMaxPullLimit = 200;

// 实现 `jsonStringValue` 的核心逻辑。
QString jsonStringValue(const QJsonValue &value) {
  if (value.isString()) {
    return value.toString().trimmed();
  }
  if (value.isDouble()) {
    return QString::number(static_cast<qint64>(value.toDouble()));
  }
  return QString();
}

qint64 jsonInt64Value(const QJsonValue &value, qint64 defaultValue = 0) {
  if (value.isDouble()) {
    return static_cast<qint64>(value.toDouble(defaultValue));
  }
  if (value.isString()) {
    bool ok = false;
    const qint64 parsed = value.toString().trimmed().toLongLong(&ok);
    return ok ? parsed : defaultValue;
  }
  return defaultValue;
}

int jsonIntValue(const QJsonValue &value, int defaultValue = 0) {
  if (value.isDouble()) {
    return value.toInt(defaultValue);
  }
  if (value.isString()) {
    bool ok = false;
    const int parsed = value.toString().trimmed().toInt(&ok);
    return ok ? parsed : defaultValue;
  }
  return defaultValue;
}

// 实现 `messageEnvelopeOk` 的核心逻辑。
bool messageEnvelopeOk(const protocol::Envelope &envelope) {
  if (envelope.hasCode && envelope.code != 0) {
    return false;
  }
  if (envelope.hasOk) {
    return envelope.ok;
  }
  const QJsonValue okValue = envelope.data.value(QStringLiteral("ok"));
  if (okValue.isBool()) {
    return okValue.toBool();
  }
  return true;
}

// 实现 `envelopeErrorText` 的核心逻辑。
QString envelopeErrorText(const protocol::Envelope &envelope) {
  const QString envelopeMessage = envelope.message.trimmed();
  if (!envelopeMessage.isEmpty()) {
    return envelopeMessage;
  }
  const QString dataMessage =
      jsonStringValue(envelope.data.value(QStringLiteral("message")));
  if (!dataMessage.isEmpty()) {
    return dataMessage;
  }
  const QString error =
      jsonStringValue(envelope.data.value(QStringLiteral("error")));
  if (!error.isEmpty()) {
    return error;
  }
  const QString detail =
      jsonStringValue(envelope.data.value(QStringLiteral("detail")));
  if (!detail.isEmpty()) {
    return detail;
  }
  const int code = envelope.hasCode ? envelope.code : 0;
  return QStringLiteral("request failed, code=%1").arg(code);
}

// 解析消息object并生成内部结果。
bool parseMessageObject(const QJsonObject &messageObject,
                        const QString &requestId, ChatMessage *outMessage,
                        QString *error) {
  if (!outMessage) {
    if (error) {
      *error = QStringLiteral("internal error: out message is null");
    }
    return false;
  }

  ChatMessage message;
  message.requestId = requestId.trimmed();
  message.conversationId =
      jsonStringValue(messageObject.value(QStringLiteral("conversation_id")));
  message.messageId =
      jsonStringValue(messageObject.value(QStringLiteral("message_id")));
  message.seq = jsonInt64Value(messageObject.value(QStringLiteral("seq")), 0);
  message.sentAt =
      jsonStringValue(messageObject.value(QStringLiteral("sent_at")));
  message.senderUserId =
      jsonStringValue(messageObject.value(QStringLiteral("from_user_id")));
  message.senderNumericId =
      jsonStringValue(messageObject.value(QStringLiteral("from_numeric_id")));
  message.senderUsername =
      jsonStringValue(messageObject.value(QStringLiteral("from_username")));

  const QString messageKind =
      jsonStringValue(messageObject.value(QStringLiteral("message_kind")))
          .toLower();
  const QJsonObject fileObject =
      messageObject.value(QStringLiteral("file")).toObject();

  if (messageKind == QStringLiteral("file") || !fileObject.isEmpty()) {
    message.kind = ChatMessageKind::File;
    message.file.fileId =
        jsonStringValue(fileObject.value(QStringLiteral("file_id")));
    message.file.originalName =
        jsonStringValue(fileObject.value(QStringLiteral("original_name")));
    message.file.storedName =
        jsonStringValue(fileObject.value(QStringLiteral("stored_name")));
    message.file.sizeBytes =
        jsonInt64Value(fileObject.value(QStringLiteral("size_bytes")), -1);
    message.file.contentType =
        jsonStringValue(fileObject.value(QStringLiteral("content_type")));
    message.file.sha256 =
        jsonStringValue(fileObject.value(QStringLiteral("sha256")));
  } else {
    message.kind = ChatMessageKind::Text;
    message.text =
        jsonStringValue(messageObject.value(QStringLiteral("content")));
  }

  if (!message.isValid()) {
    if (error) {
      *error = QStringLiteral("message object missing required fields");
    }
    return false;
  }

  *outMessage = message;
  return true;
}
} // namespace

// 实现 `m_client` 的核心逻辑。
MessageSyncClient::MessageSyncClient(websocketclient *client, QObject *parent)
    : QObject(parent), m_client(client) {
  qRegisterMetaType<MessagePullResult>("MessagePullResult");
  qRegisterMetaType<MessageAckResult>("MessageAckResult");
  qRegisterMetaType<ChatMessage>("ChatMessage");
  qRegisterMetaType<QVector<ChatMessage>>("QVector<ChatMessage>");

  Q_ASSERT_X(thread() == QThread::currentThread(), "MessageSyncClient",
             "MessageSyncClient should run in the main event thread");

  if (!m_client) {
    qWarning() << "[MessageSync] init failed: websocket client is null";
    return;
  }

  connect(m_client, &websocketclient::textMessageReceived, this,
          &MessageSyncClient::onTextMessageReceived);
  connect(m_client, &websocketclient::disconnected, this,
          &MessageSyncClient::onDisconnected);
}

// 实现 `pullMessages` 的核心逻辑。
QString MessageSyncClient::pullMessages(const QString &conversationId,
                                        qint64 afterSeq, int limit) {
  const QString requestId = generateRequestId();
  const QString trimmedConversationId = conversationId.trimmed();
  if (trimmedConversationId.isEmpty()) {
    failRequest(requestId, QString::fromLatin1(kActionPull), 4001,
                QStringLiteral("conversation_id is required"));
    return requestId;
  }
  if (!m_client || !m_client->isConnected()) {
    failRequest(requestId, QString::fromLatin1(kActionPull), 500,
                QStringLiteral("websocket is not connected"));
    return requestId;
  }

  const int normalizedLimit =
      std::clamp(limit <= 0 ? kDefaultPullLimit : limit, 1, kMaxPullLimit);
  QJsonObject data;
  data.insert(QStringLiteral("conversation_id"), trimmedConversationId);
  data.insert(QStringLiteral("after_seq"),
              static_cast<double>(qMax<qint64>(afterSeq, 0)));
  data.insert(QStringLiteral("limit"), normalizedLimit);

  addPendingRequest(requestId, QString::fromLatin1(kActionPull));
  if (!sendMessagePayload(QString::fromLatin1(kActionPull), requestId, data)) {
    clearPendingRequest(requestId);
    failRequest(requestId, QString::fromLatin1(kActionPull), 500,
                QStringLiteral("websocket is not connected"));
  }
  return requestId;
}

// 实现 `acknowledgeUpToSeq` 的核心逻辑。
QString MessageSyncClient::acknowledgeUpToSeq(const QString &conversationId,
                                              qint64 upToSeq, bool delivered) {
  const QString requestId = generateRequestId();
  const QString trimmedConversationId = conversationId.trimmed();
  if (trimmedConversationId.isEmpty()) {
    failRequest(requestId, QString::fromLatin1(kActionAck), 4001,
                QStringLiteral("conversation_id is required"));
    return requestId;
  }
  if (upToSeq <= 0) {
    failRequest(requestId, QString::fromLatin1(kActionAck), 4001,
                QStringLiteral("up_to_seq must be greater than 0"));
    return requestId;
  }
  if (!m_client || !m_client->isConnected()) {
    failRequest(requestId, QString::fromLatin1(kActionAck), 500,
                QStringLiteral("websocket is not connected"));
    return requestId;
  }

  QJsonObject data;
  data.insert(QStringLiteral("conversation_id"), trimmedConversationId);
  data.insert(QStringLiteral("up_to_seq"), static_cast<double>(upToSeq));
  data.insert(QStringLiteral("delivered"), delivered);

  addPendingRequest(requestId, QString::fromLatin1(kActionAck));
  if (!sendMessagePayload(QString::fromLatin1(kActionAck), requestId, data)) {
    clearPendingRequest(requestId);
    failRequest(requestId, QString::fromLatin1(kActionAck), 500,
                QStringLiteral("websocket is not connected"));
  }
  return requestId;
}

// 解析incoming发送envelope并生成内部结果。
bool MessageSyncClient::parseIncomingSendEnvelope(
    const protocol::Envelope &envelope, ChatMessage *outMessage,
    QString *error) {
  if (envelope.type != QLatin1String(kTypeMessage) ||
      envelope.action != QLatin1String(kActionSend)) {
    if (error) {
      *error = QStringLiteral("envelope is not MESSAGE/SEND");
    }
    return false;
  }
  return parseMessageObject(envelope.data, envelope.requestId, outMessage, error);
}

// 解析pulled消息object并生成内部结果。
bool MessageSyncClient::parsePulledMessageObject(const QJsonObject &messageObject,
                                                 ChatMessage *outMessage,
                                                 QString *error) {
  return parseMessageObject(messageObject, QString(), outMessage, error);
}

// 响应文本消息接收事件。
void MessageSyncClient::onTextMessageReceived(const QString &message) {
  protocol::Envelope envelope;
  QString parseError;
  if (!protocol::parseEnvelope(message, &envelope, &parseError)) {
    return;
  }
  if (envelope.type != QLatin1String(kTypeMessage)) {
    return;
  }

  const QString requestId = envelope.requestId.trimmed();
  if (requestId.isEmpty() || !m_pendingRequests.contains(requestId)) {
    return;
  }

  const PendingRequest pending = m_pendingRequests.value(requestId);
  clearPendingRequest(requestId);

  const QString action = pending.action;
  if (!envelope.action.isEmpty() && envelope.action != action) {
    failRequest(requestId, action, 500,
                QStringLiteral("response action mismatch"));
    return;
  }

  const int code = envelope.hasCode ? envelope.code : 0;
  if (!messageEnvelopeOk(envelope)) {
    failRequest(requestId, action, code, envelopeErrorText(envelope));
    return;
  }

  if (action == QLatin1String(kActionPull)) {
    MessagePullResult result;
    QString error;
    if (!parsePullResult(envelope, &result, &error)) {
      failRequest(requestId, action, 500, error);
      return;
    }
    emit pullSucceeded(requestId, result);
    return;
  }

  if (action == QLatin1String(kActionAck)) {
    MessageAckResult result;
    QString error;
    if (!parseAckResult(envelope, &result, &error)) {
      failRequest(requestId, action, 500, error);
      return;
    }
    emit ackSucceeded(requestId, result);
    return;
  }

  failRequest(requestId, action, 500, QStringLiteral("unsupported action"));
}

// 响应已断开事件。
void MessageSyncClient::onDisconnected() {
  const auto requestIds = m_pendingRequests.keys();
  for (const QString &requestId : requestIds) {
    const QString action = m_pendingRequests.value(requestId).action;
    clearPendingRequest(requestId);
    failRequest(requestId, action, 500,
                QStringLiteral("websocket disconnected"));
  }
}

// 实现 `generateRequestId` 的核心逻辑。
QString MessageSyncClient::generateRequestId() const {
  return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

// 发送消息payload数据。
bool MessageSyncClient::sendMessagePayload(const QString &action,
                                           const QString &requestId,
                                           const QJsonObject &data) {
  if (!m_client || !m_client->isConnected()) {
    return false;
  }
  const QString payload = protocol::createRequest(
      QString::fromLatin1(kTypeMessage), action, data, requestId);
  m_client->sendTextMessage(payload);
  qInfo().noquote() << "[MessageSync] send action=" << action
                    << "request_id=" << requestId;
  return true;
}

// 实现 `addPendingRequest` 的核心逻辑。
void MessageSyncClient::addPendingRequest(const QString &requestId,
                                          const QString &action) {
  clearPendingRequest(requestId);

  auto *timer = new QTimer(this);
  timer->setSingleShot(true);
  connect(timer, &QTimer::timeout, this, [this, requestId, action]() {
    if (!m_pendingRequests.contains(requestId)) {
      return;
    }
    clearPendingRequest(requestId);
    failRequest(requestId, action, 500, QStringLiteral("request timeout"));
  });
  timer->start(kRequestTimeoutMs);

  PendingRequest pending;
  pending.action = action;
  pending.timer = timer;
  m_pendingRequests.insert(requestId, pending);
}

// 清理待处理请求状态。
void MessageSyncClient::clearPendingRequest(const QString &requestId) {
  auto it = m_pendingRequests.find(requestId);
  if (it == m_pendingRequests.end()) {
    return;
  }
  if (it->timer) {
    it->timer->stop();
    it->timer->deleteLater();
  }
  m_pendingRequests.erase(it);
}

// 实现 `failRequest` 的核心逻辑。
void MessageSyncClient::failRequest(const QString &requestId,
                                    const QString &action, int code,
                                    const QString &errorMessage) {
  qWarning().noquote() << "[MessageSync] action=" << action
                       << "request_id=" << requestId << "code=" << code
                       << "message=" << errorMessage;
  emit requestFailed(requestId, action, code, errorMessage);
}

// 解析拉取结果并生成内部结果。
bool MessageSyncClient::parsePullResult(const protocol::Envelope &envelope,
                                        MessagePullResult *outResult,
                                        QString *error) const {
  if (!outResult) {
    if (error) {
      *error = QStringLiteral("internal error: out pull result is null");
    }
    return false;
  }

  MessagePullResult result;
  result.conversationId =
      jsonStringValue(envelope.data.value(QStringLiteral("conversation_id")));
  if (result.conversationId.isEmpty()) {
    if (error) {
      *error = QStringLiteral("pull response missing conversation_id");
    }
    return false;
  }

  result.conversationType =
      jsonIntValue(envelope.data.value(QStringLiteral("conversation_type")), 0);
  result.pulledCount =
      jsonIntValue(envelope.data.value(QStringLiteral("pulled_count")), 0);
  result.hasMore =
      envelope.data.value(QStringLiteral("has_more")).toBool(false);
  result.nextAfterSeq =
      jsonInt64Value(envelope.data.value(QStringLiteral("next_after_seq")), 0);
  result.serverLastSeq =
      jsonInt64Value(envelope.data.value(QStringLiteral("server_last_seq")), 0);

  const QJsonValue messagesValue =
      envelope.data.value(QStringLiteral("messages"));
  if (!messagesValue.isArray()) {
    if (error) {
      *error = QStringLiteral("pull response messages is not array");
    }
    return false;
  }

  const QJsonArray messagesArray = messagesValue.toArray();
  result.messages.reserve(messagesArray.size());
  for (const QJsonValue &messageValue : messagesArray) {
    if (!messageValue.isObject()) {
      continue;
    }
    ChatMessage message;
    QString parseError;
    if (!parsePulledMessageObject(messageValue.toObject(), &message, &parseError)) {
      if (error) {
        *error = parseError;
      }
      return false;
    }
    result.messages.push_back(message);
  }

  std::sort(result.messages.begin(), result.messages.end(),
            [](const ChatMessage &left, const ChatMessage &right) {
              if (left.seq != right.seq) {
                return left.seq < right.seq;
              }
              return left.sentAt < right.sentAt;
            });
  *outResult = result;
  return true;
}

// 解析确认结果并生成内部结果。
bool MessageSyncClient::parseAckResult(const protocol::Envelope &envelope,
                                       MessageAckResult *outResult,
                                       QString *error) const {
  if (!outResult) {
    if (error) {
      *error = QStringLiteral("internal error: out ack result is null");
    }
    return false;
  }

  MessageAckResult result;
  result.conversationId =
      jsonStringValue(envelope.data.value(QStringLiteral("conversation_id")));
  if (result.conversationId.isEmpty()) {
    if (error) {
      *error = QStringLiteral("ack response missing conversation_id");
    }
    return false;
  }
  result.ackedUpToSeq =
      jsonInt64Value(envelope.data.value(QStringLiteral("acked_up_to_seq")), 0);
  if (result.ackedUpToSeq <= 0) {
    result.ackedUpToSeq =
        jsonInt64Value(envelope.data.value(QStringLiteral("up_to_seq")), 0);
  }
  result.affectedCount =
      jsonIntValue(envelope.data.value(QStringLiteral("affected_count")), 0);
  *outResult = result;
  return true;
}
