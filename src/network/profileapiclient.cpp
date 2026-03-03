#include "profileapiclient.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QThread>
#include <QUuid>

namespace {
constexpr const char *kTypeProfile = "PROFILE";
constexpr const char *kActionGetInfo = "GET_INFO";
constexpr const char *kActionSetInfo = "SET_INFO";

QString normalizeTheme(const QString &theme) {
  const QString trimmed = theme.trimmed();
  return trimmed.isEmpty() ? QStringLiteral("default") : trimmed;
}
}

ProfileApiClient::ProfileApiClient(websocketclient *client, QObject *parent)
    : QObject(parent), m_client(client) {
  qRegisterMetaType<ProfileInfo>("ProfileInfo");

  Q_ASSERT_X(thread() == QThread::currentThread(), "ProfileApiClient",
             "ProfileApiClient should run in the main event thread");

  if (!m_client) {
    qWarning() << "ProfileApiClient init failed: websocket client is null";
    return;
  }

  connect(m_client, &websocketclient::textMessageReceived, this,
          &ProfileApiClient::onTextMessageReceived);
  connect(m_client, &websocketclient::disconnected, this,
          &ProfileApiClient::onDisconnected);
}

void ProfileApiClient::requestProfileInfo(const QString &userId) {
  const QString requestId = generateRequestId();
  const QString action = QString::fromLatin1(kActionGetInfo);

  QString error;
  if (!validateGetInfo(userId, &error)) {
    failRequest(requestId, action, error);
    return;
  }

  QJsonObject data;
  data.insert("user_id", userId.trimmed());
  sendProfileRequest(action, requestId, data);
}

void ProfileApiClient::setProfileInfo(const QString &userId,
                                      const QString &avatarUrl,
                                      const QString &nickname,
                                      const QString &signature,
                                      const QString &theme) {
  const QString requestId = generateRequestId();
  const QString action = QString::fromLatin1(kActionSetInfo);

  QString error;
  if (!validateSetInfo(userId, avatarUrl, nickname, signature, theme, &error)) {
    failRequest(requestId, action, error);
    return;
  }

  QJsonObject data;
  data.insert("user_id", userId.trimmed());
  data.insert("avatar_url", avatarUrl.trimmed());
  data.insert("nickname", nickname.trimmed());
  data.insert("signature", signature.trimmed());
  data.insert("theme", normalizeTheme(theme));
  sendProfileRequest(action, requestId, data);
}

void ProfileApiClient::onTextMessageReceived(const QString &message) {
  protocol::Envelope envelope;
  QString parseError;
  if (!protocol::parseEnvelope(message, &envelope, &parseError)) {
    return;
  }

  if (envelope.type != QLatin1String(kTypeProfile)) {
    return;
  }

  const QString requestId = envelope.requestId;
  if (requestId.isEmpty()) {
    qWarning() << "[PROFILE] drop response: missing request_id";
    return;
  }

  if (!m_pendingRequests.contains(requestId)) {
    qWarning() << "[PROFILE] drop response: unknown request_id" << requestId;
    return;
  }

  const PendingRequest pending = m_pendingRequests.value(requestId);
  clearPendingRequest(requestId);

  const QString responseAction = envelope.action;
  const QString expectedAction = pending.action;
  if (!responseAction.isEmpty() && responseAction != expectedAction) {
    failRequest(requestId, expectedAction,
                QStringLiteral("response action mismatch"));
    return;
  }

  const int code = envelope.hasCode ? envelope.code : 0;
  const bool ok = envelope.data.value("ok").toBool(false);
  const QString msg = envelope.data.value("message").toString().trimmed();

  qInfo().noquote() << "[PROFILE] action=" << expectedAction
                    << "request_id=" << requestId << "code=" << code
                    << "message=" << msg;

  if (!(code == 0 && ok)) {
    const QString error =
        msg.isEmpty() ? QStringLiteral("request failed, code=%1").arg(code) : msg;
    failRequest(requestId, expectedAction, error);
    return;
  }

  ProfileInfo info;
  QString error;
  if (!parseProfileInfo(envelope.data, &info, &error)) {
    failRequest(requestId, expectedAction, error);
    return;
  }

  if (expectedAction == QLatin1String(kActionGetInfo)) {
    emit profileInfoReceived(requestId, info);
    return;
  }

  if (expectedAction == QLatin1String(kActionSetInfo)) {
    emit profileInfoSetSuccess(requestId, info);
    return;
  }

  failRequest(requestId, expectedAction, QStringLiteral("unsupported action"));
}

void ProfileApiClient::onDisconnected() {
  const auto requestIds = m_pendingRequests.keys();
  for (const QString &requestId : requestIds) {
    const QString action = m_pendingRequests.value(requestId).action;
    clearPendingRequest(requestId);
    emit requestFailed(requestId, action,
                       QStringLiteral("websocket disconnected"));
  }
}

QString ProfileApiClient::generateRequestId() const {
  return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

bool ProfileApiClient::validateGetInfo(const QString &userId, QString *error) const {
  if (userId.trimmed().isEmpty()) {
    if (error) {
      *error = QStringLiteral("user_id is required");
    }
    return false;
  }
  return true;
}

bool ProfileApiClient::validateSetInfo(const QString &userId,
                                       const QString &avatarUrl,
                                       const QString &nickname,
                                       const QString &signature,
                                       const QString &theme,
                                       QString *error) const {
  const QString uid = userId.trimmed();
  const QString avatar = avatarUrl.trimmed();
  const QString name = nickname.trimmed();
  const QString sign = signature.trimmed();
  const QString th = theme.trimmed();

  if (uid.isEmpty()) {
    if (error) {
      *error = QStringLiteral("user_id is required");
    }
    return false;
  }
  if (avatar.isEmpty()) {
    if (error) {
      *error = QStringLiteral("avatar_url is required");
    }
    return false;
  }
  if (name.isEmpty()) {
    if (error) {
      *error = QStringLiteral("nickname is required");
    }
    return false;
  }
  if (sign.isEmpty()) {
    if (error) {
      *error = QStringLiteral("signature is required");
    }
    return false;
  }
  if (avatar.size() > 255) {
    if (error) {
      *error = QStringLiteral("avatar_url length must be <= 255");
    }
    return false;
  }
  if (name.size() > 64) {
    if (error) {
      *error = QStringLiteral("nickname length must be <= 64");
    }
    return false;
  }
  if (sign.size() > 255) {
    if (error) {
      *error = QStringLiteral("signature length must be <= 255");
    }
    return false;
  }
  if (!th.isEmpty() && th.size() > 32) {
    if (error) {
      *error = QStringLiteral("theme length must be <= 32");
    }
    return false;
  }

  return true;
}

void ProfileApiClient::sendProfileRequest(const QString &action,
                                          const QString &requestId,
                                          const QJsonObject &data) {
  if (!m_client) {
    failRequest(requestId, action, QStringLiteral("websocket client is null"));
    return;
  }
  if (!m_client->isConnected()) {
    failRequest(requestId, action, QStringLiteral("websocket is not connected"));
    return;
  }

  addPendingRequest(requestId, action);
  const QString payload = protocol::createRequest(QString::fromLatin1(kTypeProfile),
                                                  action, data, requestId);
  m_client->sendTextMessage(payload);

  qInfo().noquote() << "[PROFILE] send action=" << action
                    << "request_id=" << requestId;
}

void ProfileApiClient::addPendingRequest(const QString &requestId,
                                         const QString &action) {
  clearPendingRequest(requestId);

  auto *timer = new QTimer(this);
  timer->setSingleShot(true);
  connect(timer, &QTimer::timeout, this, [this, requestId, action]() {
    if (!m_pendingRequests.contains(requestId)) {
      return;
    }
    m_pendingRequests.remove(requestId);
    qWarning().noquote() << "[PROFILE] timeout action=" << action
                         << "request_id=" << requestId;
    emit requestFailed(requestId, action, QStringLiteral("request timeout"));
  });
  timer->start(kRequestTimeoutMs);

  PendingRequest pending;
  pending.action = action;
  pending.timer = timer;
  m_pendingRequests.insert(requestId, pending);
}

void ProfileApiClient::clearPendingRequest(const QString &requestId) {
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

void ProfileApiClient::failRequest(const QString &requestId, const QString &action,
                                   const QString &errorMessage) {
  qWarning().noquote() << "[PROFILE] action=" << action
                       << "request_id=" << requestId
                       << "message=" << errorMessage;
  emit requestFailed(requestId, action, errorMessage);
}

bool ProfileApiClient::parseProfileInfo(const QJsonObject &data, ProfileInfo *outInfo,
                                        QString *error) const {
  if (!outInfo) {
    if (error) {
      *error = QStringLiteral("internal error: out profile is null");
    }
    return false;
  }

  const QJsonValue profileVal = data.value("profile");
  if (!profileVal.isObject()) {
    if (error) {
      *error = QStringLiteral("response missing profile object");
    }
    return false;
  }

  const QJsonObject profileObj = profileVal.toObject();
  const QJsonValue avatar = profileObj.value("avatar_url");
  const QJsonValue nickname = profileObj.value("nickname");
  const QJsonValue signature = profileObj.value("signature");
  const QJsonValue theme = profileObj.value("theme");

  if (!avatar.isString() || !nickname.isString() || !signature.isString()) {
    if (error) {
      *error = QStringLiteral(
          "invalid profile fields, avatar_url/nickname/signature must be string");
    }
    return false;
  }

  outInfo->avatarUrl = avatar.toString();
  outInfo->nickname = nickname.toString();
  outInfo->signature = signature.toString();
  outInfo->theme = theme.isString() ? normalizeTheme(theme.toString())
                                    : QStringLiteral("default");
  return true;
}
