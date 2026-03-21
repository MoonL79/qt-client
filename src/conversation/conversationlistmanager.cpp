#include "conversationlistmanager.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonValue>
#include <QTimeZone>

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

QString resolveDisplayName(const conversationlist::ConversationItem &item) {
  if (!item.name.trimmed().isEmpty()) {
    return item.name.trimmed();
  }
  if (!item.peerNickname.trimmed().isEmpty()) {
    return item.peerNickname.trimmed();
  }
  if (!item.peerUsername.trimmed().isEmpty()) {
    return item.peerUsername.trimmed();
  }
  if (!item.peerNumericId.trimmed().isEmpty()) {
    return item.peerNumericId.trimmed();
  }
  return item.conversationId.trimmed();
}
} // namespace

namespace conversationlist {

bool ConversationListManager::updateFromJson(const QByteArray &jsonBytes) {
  QJsonParseError parseError;
  const QJsonDocument doc = QJsonDocument::fromJson(jsonBytes, &parseError);
  if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
    qWarning() << "[ConversationList] invalid json payload, error="
               << parseError.errorString();
    return false;
  }
  return updateFromResponse(doc.object());
}

bool ConversationListManager::updateFromResponse(const QJsonObject &data) {
  const QJsonValue conversationsValue = data.value("conversations");
  if (!conversationsValue.isArray()) {
    qWarning() << "[ConversationList] invalid response: conversations is not array";
    return false;
  }

  const QJsonArray conversationsArray = conversationsValue.toArray();
  QList<ConversationItem> parsed;
  parsed.reserve(conversationsArray.size());

  for (int i = 0; i < conversationsArray.size(); ++i) {
    const QJsonValue itemValue = conversationsArray.at(i);
    if (!itemValue.isObject()) {
      qWarning() << "[ConversationList] skip non-object conversation at index" << i;
      continue;
    }

    const QJsonObject obj = itemValue.toObject();
    ConversationItem item;
    item.conversationId = valueToString(obj.value("conversation_id"));
    item.conversationUuid = valueToString(obj.value("conversation_uuid"));
    item.conversationType = valueToInt(obj.value("conversation_type"), 0);
    item.name = valueToString(obj.value("name"));
    item.avatarUrl = valueToString(obj.value("avatar_url"));
    item.memberCount = valueToInt(obj.value("member_count"), 0);
    item.peerUserId = valueToString(obj.value("peer_user_id"));
    item.peerNumericId = valueToString(obj.value("peer_numeric_id"));
    item.peerUsername = valueToString(obj.value("peer_username"));
    item.peerNickname = valueToString(obj.value("peer_nickname"));
    item.peerAvatarUrl = valueToString(obj.value("peer_avatar_url"));
    item.peerBio = valueToString(obj.value("peer_bio"));
    item.peerStatus = valueToInt(obj.value("peer_status"), 0);
    item.peerIsOnline = valueToBool(obj.value("peer_is_online"), false);
    item.peerLastSeenAt = valueToString(obj.value("peer_last_seen_at"));
    item.peerLastSeenAtUtc = parseUtcIsoTime(item.peerLastSeenAt);

    if (item.conversationId.isEmpty()) {
      qWarning() << "[ConversationList] skip invalid item at index" << i
                 << "missing conversation_id";
      continue;
    }

    if (item.conversationUuid.isEmpty()) {
      item.conversationUuid = item.conversationId;
    }
    if (item.name.isEmpty()) {
      item.name = resolveDisplayName(item);
    }
    if (item.avatarUrl.isEmpty()) {
      item.avatarUrl = item.peerAvatarUrl;
    }

    parsed.push_back(item);
  }

  m_conversations = parsed;
  qInfo() << "[ConversationList] sync completed, size=" << m_conversations.size();
  return true;
}

bool ConversationListManager::applyPeerPresenceUpdate(
    const QString &userId, const QString &numericId, bool isOnline,
    const QString &lastSeenAtUtc, ConversationItem *updatedConversation) {
  const QString trimmedUserId = userId.trimmed();
  const QString trimmedNumericId = numericId.trimmed();

  for (ConversationItem &item : m_conversations) {
    const bool userIdMatched =
        !trimmedUserId.isEmpty() && item.peerUserId == trimmedUserId;
    const bool numericIdMatched =
        !trimmedNumericId.isEmpty() && item.peerNumericId == trimmedNumericId;
    if (!userIdMatched && !numericIdMatched) {
      continue;
    }

    item.peerIsOnline = isOnline;
    item.peerLastSeenAt = lastSeenAtUtc.trimmed();
    item.peerLastSeenAtUtc = parseUtcIsoTime(item.peerLastSeenAt);

    if (updatedConversation) {
      *updatedConversation = item;
    }

    qInfo().noquote() << "[ConversationList] applied presence update peer_user_id="
                      << item.peerUserId << "peer_numeric_id="
                      << item.peerNumericId << "is_online=" << item.peerIsOnline
                      << "last_seen_at=" << item.peerLastSeenAt;
    return true;
  }

  return false;
}

const QList<ConversationItem> &ConversationListManager::conversations() const {
  return m_conversations;
}

void ConversationListManager::clear() { m_conversations.clear(); }

} // namespace conversationlist
