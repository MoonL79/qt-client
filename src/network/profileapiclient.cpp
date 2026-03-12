#include "profileapiclient.h"

#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <QThread>
#include <QTimeZone>
#include <QUuid>
#include <QtGlobal>

namespace {
constexpr const char *kTypeProfile = "PROFILE";
constexpr const char *kActionGetInfo = "GET_INFO";
constexpr const char *kActionSetInfo = "SET_INFO";
constexpr const char *kActionGet = "GET";
constexpr const char *kActionAddFriend = "ADD_FRIEND";
constexpr const char *kActionDeleteFriend = "DELETE_FRIEND";
constexpr const char *kActionListFriends = "LIST_FRIENDS";

QString normalizeTheme(const QString &theme) {
  const QString trimmed = theme.trimmed();
  return trimmed.isEmpty() ? QStringLiteral("default") : trimmed;
}

QString jsonValueToString(const QJsonValue &value) {
  if (value.isString()) {
    return value.toString();
  }
  if (value.isDouble()) {
    const qint64 num = static_cast<qint64>(value.toDouble());
    return QString::number(num);
  }
  return QString();
}

bool readRequiredString(const QJsonObject &obj, const char *key, QString *out,
                        bool allowNumber = false) {
  if (!obj.contains(QLatin1String(key))) {
    return false;
  }
  const QJsonValue value = obj.value(QLatin1String(key));
  if (value.isString()) {
    if (out) {
      *out = value.toString();
    }
    return true;
  }
  if (allowNumber && value.isDouble()) {
    if (out) {
      const qint64 num = static_cast<qint64>(value.toDouble());
      *out = QString::number(num);
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
    const int parsed = value.toString().toInt(&ok);
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

int readOptionalInt(const QJsonObject &obj, const char *key, int defaultValue = 0) {
  int out = defaultValue;
  if (readRequiredInt(obj, key, &out)) {
    return out;
  }
  return defaultValue;
}

bool readOptionalBool(const QJsonObject &obj, const char *key,
                      bool defaultValue = false) {
  bool out = defaultValue;
  if (readRequiredBool(obj, key, &out)) {
    return out;
  }
  return defaultValue;
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

bool isUnsignedIntegerString(const QString &value) {
  static const QRegularExpression kUnsignedIntRe(QStringLiteral("^\\d+$"));
  return kUnsignedIntRe.match(value).hasMatch();
}
}

ProfileApiClient::ProfileApiClient(websocketclient *client, QObject *parent)
    : QObject(parent), m_client(client) {
  qRegisterMetaType<ProfileInfo>("ProfileInfo");
  qRegisterMetaType<AddFriendResult>("AddFriendResult");
  qRegisterMetaType<DeleteFriendRequest>("DeleteFriendRequest");
  qRegisterMetaType<DeleteFriendResult>("DeleteFriendResult");
  qRegisterMetaType<FriendItem>("FriendItem");
  qRegisterMetaType<QVector<FriendItem>>("QVector<FriendItem>");
  qRegisterMetaType<QJsonObject>("QJsonObject");

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

QString ProfileApiClient::requestProfileInfo(const QString &userId) {
  const QString requestId = generateRequestId();
  const QString action = QString::fromLatin1(kActionGetInfo);

  QString error;
  if (!validateGetInfo(userId, &error)) {
    failRequest(requestId, action, error);
    return requestId;
  }

  QJsonObject data;
  data.insert("user_id", userId.trimmed());
  sendProfileRequest(action, requestId, data, 0, false);
  return requestId;
}

QString ProfileApiClient::setProfileInfo(const QString &userId,
                                         const QString &avatarUrl,
                                         const QString &nickname,
                                         const QString &signature,
                                         const QString &theme) {
  const QString requestId = generateRequestId();
  const QString action = QString::fromLatin1(kActionSetInfo);

  QString error;
  if (!validateSetInfo(userId, avatarUrl, nickname, signature, theme, &error)) {
    failRequest(requestId, action, error);
    return requestId;
  }

  QJsonObject data;
  data.insert("user_id", userId.trimmed());
  data.insert("avatar_url", avatarUrl.trimmed());
  data.insert("nickname", nickname.trimmed());
  data.insert("signature", signature.trimmed());
  data.insert("theme", normalizeTheme(theme));
  sendProfileRequest(action, requestId, data, 0, false);
  return requestId;
}

QString ProfileApiClient::queryUserProfile(const QString &numericId) {
  const QString requestId = generateRequestId();
  const QString action = QString::fromLatin1(kActionGet);

  QString error;
  if (!validateQueryUserProfile(numericId, &error)) {
    failRequest(requestId, action, error);
    return requestId;
  }

  QJsonObject data;
  data.insert("numeric_id", numericId.trimmed());
  sendProfileRequest(action, requestId, data, kMaxRetryCount, true);
  return requestId;
}

QString ProfileApiClient::addFriend(const QString &userNumericId,
                                    const QString &friendNumericId,
                                    const QString &remark) {
  const QString requestId = generateRequestId();
  const QString action = QString::fromLatin1(kActionAddFriend);

  QString error;
  if (!validateAddFriend(userNumericId, friendNumericId, remark, &error)) {
    failRequest(requestId, action, error);
    return requestId;
  }

  QJsonObject data;
  data.insert("user_numeric_id", userNumericId.trimmed());
  data.insert("friend_numeric_id", friendNumericId.trimmed());
  if (!remark.trimmed().isEmpty()) {
    data.insert("remark", remark.trimmed());
  }
  sendProfileRequest(action, requestId, data, 0, false);
  return requestId;
}

QString ProfileApiClient::deleteFriend(const QString &userNumericId,
                                       const QString &friendNumericId) {
  const QString requestId = generateRequestId();
  const QString action = QString::fromLatin1(kActionDeleteFriend);

  QString error;
  if (!validateDeleteFriend(userNumericId, friendNumericId, &error)) {
    failRequest(requestId, action, error, 3003);
    return requestId;
  }

  QJsonObject data;
  data.insert("user_numeric_id", userNumericId.trimmed());
  data.insert("friend_numeric_id", friendNumericId.trimmed());
  sendProfileRequest(action, requestId, data, 0, false);
  return requestId;
}

QString ProfileApiClient::fetchFriendList(const QString &myNumericId) {
  const QString requestId = generateRequestId();
  const QString action = QString::fromLatin1(kActionListFriends);

  QString error;
  if (!validateFetchFriendList(myNumericId, &error)) {
    failRequest(requestId, action, error, 3003);
    return requestId;
  }

  QJsonObject data;
  data.insert("numeric_id", myNumericId.trimmed());
  sendProfileRequest(action, requestId, data, kMaxRetryCount, true);
  return requestId;
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
    failRequest(requestId, expectedAction, error, code);
    return;
  }

  if (expectedAction == QLatin1String(kActionGetInfo)) {
    ProfileInfo info;
    QString error;
    if (!parseProfileInfo(envelope.data, &info, false, &error)) {
      failRequest(requestId, expectedAction, error);
      return;
    }
    emit profileInfoReceived(requestId, info);
    return;
  }

  if (expectedAction == QLatin1String(kActionSetInfo)) {
    ProfileInfo info;
    QString error;
    if (!parseProfileInfo(envelope.data, &info, false, &error)) {
      failRequest(requestId, expectedAction, error);
      return;
    }
    emit profileInfoSetSuccess(requestId, info);
    return;
  }

  if (expectedAction == QLatin1String(kActionGet)) {
    ProfileInfo info;
    QString error;
    if (!parseProfileInfo(envelope.data, &info, true, &error)) {
      failRequest(requestId, expectedAction, error);
      return;
    }
    emit userProfileQueried(requestId, info);
    return;
  }

  if (expectedAction == QLatin1String(kActionAddFriend)) {
    AddFriendResult result;
    QString error;
    if (!parseAddFriendResult(envelope.data, &result, &error)) {
      failRequest(requestId, expectedAction, error);
      return;
    }
    emit addFriendSuccess(requestId, result);
    return;
  }

  if (expectedAction == QLatin1String(kActionDeleteFriend)) {
    DeleteFriendResult result;
    QString error;
    if (!parseDeleteFriendResult(envelope.data, requestId, code, &result, &error)) {
      failRequest(requestId, expectedAction, error, code);
      return;
    }
    emit deleteFriendFinished(requestId, result);
    return;
  }

  if (expectedAction == QLatin1String(kActionListFriends)) {
    emit friendListPayloadReceived(requestId, envelope.data);
    QVector<FriendItem> friends;
    QString error;
    if (!parseFriendList(envelope.data, &friends, &error)) {
      failRequest(requestId, expectedAction, error, 3003);
      return;
    }
    emit friendListFetched(requestId, friends);
    return;
  }

  failRequest(requestId, expectedAction, QStringLiteral("unsupported action"));
}

void ProfileApiClient::onDisconnected() {
  const auto requestIds = m_pendingRequests.keys();
  for (const QString &requestId : requestIds) {
    if (!retryPendingRequest(requestId, QStringLiteral("websocket disconnected"))) {
      const QString action = m_pendingRequests.value(requestId).action;
      clearPendingRequest(requestId);
      failRequest(requestId, action, QStringLiteral("websocket disconnected"));
    }
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

bool ProfileApiClient::validateQueryUserProfile(const QString &numericId,
                                                QString *error) const {
  const QString id = numericId.trimmed();
  if (id.isEmpty()) {
    if (error) {
      *error = QStringLiteral("numeric_id is required");
    }
    return false;
  }
  if (!isUnsignedIntegerString(id)) {
    if (error) {
      *error = QStringLiteral("numeric_id must be unsigned integer string");
    }
    return false;
  }
  return true;
}

bool ProfileApiClient::validateAddFriend(const QString &userNumericId,
                                         const QString &friendNumericId,
                                         const QString &remark,
                                         QString *error) const {
  const QString userId = userNumericId.trimmed();
  const QString friendId = friendNumericId.trimmed();
  const QString note = remark.trimmed();
  if (userId.isEmpty()) {
    if (error) {
      *error = QStringLiteral("user_numeric_id is required");
    }
    return false;
  }
  if (!isUnsignedIntegerString(userId)) {
    if (error) {
      *error = QStringLiteral("user_numeric_id must be unsigned integer string");
    }
    return false;
  }
  if (friendId.isEmpty()) {
    if (error) {
      *error = QStringLiteral("friend_numeric_id is required");
    }
    return false;
  }
  if (!isUnsignedIntegerString(friendId)) {
    if (error) {
      *error = QStringLiteral("friend_numeric_id must be unsigned integer string");
    }
    return false;
  }
  if (userId == friendId) {
    if (error) {
      *error = QStringLiteral("cannot add self");
    }
    return false;
  }
  if (note.size() > 255) {
    if (error) {
      *error = QStringLiteral("remark length must be <= 255");
    }
    return false;
  }
  return true;
}

bool ProfileApiClient::validateDeleteFriend(const QString &userNumericId,
                                            const QString &friendNumericId,
                                            QString *error) const {
  const QString userId = userNumericId.trimmed();
  const QString friendId = friendNumericId.trimmed();
  if (userId.isEmpty()) {
    if (error) {
      *error = QStringLiteral("user_numeric_id is required");
    }
    return false;
  }
  if (!isUnsignedIntegerString(userId)) {
    if (error) {
      *error = QStringLiteral("user_numeric_id must be unsigned integer string");
    }
    return false;
  }
  if (friendId.isEmpty()) {
    if (error) {
      *error = QStringLiteral("friend_numeric_id is required");
    }
    return false;
  }
  if (!isUnsignedIntegerString(friendId)) {
    if (error) {
      *error = QStringLiteral("friend_numeric_id must be unsigned integer string");
    }
    return false;
  }
  if (userId == friendId) {
    if (error) {
      *error = QStringLiteral("cannot delete self");
    }
    return false;
  }
  return true;
}

bool ProfileApiClient::validateFetchFriendList(const QString &myNumericId,
                                               QString *error) const {
  const QString id = myNumericId.trimmed();
  if (id.isEmpty()) {
    if (error) {
      *error = QStringLiteral("numeric_id is required");
    }
    return false;
  }
  if (!isUnsignedIntegerString(id)) {
    if (error) {
      *error = QStringLiteral("numeric_id must be unsigned integer string");
    }
    return false;
  }
  return true;
}

void ProfileApiClient::sendProfileRequest(const QString &action,
                                          const QString &requestId,
                                          const QJsonObject &data, int retries,
                                          bool retryOnTransient) {
  if (!m_client) {
    failRequest(requestId, action, QStringLiteral("websocket client is null"));
    return;
  }
  addPendingRequest(requestId, action, data, retries, retryOnTransient);
  if (!sendProfilePayload(action, requestId, data)) {
    clearPendingRequest(requestId);
    failRequest(requestId, action, QStringLiteral("websocket is not connected"));
  }
}

void ProfileApiClient::addPendingRequest(const QString &requestId,
                                         const QString &action,
                                         const QJsonObject &data, int retries,
                                         bool retryOnTransient) {
  clearPendingRequest(requestId);

  auto *timer = new QTimer(this);
  timer->setSingleShot(true);
  connect(timer, &QTimer::timeout, this, [this, requestId, action]() {
    if (!m_pendingRequests.contains(requestId)) {
      return;
    }
    if (!retryPendingRequest(requestId, QStringLiteral("request timeout"))) {
      m_pendingRequests.remove(requestId);
      qWarning().noquote() << "[PROFILE] timeout action=" << action
                           << "request_id=" << requestId;
      failRequest(requestId, action, QStringLiteral("request timeout"));
    }
  });
  timer->start(kRequestTimeoutMs);

  PendingRequest pending;
  pending.action = action;
  pending.data = data;
  pending.remainingRetries = qMax(0, retries);
  pending.retryOnTransient = retryOnTransient;
  pending.timer = timer;
  m_pendingRequests.insert(requestId, pending);
}

bool ProfileApiClient::sendProfilePayload(const QString &action,
                                          const QString &requestId,
                                          const QJsonObject &data) {
  if (!m_client || !m_client->isConnected()) {
    return false;
  }
  const QString payload = protocol::createRequest(QString::fromLatin1(kTypeProfile),
                                                  action, data, requestId);
  m_client->sendTextMessage(payload);
  qInfo().noquote() << "[PROFILE] send action=" << action
                    << "request_id=" << requestId;
  return true;
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

bool ProfileApiClient::retryPendingRequest(const QString &requestId,
                                           const QString &reason) {
  auto it = m_pendingRequests.find(requestId);
  if (it == m_pendingRequests.end()) {
    return false;
  }
  if (!it->retryOnTransient || it->remainingRetries <= 0) {
    return false;
  }

  PendingRequest pending = it.value();
  pending.remainingRetries -= 1;
  m_pendingRequests.insert(requestId, pending);

  qWarning().noquote() << "[PROFILE] retry action=" << pending.action
                       << "request_id=" << requestId << "reason=" << reason
                       << "remaining_retries=" << pending.remainingRetries;
  QTimer::singleShot(kRetryDelayMs, this, [this, requestId]() {
    auto it = m_pendingRequests.find(requestId);
    if (it == m_pendingRequests.end()) {
      return;
    }
    if (!sendProfilePayload(it->action, requestId, it->data)) {
      const QString action = it->action;
      clearPendingRequest(requestId);
      failRequest(requestId, action, QStringLiteral("request timeout, please retry manually"));
      return;
    }
    if (it->timer) {
      it->timer->start(kRequestTimeoutMs);
    }
  });
  return true;
}

void ProfileApiClient::failRequest(const QString &requestId, const QString &action,
                                   const QString &errorMessage, int code) {
  qWarning().noquote() << "[PROFILE] action=" << action
                       << "request_id=" << requestId
                       << "code=" << code
                       << "message=" << errorMessage;
  if (action == QLatin1String(kActionListFriends)) {
    emit friendListFailed(requestId, code, errorMessage);
  }
  emit requestFailedDetailed(requestId, action, code, errorMessage);
  emit requestFailed(requestId, action, errorMessage);
}

bool ProfileApiClient::parseProfileInfo(const QJsonObject &data, ProfileInfo *outInfo,
                                        bool strict, QString *error) const {
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
  QString userId;
  QString numericId;
  QString username;
  QString email;
  QString phone;
  int statusValue = 0;
  QString userUuid;
  QString avatarUrl;
  QString nickname;
  QString bio;
  QString signature;
  QString theme;
  if (strict) {
    if (!readRequiredString(profileObj, "user_id", &userId, true) ||
        !readRequiredString(profileObj, "numeric_id", &numericId, true) ||
        !readRequiredString(profileObj, "username", &username) ||
        !readRequiredString(profileObj, "email", &email) ||
        !readRequiredString(profileObj, "phone", &phone) ||
        !readRequiredInt(profileObj, "status", &statusValue) ||
        !readRequiredString(profileObj, "user_uuid", &userUuid) ||
        !readRequiredString(profileObj, "nickname", &nickname) ||
        !readRequiredString(profileObj, "avatar_url", &avatarUrl) ||
        !readRequiredString(profileObj, "bio", &bio) ||
        !readRequiredString(profileObj, "signature", &signature) ||
        !readRequiredString(profileObj, "theme", &theme)) {
      if (error) {
        *error = QStringLiteral("invalid profile fields for PROFILE/GET");
      }
      return false;
    }
  } else {
    if (!readRequiredString(profileObj, "avatar_url", &avatarUrl) ||
        !readRequiredString(profileObj, "nickname", &nickname) ||
        !readRequiredString(profileObj, "signature", &signature)) {
      if (error) {
        *error = QStringLiteral(
            "invalid profile fields, avatar_url/nickname/signature must be string");
      }
      return false;
    }
    userId = jsonValueToString(profileObj.value("user_id"));
    numericId = jsonValueToString(profileObj.value("numeric_id"));
    username = profileObj.value("username").toString();
    email = profileObj.value("email").toString();
    phone = profileObj.value("phone").toString();
    userUuid = profileObj.value("user_uuid").toString();
    bio = profileObj.value("bio").toString();
    readRequiredInt(profileObj, "status", &statusValue);
    theme = profileObj.value("theme").toString();
  }

  outInfo->userId = userId;
  outInfo->numericId = numericId;
  outInfo->username = username;
  outInfo->email = email;
  outInfo->phone = phone;
  outInfo->status = statusValue;
  outInfo->userUuid = userUuid;
  outInfo->avatarUrl = avatarUrl;
  outInfo->nickname = nickname;
  outInfo->bio = bio;
  outInfo->signature = signature;
  outInfo->theme =
      theme.trimmed().isEmpty() ? QStringLiteral("default") : normalizeTheme(theme);
  return true;
}

bool ProfileApiClient::parseAddFriendResult(const QJsonObject &data,
                                            AddFriendResult *outResult,
                                            QString *error) const {
  if (!outResult) {
    if (error) {
      *error = QStringLiteral("internal error: out result is null");
    }
    return false;
  }

  QString userNumericId;
  QString friendNumericId;
  QString userId;
  QString friendUserId;
  int status = 0;
  if (!readRequiredString(data, "user_numeric_id", &userNumericId, true) ||
      !readRequiredString(data, "friend_numeric_id", &friendNumericId, true) ||
      !readRequiredString(data, "user_id", &userId, true) ||
      !readRequiredString(data, "friend_user_id", &friendUserId, true) ||
      !readRequiredInt(data, "status", &status)) {
    if (error) {
      *error = QStringLiteral("invalid ADD_FRIEND response fields");
    }
    return false;
  }

  outResult->userNumericId = userNumericId;
  outResult->friendNumericId = friendNumericId;
  outResult->userId = userId;
  outResult->friendUserId = friendUserId;
  outResult->status = status;
  return true;
}

bool ProfileApiClient::parseDeleteFriendResult(const QJsonObject &data,
                                               const QString &requestId, int code,
                                               DeleteFriendResult *outResult,
                                               QString *error) const {
  if (!outResult) {
    if (error) {
      *error = QStringLiteral("internal error: out result is null");
    }
    return false;
  }

  QString message;
  QString userNumericId;
  QString friendNumericId;
  QString userId;
  QString friendUserId;
  int deletedRows = 0;
  bool ok = false;
  bool removed = false;
  if (!readRequiredBool(data, "ok", &ok) ||
      !readRequiredString(data, "message", &message) ||
      !readRequiredString(data, "user_numeric_id", &userNumericId, true) ||
      !readRequiredString(data, "friend_numeric_id", &friendNumericId, true) ||
      !readRequiredString(data, "user_id", &userId, true) ||
      !readRequiredString(data, "friend_user_id", &friendUserId, true) ||
      !readRequiredInt(data, "deleted_rows", &deletedRows) ||
      !readRequiredBool(data, "removed", &removed)) {
    if (error) {
      *error = QStringLiteral("invalid DELETE_FRIEND response fields");
    }
    return false;
  }

  outResult->ok = ok;
  outResult->message = message.trimmed();
  outResult->userNumericId = userNumericId;
  outResult->friendNumericId = friendNumericId;
  outResult->userId = userId;
  outResult->friendUserId = friendUserId;
  outResult->deletedRows = deletedRows;
  outResult->removed = removed;
  outResult->requestId = requestId;
  outResult->code = code;
  return true;
}

bool ProfileApiClient::parseFriendList(const QJsonObject &data,
                                       QVector<FriendItem> *outFriends,
                                       QString *error) const {
  if (!outFriends) {
    if (error) {
      *error = QStringLiteral("internal error: out friends is null");
    }
    return false;
  }
  QString ignored;
  if (!readRequiredString(data, "numeric_id", &ignored, true) ||
      !readRequiredString(data, "user_id", &ignored, true)) {
    if (error) {
      *error = QStringLiteral("invalid LIST_FRIENDS response owner fields");
    }
    return false;
  }

  const QJsonValue friendsVal = data.value("friends");
  if (friendsVal.isUndefined() || friendsVal.isNull()) {
    outFriends->clear();
    return true;
  }
  if (!friendsVal.isArray()) {
    if (error) {
      *error = QStringLiteral("invalid LIST_FRIENDS response: friends is not array");
    }
    return false;
  }

  const QJsonArray arr = friendsVal.toArray();
  outFriends->clear();
  outFriends->reserve(arr.size());
  for (const QJsonValue &itemVal : arr) {
    if (!itemVal.isObject()) {
      continue;
    }
    const QJsonObject obj = itemVal.toObject();
    FriendItem item;
    item.userId = jsonValueToString(obj.value("user_id"));
    item.numericId = jsonValueToString(obj.value("numeric_id"));
    item.username = obj.value("username").toString();
    item.status = readOptionalInt(obj, "status", 0);
    item.userStatus = readOptionalInt(obj, "user_status", item.status);
    item.isOnline = readOptionalBool(obj, "is_online", false);
    item.lastSeenAtUtc = obj.value("last_seen_at").toString().trimmed();
    item.lastSeenAt = parseUtcIsoTime(item.lastSeenAtUtc);
    item.nickname = obj.value("nickname").toString();
    item.avatarUrl = obj.value("avatar_url").toString();
    item.bio = obj.value("bio").toString();
    outFriends->push_back(item);
  }
  return true;
}
