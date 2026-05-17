#include "chatfileservice.h"

#include "usersession.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHttpMultiPart>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QMimeDatabase>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QThread>
#include <QUuid>

namespace {
constexpr const char *kTypeMessage = "MESSAGE";
constexpr const char *kActionSend = "SEND";
constexpr const char *kMessageKindFile = "file";
constexpr const char *kUploadAction = "UPLOAD_FILE";
constexpr const char *kDownloadAction = "DOWNLOAD_FILE";
constexpr const char *kDefaultHttpHost = "192.168.14.133";
constexpr int kDefaultHttpPort = 18080;
constexpr const char *kHttpUrlEnv = "QT_SERVER_HTTP_URL";
constexpr const char *kHttpHostEnv = "QT_SERVER_HTTP_HOST";
constexpr const char *kHttpPortEnv = "QT_SERVER_HTTP_PORT";
constexpr const char *kStaticHostEnv = "QT_SERVER_STATIC_HOST";
constexpr const char *kStaticPortEnv = "QT_SERVER_STATIC_PORT";

QString jsonStringValue(const QJsonObject &obj, const char *key) {
  const QJsonValue value = obj.value(QLatin1String(key));
  if (value.isString()) {
    return value.toString().trimmed();
  }
  if (value.isDouble()) {
    return QString::number(static_cast<qint64>(value.toDouble()));
  }
  return QString();
}

qint64 jsonInt64Value(const QJsonObject &obj, const char *key,
                      qint64 defaultValue = 0) {
  const QJsonValue value = obj.value(QLatin1String(key));
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

QString extractMessage(const QJsonObject &obj, const QString &fallback = QString()) {
  const QString message = jsonStringValue(obj, "message");
  if (!message.isEmpty()) {
    return message;
  }
  const QString error = jsonStringValue(obj, "error");
  if (!error.isEmpty()) {
    return error;
  }
  const QString detail = jsonStringValue(obj, "detail");
  if (!detail.isEmpty()) {
    return detail;
  }
  return fallback.trimmed();
}

int extractCode(const QJsonObject &obj, int defaultValue = 0) {
  const QJsonValue value = obj.value(QStringLiteral("code"));
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

bool hasExplicitCode(const QJsonObject &obj) {
  const QJsonValue value = obj.value(QStringLiteral("code"));
  return value.isDouble() || value.isString();
}

QString resolveHttpHost(const websocketclient *client) {
  QString host = qEnvironmentVariable(kHttpHostEnv).trimmed();
  if (host.isEmpty()) {
    host = qEnvironmentVariable(kStaticHostEnv).trimmed();
  }
  if (host.isEmpty() && client) {
    const QUrl wsUrl = client->url();
    if (wsUrl.isValid()) {
      host = wsUrl.host().trimmed();
    }
  }
  if (host.isEmpty()) {
    host = QString::fromLatin1(kDefaultHttpHost);
  }
  return host;
}

int resolveHttpPort() {
  bool ok = false;
  int port = qEnvironmentVariableIntValue(kHttpPortEnv, &ok);
  if (!ok || port <= 0 || port > 65535) {
    port = qEnvironmentVariableIntValue(kStaticPortEnv, &ok);
  }
  if (!ok || port <= 0 || port > 65535) {
    port = kDefaultHttpPort;
  }
  return port;
}
} // namespace

ChatFileService::ChatFileService(websocketclient *client, QObject *parent)
    : QObject(parent), m_client(client) {
  qRegisterMetaType<LocalChatFileDescriptor>("LocalChatFileDescriptor");
  qRegisterMetaType<ChatFileUploadResult>("ChatFileUploadResult");
  qRegisterMetaType<ChatFileContent>("ChatFileContent");
  qRegisterMetaType<ChatFileDownloadTask>("ChatFileDownloadTask");
  qRegisterMetaType<ChatMessage>("ChatMessage");
  qRegisterMetaType<QVector<ChatMessage>>("QVector<ChatMessage>");

  Q_ASSERT_X(thread() == QThread::currentThread(), "ChatFileService",
             "ChatFileService should run in the main event thread");

  if (!m_client) {
    qWarning() << "[ChatFileService] init without websocket client; realtime parsing is disabled";
    return;
  }

  connect(m_client, &websocketclient::textMessageReceived, this,
          &ChatFileService::onTextMessageReceived);
  connect(m_client, &websocketclient::disconnected, this,
          &ChatFileService::onDisconnected);
}

QString ChatFileService::uploadFile(const QString &conversationId,
                                    const QString &localFilePath,
                                    const QString &currentUserId,
                                    const QString &token,
                                    const QString &tokenType) {
  const QString requestId = QUuid::createUuid().toString(QUuid::WithoutBraces);
  const QString trimmedConversationId = conversationId.trimmed();
  const QString trimmedPath = localFilePath.trimmed();
  const QString trimmedUserId = currentUserId.trimmed();
  const QString action = QString::fromLatin1(kUploadAction);
  if (trimmedConversationId.isEmpty()) {
    failRequest(requestId, action, 400, QStringLiteral("conversation_id is required"));
    return requestId;
  }
  if (trimmedUserId.isEmpty()) {
    failRequest(requestId, action, 400, QStringLiteral("current user id is required"));
    return requestId;
  }
  if (trimmedPath.isEmpty()) {
    failRequest(requestId, action, 400, QStringLiteral("local file path is required"));
    return requestId;
  }

  QFileInfo fileInfo(trimmedPath);
  if (!fileInfo.exists() || !fileInfo.isFile()) {
    failRequest(requestId, action, 404, QStringLiteral("local file does not exist"));
    return requestId;
  }

  QFile *file = new QFile(trimmedPath);
  if (!file->open(QIODevice::ReadOnly)) {
    file->deleteLater();
    failRequest(requestId, action, 400, QStringLiteral("local file cannot be opened"));
    return requestId;
  }

  const QString authHeader = resolveAuthorizationHeader(token, tokenType);
  if (authHeader.isEmpty()) {
    file->deleteLater();
    QString error;
    if (token.trimmed().isEmpty()) {
      validateSessionToken(&error);
    } else {
      error = QStringLiteral("token type is missing");
    }
    failRequest(requestId, action, 401, error);
    return requestId;
  }

  const QUrl uploadUrl = buildUploadUrl();
  if (!uploadUrl.isValid()) {
    file->deleteLater();
    failRequest(requestId, action, 500, QStringLiteral("upload endpoint is invalid"));
    return requestId;
  }

  QHttpMultiPart *multiPart = new QHttpMultiPart(QHttpMultiPart::FormDataType);

  QHttpPart userIdPart;
  userIdPart.setHeader(QNetworkRequest::ContentDispositionHeader,
                       QVariant("form-data; name=\"user_id\""));
  userIdPart.setBody(trimmedUserId.toUtf8());
  multiPart->append(userIdPart);

  QHttpPart conversationIdPart;
  conversationIdPart.setHeader(QNetworkRequest::ContentDispositionHeader,
                               QVariant("form-data; name=\"conversation_id\""));
  conversationIdPart.setBody(trimmedConversationId.toUtf8());
  multiPart->append(conversationIdPart);

  QMimeDatabase mimeDb;
  const QString contentType = mimeDb.mimeTypeForFile(fileInfo).name().trimmed().isEmpty()
                                  ? QStringLiteral("application/octet-stream")
                                  : mimeDb.mimeTypeForFile(fileInfo).name().trimmed();

  QHttpPart filePart;
  filePart.setHeader(QNetworkRequest::ContentTypeHeader, QVariant(contentType));
  filePart.setHeader(QNetworkRequest::ContentDispositionHeader,
                     QVariant(QStringLiteral("form-data; name=\"file\"; filename=\"%1\"")
                                  .arg(fileInfo.fileName())));
  filePart.setBodyDevice(file);
  file->setParent(multiPart);
  multiPart->append(filePart);

  QNetworkRequest request(uploadUrl);
  request.setRawHeader("Authorization", authHeader.toUtf8());
  request.setTransferTimeout(30000);

  QNetworkReply *reply = m_networkManager.post(request, multiPart);
  multiPart->setParent(reply);

  PendingUpload pending;
  pending.action = action;
  pending.filePath = trimmedPath;
  pending.reply = reply;
  m_pendingUploads.insert(requestId, pending);

  connect(reply, &QNetworkReply::uploadProgress, this,
          [this, requestId](qint64 sent, qint64 total) {
            emit uploadProgress(requestId, sent, total);
          });
  connect(reply, &QNetworkReply::finished, this, [this, requestId]() {
    auto it = m_pendingUploads.find(requestId);
    if (it == m_pendingUploads.end()) {
      return;
    }

    QNetworkReply *reply = it->reply.data();
    const QString action = it->action;
    m_pendingUploads.erase(it);
    if (!reply) {
      failRequest(requestId, action, 500, QStringLiteral("upload reply missing"));
      return;
    }

    const int httpCode =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray body = reply->readAll();
    if (reply->error() != QNetworkReply::NoError) {
      const QString message =
          httpCode == 401 ? QStringLiteral("token missing or expired")
                          : QStringLiteral("upload failed: %1")
                                .arg(reply->errorString().trimmed());
      failRequest(requestId, action, httpCode == 0 ? 500 : httpCode, message);
      reply->deleteLater();
      return;
    }

    QString error;
    ChatFileUploadResult result =
        parseUploadResponse(requestId, httpCode, body, &error);
    if (!error.isEmpty()) {
      failRequest(requestId, action, result.code >= 0 ? result.code : httpCode,
                  error);
      reply->deleteLater();
      return;
    }

    emit uploadFinished(requestId, result);
    reply->deleteLater();
  });

  return requestId;
}

QString ChatFileService::sendFileMessage(const QString &conversationId,
                                         const ChatFileUploadResult &uploadResult) {
  const QString requestId = QUuid::createUuid().toString(QUuid::WithoutBraces);
  const QString trimmedConversationId = conversationId.trimmed();
  const QString action = QString::fromLatin1(kActionSend);
  if (trimmedConversationId.isEmpty()) {
    failRequest(requestId, action, 400, QStringLiteral("conversation_id is required"));
    return requestId;
  }
  if (!uploadResult.hasRequiredFields()) {
    failRequest(requestId, action, 400, QStringLiteral("upload result is incomplete"));
    return requestId;
  }
  if (!m_client || !m_client->isConnected()) {
    failRequest(requestId, action, 500, QStringLiteral("websocket is not connected"));
    return requestId;
  }

  QJsonObject file;
  file.insert(QStringLiteral("file_id"), uploadResult.fileId);
  file.insert(QStringLiteral("original_name"), uploadResult.originalName);
  file.insert(QStringLiteral("content_type"), uploadResult.contentType);
  file.insert(QStringLiteral("sha256"), uploadResult.sha256);
  file.insert(QStringLiteral("size_bytes"), static_cast<double>(uploadResult.sizeBytes));

  QJsonObject data;
  data.insert(QStringLiteral("conversation_id"), trimmedConversationId);
  data.insert(QStringLiteral("message_kind"), QString::fromLatin1(kMessageKindFile));
  data.insert(QStringLiteral("file"), file);

  const QString payload = protocol::createRequest(QString::fromLatin1(kTypeMessage),
                                                  action, data, requestId);
  PendingSend pending;
  pending.action = action;
  pending.conversationId = trimmedConversationId;
  pending.uploadResult = uploadResult;
  m_pendingSends.insert(requestId, pending);
  m_client->sendTextMessage(payload);
  return requestId;
}

QString ChatFileService::downloadFile(const QString &fileId, const QString &savePath,
                                      const QString &token,
                                      const QString &tokenType) {
  const QString requestId = QUuid::createUuid().toString(QUuid::WithoutBraces);
  const QString trimmedFileId = fileId.trimmed();
  const QString trimmedSavePath = savePath.trimmed();
  const QString action = QString::fromLatin1(kDownloadAction);
  if (trimmedFileId.isEmpty()) {
    failRequest(requestId, action, 400, QStringLiteral("file_id is required"));
    return requestId;
  }
  if (trimmedSavePath.isEmpty()) {
    failRequest(requestId, action, 400, QStringLiteral("save path is required"));
    return requestId;
  }

  const QString authHeader = resolveAuthorizationHeader(token, tokenType);
  if (authHeader.isEmpty()) {
    QString error;
    if (token.trimmed().isEmpty()) {
      validateSessionToken(&error);
    } else {
      error = QStringLiteral("token type is missing");
    }
    failRequest(requestId, action, 401, error);
    return requestId;
  }

  const QUrl downloadUrl = buildDownloadUrl(trimmedFileId);
  if (!downloadUrl.isValid()) {
    failRequest(requestId, action, 500, QStringLiteral("download endpoint is invalid"));
    return requestId;
  }

  QNetworkRequest request(downloadUrl);
  request.setRawHeader("Authorization", authHeader.toUtf8());
  request.setTransferTimeout(30000);
  QNetworkReply *reply = m_networkManager.get(request);

  PendingDownload pending;
  pending.action = action;
  pending.task.requestId = requestId;
  pending.task.fileId = trimmedFileId;
  pending.task.savePath = trimmedSavePath;
  pending.task.token = token.trimmed().isEmpty() ? UserSession::instance().uploadToken()
                                                 : token.trimmed();
  pending.task.tokenType = normalizeTokenType(
      tokenType.trimmed().isEmpty() ? UserSession::instance().uploadTokenType() : tokenType);
  pending.reply = reply;
  m_pendingDownloads.insert(requestId, pending);

  connect(reply, &QNetworkReply::downloadProgress, this,
          [this, requestId](qint64 received, qint64 total) {
            auto it = m_pendingDownloads.find(requestId);
            if (it == m_pendingDownloads.end()) {
              return;
            }
            it->task.bytesReceived = received;
            it->task.bytesTotal = total;
            emit downloadProgress(requestId, received, total);
          });
  connect(reply, &QNetworkReply::finished, this, [this, requestId]() {
    auto it = m_pendingDownloads.find(requestId);
    if (it == m_pendingDownloads.end()) {
      return;
    }

    PendingDownload pending = it.value();
    m_pendingDownloads.erase(it);
    QNetworkReply *reply = pending.reply.data();
    if (!reply) {
      failRequest(requestId, pending.action, 500,
                  QStringLiteral("download reply missing"));
      return;
    }

    if (reply->error() != QNetworkReply::NoError) {
      const int httpCode =
          reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
      const QString message =
          httpCode == 401 ? QStringLiteral("token missing or expired")
                          : QStringLiteral("download failed: %1")
                                .arg(reply->errorString().trimmed());
      failRequest(requestId, pending.action, httpCode == 0 ? 500 : httpCode, message);
      reply->deleteLater();
      return;
    }

    QString error;
    if (!parseDownloadResponse(requestId, pending.task.fileId, pending.task.savePath,
                               reply, &error)) {
      failRequest(requestId, pending.action, 500, error);
      reply->deleteLater();
      return;
    }

    emit downloadFinished(requestId, pending.task);
    reply->deleteLater();
  });

  return requestId;
}

bool ChatFileService::handleIncomingMessage(const QString &payload,
                                            ChatMessage *outMessage,
                                            QString *error) {
  protocol::Envelope envelope;
  QString parseError;
  if (!protocol::parseEnvelope(payload, &envelope, &parseError)) {
    if (error) {
      *error = parseError.isEmpty() ? QStringLiteral("invalid message payload")
                                    : parseError;
    }
    return false;
  }
  return handleIncomingMessage(envelope, outMessage, error);
}

bool ChatFileService::handleIncomingMessage(const protocol::Envelope &envelope,
                                            ChatMessage *outMessage,
                                            QString *error) {
  ChatMessage message;
  if (!parseFileMessage(envelope, &message, error)) {
    return false;
  }
  cacheMessage(message);
  if (outMessage) {
    *outMessage = message;
  }
  emit fileMessageReceived(message);
  return true;
}

bool ChatFileService::canHandleMessage(const QString &payload) const {
  protocol::Envelope envelope;
  return protocol::parseEnvelope(payload, &envelope) && canHandleMessage(envelope);
}

bool ChatFileService::canHandleMessage(const protocol::Envelope &envelope) const {
  if (envelope.type != QLatin1String(kTypeMessage) ||
      envelope.action != QLatin1String(kActionSend)) {
    return false;
  }
  return jsonStringValue(envelope.data, "message_kind") ==
         QLatin1String(kMessageKindFile);
}

bool ChatFileService::parseFileMessage(const QString &payload, ChatMessage *outMessage,
                                       QString *error) const {
  protocol::Envelope envelope;
  QString parseError;
  if (!protocol::parseEnvelope(payload, &envelope, &parseError)) {
    if (error) {
      *error = parseError;
    }
    return false;
  }
  return parseFileMessage(envelope, outMessage, error);
}

bool ChatFileService::parseFileMessage(const protocol::Envelope &envelope,
                                       ChatMessage *outMessage,
                                       QString *error) const {
  if (!outMessage) {
    if (error) {
      *error = QStringLiteral("internal error: out message is null");
    }
    return false;
  }
  if (!canHandleMessage(envelope)) {
    if (error) {
      *error = QStringLiteral("message_kind is not file");
    }
    return false;
  }

  const QJsonObject fileObj = envelope.data.value(QStringLiteral("file")).toObject();
  if (fileObj.isEmpty()) {
    if (error) {
      *error = QStringLiteral("file object is missing");
    }
    return false;
  }

  ChatMessage message;
  message.requestId = envelope.requestId.trimmed();
  message.conversationId = jsonStringValue(envelope.data, "conversation_id");
  message.messageId = jsonStringValue(envelope.data, "message_id");
  message.seq = jsonInt64Value(envelope.data, "seq", 0);
  message.sentAt = jsonStringValue(envelope.data, "sent_at");
  message.senderUserId = jsonStringValue(envelope.data, "from_user_id");
  message.senderNumericId = jsonStringValue(envelope.data, "from_numeric_id");
  message.senderUsername = jsonStringValue(envelope.data, "from_username");
  message.kind = ChatMessageKind::File;
  message.file.fileId = jsonStringValue(fileObj, "file_id");
  message.file.originalName = jsonStringValue(fileObj, "original_name");
  message.file.storedName = jsonStringValue(fileObj, "stored_name");
  message.file.sizeBytes = jsonInt64Value(fileObj, "size_bytes", -1);
  message.file.contentType = jsonStringValue(fileObj, "content_type");
  message.file.sha256 = jsonStringValue(fileObj, "sha256");

  if (!message.isValid()) {
    if (error) {
      *error = QStringLiteral("file message missing required fields");
    }
    return false;
  }

  *outMessage = message;
  return true;
}

QVector<ChatMessage>
ChatFileService::messagesForConversation(const QString &conversationId) const {
  return m_messagesByConversation.value(conversationId.trimmed());
}

void ChatFileService::onTextMessageReceived(const QString &message) {
  protocol::Envelope envelope;
  QString parseError;
  if (!protocol::parseEnvelope(message, &envelope, &parseError)) {
    return;
  }
  if (envelope.type != QLatin1String(kTypeMessage) ||
      envelope.action != QLatin1String(kActionSend)) {
    return;
  }

  const QString requestId = envelope.requestId.trimmed();
  if (!requestId.isEmpty() && m_pendingSends.contains(requestId)) {
    const PendingSend pending = m_pendingSends.take(requestId);
    const int code = envelope.hasCode ? envelope.code : extractCode(envelope.data, 0);
    const bool ok =
        code == 0 && (envelope.hasOk ? envelope.ok
                                     : envelope.data.value(QStringLiteral("ok"))
                                           .toBool(true));
    if (!ok) {
      const QString message =
          envelope.message.trimmed().isEmpty()
              ? extractMessage(envelope.data, QStringLiteral("send file message failed"))
              : envelope.message.trimmed();
      failRequest(requestId, pending.action, code, message);
      return;
    }

    ChatMessage chatMessage;
    if (!canHandleMessage(envelope) ||
        !parseFileMessage(envelope, &chatMessage, nullptr)) {
      chatMessage.requestId = requestId;
      chatMessage.conversationId = pending.conversationId;
      chatMessage.messageId = jsonStringValue(envelope.data, "message_id");
      chatMessage.seq = jsonInt64Value(envelope.data, "seq", 0);
      chatMessage.sentAt = jsonStringValue(envelope.data, "sent_at");
      chatMessage.kind = ChatMessageKind::File;
      chatMessage.file.fileId = pending.uploadResult.fileId;
      chatMessage.file.originalName = pending.uploadResult.originalName;
      chatMessage.file.storedName = pending.uploadResult.storedName;
      chatMessage.file.sizeBytes = pending.uploadResult.sizeBytes;
      chatMessage.file.contentType = pending.uploadResult.contentType;
      chatMessage.file.sha256 = pending.uploadResult.sha256;
    }

    if (!chatMessage.isValid()) {
      failRequest(requestId, pending.action, 500,
                  QStringLiteral("file send response missing required fields"));
      return;
    }

    if (chatMessage.senderUserId.trimmed().isEmpty()) {
      chatMessage.senderUserId = UserSession::instance().userId().trimmed();
    }
    if (chatMessage.senderNumericId.trimmed().isEmpty()) {
      chatMessage.senderNumericId = UserSession::instance().numericId().trimmed();
    }
    if (chatMessage.senderUsername.trimmed().isEmpty()) {
      chatMessage.senderUsername = UserSession::instance().username().trimmed();
    }

    cacheMessage(chatMessage);
    emit fileMessageSendSucceeded(requestId, chatMessage);
    return;
  }

}

void ChatFileService::onDisconnected() {
  const auto sendIds = m_pendingSends.keys();
  for (const QString &requestId : sendIds) {
    const QString action = m_pendingSends.take(requestId).action;
    failRequest(requestId, action, 500, QStringLiteral("websocket disconnected"));
  }
}

QString ChatFileService::normalizeTokenType(const QString &tokenType) const {
  const QString trimmed = tokenType.trimmed();
  return trimmed.isEmpty() ? QStringLiteral("Bearer") : trimmed;
}

QString ChatFileService::resolveAuthorizationHeader(const QString &token,
                                                    const QString &tokenType) const {
  const QString trimmedToken = token.trimmed();
  if (!trimmedToken.isEmpty()) {
    return QStringLiteral("%1 %2").arg(normalizeTokenType(tokenType), trimmedToken);
  }

  QString error;
  if (!validateSessionToken(&error)) {
    return QString();
  }
  return UserSession::instance().authorizationHeaderValue();
}

QUrl ChatFileService::buildUploadUrl() const {
  const QString explicitUrl = qEnvironmentVariable(kHttpUrlEnv).trimmed();
  if (!explicitUrl.isEmpty()) {
    QUrl url(explicitUrl);
    url.setPath(QStringLiteral("/upload/chat-file"));
    return url;
  }

  QUrl url;
  url.setScheme(QStringLiteral("http"));
  url.setHost(resolveHttpHost(m_client));
  url.setPort(resolveHttpPort());
  url.setPath(QStringLiteral("/upload/chat-file"));
  return url;
}

QUrl ChatFileService::buildDownloadUrl(const QString &fileId) const {
  const QString explicitUrl = qEnvironmentVariable(kHttpUrlEnv).trimmed();
  if (!explicitUrl.isEmpty()) {
    QUrl url(explicitUrl);
    url.setPath(QStringLiteral("/download/chat-file/%1").arg(fileId.trimmed()));
    return url;
  }

  QUrl url;
  url.setScheme(QStringLiteral("http"));
  url.setHost(resolveHttpHost(m_client));
  url.setPort(resolveHttpPort());
  url.setPath(QStringLiteral("/download/chat-file/%1").arg(fileId.trimmed()));
  return url;
}

bool ChatFileService::validateSessionToken(QString *error) const {
  const UserSession &session = UserSession::instance();
  if (!session.isLoggedIn()) {
    if (error) {
      *error = QStringLiteral("user is not logged in");
    }
    return false;
  }
  if (session.uploadToken().trimmed().isEmpty()) {
    if (error) {
      *error = QStringLiteral("chat file token is missing");
    }
    return false;
  }
  if (session.uploadTokenType().trimmed().isEmpty()) {
    if (error) {
      *error = QStringLiteral("chat file token type is missing");
    }
    return false;
  }
  if (session.isUploadTokenExpired()) {
    if (error) {
      *error = QStringLiteral("chat file token is expired");
    }
    return false;
  }
  return true;
}

void ChatFileService::failRequest(const QString &requestId, const QString &action,
                                  int code, const QString &error) {
  qWarning().noquote() << "[ChatFileService] action=" << action
                       << "request_id=" << requestId << "code=" << code
                       << "message=" << error;
  emit requestFailed(requestId, action, code, error);
}

void ChatFileService::cacheMessage(const ChatMessage &message) {
  if (!message.isValid()) {
    return;
  }
  QVector<ChatMessage> &messages =
      m_messagesByConversation[message.conversationId.trimmed()];
  messages.push_back(message);
}

ChatFileUploadResult
ChatFileService::parseUploadResponse(const QString &requestId, int httpCode,
                                     const QByteArray &body, QString *error) const {
  ChatFileUploadResult result;
  result.requestId = requestId;
  result.code = httpCode;

  QJsonParseError parseError;
  const QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);
  if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
    if (error) {
      *error = QStringLiteral("upload response is not valid json");
    }
    return result;
  }

  const QJsonObject obj = doc.object();
  const bool explicitCode = hasExplicitCode(obj);
  result.ok = obj.value(QStringLiteral("ok")).toBool(false);
  result.code = explicitCode ? extractCode(obj, httpCode) : 0;
  result.message = extractMessage(obj);
  result.fileId = jsonStringValue(obj, "file_id");
  result.conversationId = jsonStringValue(obj, "conversation_id");
  result.originalName = jsonStringValue(obj, "original_name");
  result.storedName = jsonStringValue(obj, "stored_name");
  result.sizeBytes = jsonInt64Value(obj, "size_bytes", -1);
  result.contentType = jsonStringValue(obj, "content_type");
  result.sha256 = jsonStringValue(obj, "sha256");

  if (!result.ok || (explicitCode && result.code != 0)) {
    if (error) {
      *error = result.message.isEmpty() ? QStringLiteral("upload failed")
                                        : result.message;
    }
    return result;
  }
  if (!result.hasRequiredFields() || result.contentType.isEmpty() ||
      result.sha256.isEmpty()) {
    if (error) {
      *error = QStringLiteral("upload response missing required fields");
    }
    return result;
  }
  return result;
}

bool ChatFileService::parseDownloadResponse(const QString &requestId,
                                            const QString &fileId,
                                            const QString &savePath,
                                            QNetworkReply *reply,
                                            QString *error) {
  Q_UNUSED(requestId);
  Q_UNUSED(fileId);
  QFileInfo saveInfo(savePath);
  QDir parentDir = saveInfo.dir();
  if (!parentDir.exists() && !parentDir.mkpath(QStringLiteral("."))) {
    if (error) {
      *error = QStringLiteral("download target directory cannot be created");
    }
    return false;
  }

  QSaveFile file(savePath);
  if (!file.open(QIODevice::WriteOnly)) {
    if (error) {
      *error = QStringLiteral("download target cannot be opened");
    }
    return false;
  }
  const QByteArray body = reply->readAll();
  if (body.isEmpty()) {
    if (error) {
      *error = QStringLiteral("download response body is empty");
    }
    return false;
  }
  if (file.write(body) != body.size()) {
    if (error) {
      *error = QStringLiteral("download target write failed");
    }
    return false;
  }
  if (!file.commit()) {
    if (error) {
      *error = QStringLiteral("download target commit failed");
    }
    return false;
  }
  return true;
}

