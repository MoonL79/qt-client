#ifndef CONVERSATIONLISTMANAGER_H
#define CONVERSATIONLISTMANAGER_H

#include <QByteArray>
#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QString>

namespace conversationlist {

struct ConversationItem {
  QString conversationId;
  QString conversationUuid;
  QString groupNumericId;
  int conversationType = 0;
  QString name;
  QString avatarUrl;
  int memberCount = 0;
  QString peerUserId;
  QString peerNumericId;
  QString peerUsername;
  QString peerNickname;
  QString peerAvatarUrl;
  QString peerBio;
  int peerStatus = 0;
  bool peerIsOnline = false;
  QString peerLastSeenAt;
  QDateTime peerLastSeenAtUtc;
};

class ConversationListManager {
public:
  bool updateFromJson(const QByteArray &jsonBytes);
  bool updateFromResponse(const QJsonObject &data);
  bool applyPeerPresenceUpdate(const QString &userId, const QString &numericId,
                               bool isOnline, const QString &lastSeenAtUtc,
                               ConversationItem *updatedConversation = nullptr);
  const QList<ConversationItem> &conversations() const;
  void clear();

private:
  QList<ConversationItem> m_conversations;
};

} // namespace conversationlist

#endif // CONVERSATIONLISTMANAGER_H

