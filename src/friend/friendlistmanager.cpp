#include "friendlistmanager.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonValue>
#include <QListWidget>
#include <QListWidgetItem>
#include <QTimeZone>
#include <QtGlobal>

namespace {
QString valueToString(const QJsonValue &value) {
  if (value.isString()) {
    return value.toString().trimmed();
  }
  if (value.isDouble()) {
    return QString::number(static_cast<qint64>(value.toDouble()));
  }
  return QString();
}

int valueToInt(const QJsonValue &value, int defaultValue = 0) {
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

bool valueToBool(const QJsonValue &value, bool defaultValue = false) {
  if (value.isBool()) {
    return value.toBool();
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
} // namespace

namespace friendlist {

bool FriendListManager::updateFromJson(const QByteArray &jsonBytes) {
  QJsonParseError parseError;
  const QJsonDocument doc = QJsonDocument::fromJson(jsonBytes, &parseError);
  if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
    qWarning() << "[FriendList] invalid json payload, error="
               << parseError.errorString();
    return false;
  }
  return updateFromResponse(doc.object());
}

bool FriendListManager::updateFromResponse(const QJsonObject &data) {
  const QJsonValue friendsValue = data.value("friends");
  if (!friendsValue.isArray()) {
    qWarning() << "[FriendList] invalid response: friends is not array";
    return false;
  }

  const QJsonArray friendsArray = friendsValue.toArray();
  QList<FriendItem> parsed;
  parsed.reserve(friendsArray.size());

  for (int i = 0; i < friendsArray.size(); ++i) {
    const QJsonValue itemValue = friendsArray.at(i);
    if (!itemValue.isObject()) {
      qWarning() << "[FriendList] skip non-object friend item at index" << i;
      continue;
    }

    const QJsonObject obj = itemValue.toObject();
    FriendItem item;
    item.conversationId = valueToString(obj.value("conversation_uuid"));
    if (item.conversationId.isEmpty()) {
      item.conversationId = valueToString(obj.value("conversation_id"));
    }
    item.userId = valueToString(obj.value("user_id"));
    item.numericId = valueToString(obj.value("numeric_id"));
    item.username = valueToString(obj.value("username"));
    item.nickname = valueToString(obj.value("nickname"));
    item.avatarUrl = valueToString(obj.value("avatar_url"));
    item.bio = valueToString(obj.value("bio"));
    item.status = valueToInt(obj.value("status"), 0);
    item.userStatus = valueToInt(obj.value("user_status"), item.status);
    item.isOnline = valueToBool(obj.value("is_online"), false);
    item.lastSeenAtUtc = valueToString(obj.value("last_seen_at"));
    item.lastSeenAt = parseUtcIsoTime(item.lastSeenAtUtc);

    if (item.userId.isEmpty() || item.numericId.isEmpty() ||
        item.username.isEmpty()) {
      qWarning() << "[FriendList] skip invalid item at index" << i
                 << "required field missing, user_id/numeric_id/username";
      continue;
    }

    item.displayName = item.nickname.isEmpty() ? item.username : item.nickname;
    parsed.push_back(item);
  }

  m_friends = parsed;
  qInfo() << "[FriendList] sync completed, size=" << m_friends.size();
  return true;
}

bool FriendListManager::applyPresenceUpdate(const QString &userId,
                                            const QString &numericId,
                                            bool isOnline,
                                            const QString &lastSeenAtUtc,
                                            FriendItem *updatedFriend) {
  const QString trimmedUserId = userId.trimmed();
  const QString trimmedNumericId = numericId.trimmed();

  for (FriendItem &item : m_friends) {
    const bool userIdMatched =
        !trimmedUserId.isEmpty() && item.userId == trimmedUserId;
    const bool numericIdMatched =
        !trimmedNumericId.isEmpty() && item.numericId == trimmedNumericId;
    if (!userIdMatched && !numericIdMatched) {
      continue;
    }

    item.isOnline = isOnline;
    item.lastSeenAtUtc = lastSeenAtUtc.trimmed();
    item.lastSeenAt = parseUtcIsoTime(item.lastSeenAtUtc);
    item.displayName = item.nickname.isEmpty() ? item.username : item.nickname;

    if (updatedFriend) {
      *updatedFriend = item;
    }

    qInfo().noquote()
        << "[FriendList] applied presence update user_id=" << item.userId
        << "numeric_id=" << item.numericId << "is_online=" << item.isOnline
        << "last_seen_at=" << item.lastSeenAtUtc;
    return true;
  }

  return false;
}

const QList<FriendItem> &FriendListManager::friends() const { return m_friends; }

void FriendListManager::clear() { m_friends.clear(); }

void FriendListManager::refreshListWidget(QListWidget *listWidget,
                                          const QList<FriendItem> &friends) {
  if (!listWidget) {
    qWarning() << "[FriendList] refresh UI skipped: listWidget is null";
    return;
  }

  listWidget->clear();
  if (friends.isEmpty()) {
    auto *emptyItem = new QListWidgetItem(QStringLiteral("暂无好友"));
    emptyItem->setFlags(emptyItem->flags() & ~Qt::ItemIsSelectable &
                        ~Qt::ItemIsEnabled);
    listWidget->addItem(emptyItem);
    return;
  }

  for (const FriendItem &friendItem : friends) {
    const QString avatarTag =
        friendItem.avatarUrl.trimmed().isEmpty() ? QStringLiteral("[默认头像]")
                                                 : QStringLiteral("[头像]");
    const QString text =
        QStringLiteral("%1 (%2) %3")
            .arg(friendItem.displayName, friendItem.numericId, avatarTag);
    auto *item = new QListWidgetItem(text);
    item->setToolTip(friendItem.bio.trimmed().isEmpty()
                         ? QStringLiteral("无个性签名")
                         : friendItem.bio.trimmed());
    item->setData(Qt::UserRole, friendItem.userId);
    item->setData(Qt::UserRole + 1, friendItem.numericId);
    listWidget->addItem(item);
  }
}

} // namespace friendlist
