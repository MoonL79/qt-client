#include "conversationlistmanager.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonValue>
#include <QTimeZone>

namespace {
/**
 * @brief 执行valueToString的核心逻辑。
 * @param value 待处理的值。
 * @return 返回处理后的字符串结果。
 */
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

qint64 valueToInt64(const QJsonValue &value, qint64 defaultValue = 0) {
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

bool valueToBool(const QJsonValue &value, bool defaultValue = false) {
  if (value.isBool()) {
    return value.toBool();
  }
  return defaultValue;
}

/**
 * @brief 解析utciso时间并生成内部结果。
 * @param value 待处理的值。
 * @return 返回 QDateTime 结果。
 */
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

/**
 * @brief 解析并确定display名称结果。
 * @param item 数据项对象。
 * @return 返回处理后的字符串结果。
 */
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

namespace {

/**
 * @brief 读取群组数字ID。
 * @param obj 输入的对象数据。
 * @return 返回处理后的字符串结果。
 */
QString readGroupNumericId(const QJsonObject &obj) {
  const QString groupNumericId = valueToString(obj.value("group_numeric_id"));
  if (!groupNumericId.isEmpty()) {
    return groupNumericId;
  }

  const QString numericId = valueToString(obj.value("numeric_id"));
  if (!numericId.isEmpty()) {
    return numericId;
  }

  return valueToString(obj.value("group_id"));
}

} // namespace

/**
 * @brief 更新fromJSON状态。
 * @param jsonBytes 对象参数 `jsonBytes`。
 * @return 返回本次处理是否成功。
 */
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

/**
 * @brief 更新from响应状态。
 * @param data 请求或响应数据对象。
 * @return 返回本次处理是否成功。
 */
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
    item.groupNumericId = readGroupNumericId(obj);
    item.conversationType = valueToInt(obj.value("conversation_type"), 0);
    item.name = valueToString(obj.value("name"));
    item.avatarUrl = valueToString(obj.value("avatar_url"));
    item.ownerUserId = valueToString(obj.value("owner_user_id"));
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
    item.lastMessageSeq = valueToInt64(obj.value("last_message_seq"), 0);
    item.lastMessageId = valueToString(obj.value("last_message_id"));
    item.lastMessageSentAt = valueToString(obj.value("last_message_sent_at"));
    item.updatedAt = valueToString(obj.value("updated_at"));

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

/**
 * @brief 应用peer在线状态更新配置。
 * @param userId 用户 ID。
 * @param numericId 数字编号。
 * @param isOnline 在线状态标记。
 * @param lastSeenAtUtc 最近在线时间字符串。
 * @param updatedConversation 会话相关标识或会话数据。
 * @return 返回布尔结果。
 */
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

/**
 * @brief 移除会话数据或状态。
 * @param conversationId 会话 ID。
 * @param groupNumericId 群组数字编号。
 * @param removedConversation 会话相关标识或会话数据。
 * @return 返回布尔结果。
 */
bool ConversationListManager::removeConversation(
    const QString &conversationId, const QString &groupNumericId,
    ConversationItem *removedConversation) {
  const QString trimmedConversationId = conversationId.trimmed();
  const QString trimmedGroupNumericId = groupNumericId.trimmed();

  for (auto it = m_conversations.begin(); it != m_conversations.end(); ++it) {
    const bool conversationIdMatched =
        !trimmedConversationId.isEmpty() && it->conversationId == trimmedConversationId;
    const bool groupNumericIdMatched =
        !trimmedGroupNumericId.isEmpty() && it->groupNumericId == trimmedGroupNumericId;
    if (!conversationIdMatched && !groupNumericIdMatched) {
      continue;
    }

    if (removedConversation) {
      *removedConversation = *it;
    }

    qInfo().noquote() << "[ConversationList] removed conversation_id="
                      << it->conversationId << "group_numeric_id="
                      << it->groupNumericId;
    m_conversations.erase(it);
    return true;
  }

  return false;
}

/**
 * @brief 执行conversations的核心逻辑。
 * @return 返回整理后的集合结果。
 */
const QList<ConversationItem> &ConversationListManager::conversations() const {
  return m_conversations;
}

/**
 * @brief 清理clear状态。
 * @return 无返回值。
 */
void ConversationListManager::clear() { m_conversations.clear(); }

} // namespace conversationlist



