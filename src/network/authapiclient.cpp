#include "authapiclient.h"

#include "usersession.h"

#include <QJsonDocument>
#include <QThread>
#include <QTimeZone>
#include <QUuid>

namespace {
constexpr const char *kTypeAuth = "AUTH";
constexpr const char *kActionLogin = "LOGIN";
constexpr const char *kActionLogout = "LOGOUT";

bool readRequiredString(const QJsonObject &obj, const char *key, QString *out,
                        bool allowNumber = false) {
  if (!obj.contains(QLatin1String(key))) {
    return false;
  }
  const QJsonValue value = obj.value(QLatin1String(key));
  if (value.isString()) {
    if (out) {
      *out = value.toString().trimmed();
    }
    return true;
  }
  if (allowNumber && value.isDouble()) {
    if (out) {
      *out = QString::number(static_cast<qint64>(value.toDouble()));
    }
    return true;
  }
  return false;
}

bool readRequiredInt(const QJsonObject &obj, const char *key, int *out) {
  if (!obj.contains(QLatin1String(key))) {
    return false;
  }
  const QJsonValue value = obj.value(QLatin1String(key));
  if (value.isDouble()) {
    if (out) {
      *out = value.toInt();
    }
    return true;
  }
  if (value.isString()) {
    bool ok = false;
    const int parsed = value.toString().trimmed().toInt(&ok);
    if (!ok) {
      return false;
    }
    if (out) {
      *out = parsed;
    }
    return true;
  }
  return false;
}

bool readRequiredBool(const QJsonObject &obj, const char *key, bool *out) {
  if (!obj.contains(QLatin1String(key))) {
    return false;
  }
  const QJsonValue value = obj.value(QLatin1String(key));
  if (!value.isBool()) {
    return false;
  }
  if (out) {
    *out = value.toBool();
  }
  return true;
}

QDateTime parseUtcIsoTime(const QString &value) {
  const QString trimmed = value.trimmed();
  if (trimmed.isEmpty()) {
    return QDateTime();
  }

  QDateTime dt = QDateTime::fromString(trimmed, Qt::ISODate);
  if (!dt.isValid()) {
    return QDateTime();
  }
  if (dt.timeSpec() == Qt::LocalTime) {
    dt.setTimeZone(QTimeZone::UTC);
  }
  return dt.toUTC();
}
} // namespace

AuthApiClient::AuthApiClient(websocketclient *client, QObject *parent)
    : QObject(parent), m_client(client) {
  qRegisterMetaType<AuthUserInfo>("AuthUserInfo");
  qRegisterMetaType<PresenceInfo>("PresenceInfo");
  qRegisterMetaType<LoginResult>("LoginResult");
  qRegisterMetaType<LogoutResult>("LogoutResult");

  Q_ASSERT_X(thread() == QThread::currentThread(), "AuthApiClient",
             "AuthApiClient should run in the main event thread");

  if (!m_client) {
    qWarning() << "[AUTH] init failed: websocket client is null";
    return;
  }

  connect(m_client, &websocketclient::textMessageReceived, this,
          &AuthApiClient::onTextMessageReceived);
  connect(m_client, &websocketclient::disconnected, this,
          &AuthApiClient::onDisconnected);
}

QString AuthApiClient::login(const QString &username, const QString &password) {
  const QString requestId = generateRequestId();
  const QString normalizedUsername = username.trimmed();
  if (normalizedUsername.isEmpty()) {
    failRequest(requestId, QString::fromLatin1(kActionLogin),
                QStringLiteral("username is required"));
    return requestId;
  }
  if (password.isEmpty()) {
    failRequest(requestId, QString::fromLatin1(kActionLogin),
                QStringLiteral("password is required"));
    return requestId;
  }
  if (!m_client || !m_client->isConnected()) {
    failRequest(requestId, QString::fromLatin1(kActionLogin),
                QStringLiteral("websocket is not connected"));
    return requestId;
  }

  QJsonObject data;
  data.insert(QStringLiteral("username"), normalizedUsername);
  data.insert(QStringLiteral("password"), password);
  addPendingRequest(requestId, QString::fromLatin1(kActionLogin));
  if (!sendAuthPayload(QString::fromLatin1(kActionLogin), requestId, data)) {
    clearPendingRequest(requestId);
    failRequest(requestId, QString::fromLatin1(kActionLogin),
                QStringLiteral("websocket is not connected"));
  }
  return requestId;
}

QString AuthApiClient::logout() { return logout(UserSession::instance().uploadToken()); }

QString AuthApiClient::logout(const QString &token) {
  const QString requestId = generateRequestId();
  const QString normalizedToken = token.trimmed();
  if (normalizedToken.isEmpty()) {
    failRequest(requestId, QString::fromLatin1(kActionLogout),
                QStringLiteral("logout token is empty"));
    return requestId;
  }
  if (!m_client || !m_client->isConnected()) {
    failRequest(requestId, QString::fromLatin1(kActionLogout),
                QStringLiteral("websocket is not connected"));
    return requestId;
  }

  QJsonObject data;
  data.insert(QStringLiteral("token"), normalizedToken);
  addPendingRequest(requestId, QString::fromLatin1(kActionLogout));
  if (!sendAuthPayload(QString::fromLatin1(kActionLogout), requestId, data)) {
    clearPendingRequest(requestId);
    failRequest(requestId, QString::fromLatin1(kActionLogout),
                QStringLiteral("websocket is not connected"));
  }
  return requestId;
}

bool AuthApiClient::isCurrentLoginResponse(const protocol::Envelope &envelope,
                                           const QString &pendingRequestId) {
  if (pendingRequestId.isEmpty()) {
    return false;
  }
  if (envelope.requestId == pendingRequestId) {
    return true;
  }
  if (!envelope.requestId.isEmpty()) {
    return false;
  }

  const QJsonValue receivedPayload =
      envelope.data.value(QStringLiteral("received_payload"));
  if (!receivedPayload.isString()) {
    return false;
  }

  protocol::Envelope originalRequest;
  if (!protocol::parseEnvelope(receivedPayload.toString(), &originalRequest)) {
    return false;
  }

  return originalRequest.requestId == pendingRequestId &&
         originalRequest.type == QLatin1String(kTypeAuth) &&
         originalRequest.action == QLatin1String(kActionLogin);
}

QString AuthApiClient::extractAuthErrorMessage(const protocol::Envelope &envelope,
                                               const QString &fallbackAction) {
  const QJsonObject &data = envelope.data;
  const QStringList directKeys = {QStringLiteral("message"), QStringLiteral("error"),
                                  QStringLiteral("reason"), QStringLiteral("detail")};
  for (const QString &key : directKeys) {
    const QJsonValue value = data.value(key);
    if (value.isString()) {
      const QString message = value.toString().trimmed();
      if (!message.isEmpty()) {
        return message;
      }
    }
  }

  const QJsonValue errorValue = data.value(QStringLiteral("error"));
  if (errorValue.isObject()) {
    const QJsonObject errorObj = errorValue.toObject();
    for (const QString &key : directKeys) {
      const QJsonValue value = errorObj.value(key);
      if (value.isString()) {
        const QString message = value.toString().trimmed();
        if (!message.isEmpty()) {
          return message;
        }
      }
    }
    return QString::fromUtf8(
               QJsonDocument(errorObj).toJson(QJsonDocument::Compact))
        .trimmed();
  }

  if (envelope.hasCode && envelope.code != 0) {
    if (fallbackAction.isEmpty()) {
      return QStringLiteral("request failed, code=%1").arg(envelope.code);
    }
    return QStringLiteral("%1 failed, code=%2")
        .arg(fallbackAction.toLower(), QString::number(envelope.code));
  }

  return QStringLiteral("request failed");
}

bool AuthApiClient::isLoginSuccessEnvelope(const protocol::Envelope &envelope) {
  const int code = envelope.hasCode ? envelope.code : 0;
  return code == 0 && envelope.data.value(QStringLiteral("ok")).toBool(false);
}

bool AuthApiClient::parseLoginResult(const protocol::Envelope &envelope,
                                     LoginResult *outResult, QString *error) {
  if (!outResult) {
    if (error) {
      *error = QStringLiteral("internal error: out login result is null");
    }
    return false;
  }
  if (envelope.type != QLatin1String(kTypeAuth) ||
      envelope.action != QLatin1String(kActionLogin)) {
    if (error) {
      *error = QStringLiteral("invalid AUTH/LOGIN envelope");
    }
    return false;
  }

  const QJsonObject &data = envelope.data;
  const QJsonValue userValue = data.value(QStringLiteral("user"));
  if (!userValue.isObject()) {
    if (error) {
      *error = QStringLiteral("login response missing user object");
    }
    return false;
  }

  const QJsonObject userObj = userValue.toObject();
  AuthUserInfo user;
  if (!readRequiredString(userObj, "user_id", &user.userId, true) ||
      !readRequiredString(userObj, "numeric_id", &user.numericId, true) ||
      !readRequiredString(userObj, "username", &user.username) ||
      !readRequiredString(userObj, "email", &user.email) ||
      !readRequiredString(userObj, "phone", &user.phone) ||
      !readRequiredInt(userObj, "status", &user.status) ||
      !readRequiredString(userObj, "user_uuid", &user.userUuid) ||
      !readRequiredString(userObj, "nickname", &user.nickname) ||
      !readRequiredString(userObj, "avatar_url", &user.avatarUrl) ||
      !readRequiredString(userObj, "bio", &user.bio)) {
    if (error) {
      *error = QStringLiteral("invalid AUTH/LOGIN user fields");
    }
    return false;
  }

  PresenceInfo presence;
  const QJsonValue presenceValue = data.value(QStringLiteral("presence"));
  if (presenceValue.isObject()) {
    const QJsonObject presenceObj = presenceValue.toObject();
    presence.hasPresence = true;
    readRequiredBool(presenceObj, "is_online", &presence.isOnline);
    presence.lastSeenAtUtc =
        presenceObj.value(QStringLiteral("last_seen_at")).toString().trimmed();
    presence.lastSeenAt = parseUtcIsoTime(presence.lastSeenAtUtc);
  }

  outResult->ok = data.value(QStringLiteral("ok")).toBool(false);
  outResult->message = data.value(QStringLiteral("message")).toString().trimmed();
  outResult->requestId = envelope.requestId;
  outResult->code = envelope.hasCode ? envelope.code : 0;
  outResult->user = user;
  outResult->uploadToken = data.value(QStringLiteral("upload_token")).toString().trimmed();
  outResult->uploadTokenType =
      data.value(QStringLiteral("upload_token_type")).toString().trimmed();
  outResult->uploadTokenExpiresAtUtc =
      data.value(QStringLiteral("upload_token_expires_at")).toString().trimmed();
  outResult->uploadTokenExpiresAt =
      parseUtcIsoTime(outResult->uploadTokenExpiresAtUtc);
  outResult->presence = presence;
  return true;
}

void AuthApiClient::onTextMessageReceived(const QString &message) {
  protocol::Envelope envelope;
  QString parseError;
  if (!protocol::parseEnvelope(message, &envelope, &parseError)) {
    return;
  }
  if (envelope.type != QLatin1String(kTypeAuth)) {
    return;
  }

  const QString requestId = envelope.requestId;
  if (requestId.isEmpty()) {
    qWarning() << "[AUTH] drop response: missing request_id";
    return;
  }
  if (!m_pendingRequests.contains(requestId)) {
    return;
  }

  const PendingRequest pending = m_pendingRequests.value(requestId);
  clearPendingRequest(requestId);

  const QString action = pending.action;
  if (!envelope.action.isEmpty() && envelope.action != action) {
    failRequest(requestId, action, QStringLiteral("response action mismatch"));
    return;
  }

  const int code = envelope.hasCode ? envelope.code : 0;
  const bool ok = envelope.data.value(QStringLiteral("ok")).toBool(false);
  if (!(code == 0 && ok)) {
    failRequest(requestId, action, extractAuthErrorMessage(envelope, action), code);
    return;
  }

  if (action == QLatin1String(kActionLogin)) {
    LoginResult result;
    QString error;
    if (!parseLoginResult(envelope, &result, &error)) {
      failRequest(requestId, action, error, code);
      return;
    }
    UserSession::instance().setLoginContext(
        result.user.userId, result.user.username, result.user.numericId,
        result.uploadToken, result.uploadTokenType, result.uploadTokenExpiresAtUtc,
        result.presence.isOnline, result.presence.lastSeenAtUtc);
    emit loginSucceeded(requestId, result);
    return;
  }

  if (action == QLatin1String(kActionLogout)) {
    LogoutResult result;
    QString error;
    if (!parseLogoutResult(envelope, &result, &error)) {
      failRequest(requestId, action, error, code);
      return;
    }
    UserSession::instance().clear();
    emit logoutSucceeded(requestId, result);
    return;
  }

  failRequest(requestId, action, QStringLiteral("unsupported action"));
}

void AuthApiClient::onDisconnected() {
  const auto requestIds = m_pendingRequests.keys();
  for (const QString &requestId : requestIds) {
    const QString action = m_pendingRequests.value(requestId).action;
    clearPendingRequest(requestId);
    failRequest(requestId, action, QStringLiteral("websocket disconnected"));
  }
}

QString AuthApiClient::generateRequestId() const {
  return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

bool AuthApiClient::sendAuthPayload(const QString &action, const QString &requestId,
                                    const QJsonObject &data) {
  if (!m_client || !m_client->isConnected()) {
    return false;
  }
  const QString payload =
      protocol::createRequest(QString::fromLatin1(kTypeAuth), action, data, requestId);
  m_client->sendTextMessage(payload);
  qInfo().noquote() << "[AUTH] send action=" << action
                    << "request_id=" << requestId;
  return true;
}

void AuthApiClient::addPendingRequest(const QString &requestId, const QString &action) {
  clearPendingRequest(requestId);

  auto *timer = new QTimer(this);
  timer->setSingleShot(true);
  connect(timer, &QTimer::timeout, this, [this, requestId, action]() {
    if (!m_pendingRequests.contains(requestId)) {
      return;
    }
    clearPendingRequest(requestId);
    failRequest(requestId, action, QStringLiteral("request timeout"));
  });
  timer->start(kRequestTimeoutMs);

  PendingRequest pending;
  pending.action = action;
  pending.timer = timer;
  m_pendingRequests.insert(requestId, pending);
}

void AuthApiClient::clearPendingRequest(const QString &requestId) {
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

void AuthApiClient::failRequest(const QString &requestId, const QString &action,
                                const QString &errorMessage, int code) {
  qWarning().noquote() << "[AUTH] action=" << action
                       << "request_id=" << requestId
                       << "code=" << code
                       << "message=" << errorMessage;
  emit authRequestFailedDetailed(requestId, action, code, errorMessage);
  emit authRequestFailed(requestId, action, errorMessage);
}

bool AuthApiClient::parseLogoutResult(const protocol::Envelope &envelope,
                                      LogoutResult *outResult,
                                      QString *error) const {
  if (!outResult) {
    if (error) {
      *error = QStringLiteral("internal error: out logout result is null");
    }
    return false;
  }

  const QJsonObject &data = envelope.data;
  if (!readRequiredBool(data, "ok", &outResult->ok) ||
      !readRequiredString(data, "message", &outResult->message) ||
      !readRequiredString(data, "user_id", &outResult->userId, true) ||
      !readRequiredString(data, "numeric_id", &outResult->numericId, true) ||
      !readRequiredBool(data, "offline", &outResult->offline)) {
    if (error) {
      *error = QStringLiteral("invalid AUTH/LOGOUT response fields");
    }
    return false;
  }

  outResult->requestId = envelope.requestId;
  outResult->code = envelope.hasCode ? envelope.code : 0;
  outResult->lastSeenAtUtc =
      data.value(QStringLiteral("last_seen_at")).toString().trimmed();
  outResult->lastSeenAt = parseUtcIsoTime(outResult->lastSeenAtUtc);
  return true;
}
