#ifndef PROFILEAPICLIENT_H
#define PROFILEAPICLIENT_H

#include <QDateTime>
#include <QObject>
#include <QHash>
#include <QJsonObject>
#include <QPointer>
#include <QTimer>
#include <QVector>

#include "protocol.h"
#include "websocketclient.h"

struct ProfileInfo {
  QString userId;
  QString numericId;
  QString username;
  QString email;
  QString phone;
  int status = 0;
  QString userUuid;
  QString avatarUrl;
  QString nickname;
  QString bio;
  QString signature;
  QString theme;
};
Q_DECLARE_METATYPE(ProfileInfo)

struct AddFriendResult {
  QString userNumericId;
  QString friendNumericId;
  QString userId;
  QString friendUserId;
  int status = 0;
};
Q_DECLARE_METATYPE(AddFriendResult)

struct DeleteFriendRequest {
  QString userNumericId;
  QString friendNumericId;
};
Q_DECLARE_METATYPE(DeleteFriendRequest)

struct DeleteFriendResult {
  bool ok = false;
  QString message;
  QString userNumericId;
  QString friendNumericId;
  QString userId;
  QString friendUserId;
  int deletedRows = 0;
  bool removed = false;
  QString requestId;
  int code = -1;
};
Q_DECLARE_METATYPE(DeleteFriendResult)

struct FriendItem {
  QString userId;
  QString numericId;
  QString username;
  int status = 0;
  int userStatus = 0;
  bool isOnline = false;
  QString lastSeenAtUtc;
  QDateTime lastSeenAt;
  QString nickname;
  QString avatarUrl;
  QString bio;
};
Q_DECLARE_METATYPE(FriendItem)
Q_DECLARE_METATYPE(QVector<FriendItem>)

struct ConversationItem {
  QString conversationId;
  QString conversationUuid;
  int conversationType = 0;
  QString name;
  QString avatarUrl;
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
Q_DECLARE_METATYPE(ConversationItem)
Q_DECLARE_METATYPE(QVector<ConversationItem>)

class ProfileApiClient : public QObject {
  Q_OBJECT

public:
  explicit ProfileApiClient(websocketclient *client = websocketclient::instance(),
                            QObject *parent = nullptr);

  QString requestProfileInfo(const QString &userId);
  QString setProfileInfo(const QString &userId, const QString &avatarUrl,
                         const QString &nickname, const QString &signature,
                         const QString &theme = QString());
  QString queryUserProfile(const QString &numericId);
  QString addFriend(const QString &userNumericId, const QString &friendNumericId,
                    const QString &remark = QString());
  QString deleteFriend(const QString &userNumericId,
                       const QString &friendNumericId);
  QString fetchFriendList(const QString &myNumericId);
  QString fetchConversationList(const QString &myNumericId);

signals:
  void profileInfoReceived(const QString &requestId, const ProfileInfo &info);
  void profileInfoSetSuccess(const QString &requestId, const ProfileInfo &info);
  void userProfileQueried(const QString &requestId, const ProfileInfo &info);
  void addFriendSuccess(const QString &requestId, const AddFriendResult &result);
  void deleteFriendFinished(const QString &requestId,
                            const DeleteFriendResult &result);
  void friendListFetched(const QString &requestId,
                         const QVector<FriendItem> &friends);
  void friendListPayloadReceived(const QString &requestId,
                                 const QJsonObject &data);
  void friendListFailed(const QString &requestId, int code,
                        const QString &message);
  void conversationListFetched(const QString &requestId,
                               const QVector<ConversationItem> &conversations);
  void conversationListPayloadReceived(const QString &requestId,
                                       const QJsonObject &data);
  void conversationListFailed(const QString &requestId, int code,
                              const QString &message);
  void requestFailed(const QString &requestId, const QString &action,
                     const QString &error);
  void requestFailedDetailed(const QString &requestId, const QString &action,
                             int code, const QString &error);

private slots:
  void onTextMessageReceived(const QString &message);
  void onDisconnected();

private:
  struct PendingRequest {
    QString action;
    QJsonObject data;
    int remainingRetries = 0;
    bool retryOnTransient = false;
    QPointer<QTimer> timer;
  };

  QString generateRequestId() const;
  bool validateGetInfo(const QString &userId, QString *error) const;
  bool validateSetInfo(const QString &userId, const QString &avatarUrl,
                       const QString &nickname, const QString &signature,
                       const QString &theme, QString *error) const;
  bool validateQueryUserProfile(const QString &numericId, QString *error) const;
  bool validateAddFriend(const QString &userNumericId,
                         const QString &friendNumericId, const QString &remark,
                         QString *error) const;
  bool validateDeleteFriend(const QString &userNumericId,
                            const QString &friendNumericId,
                            QString *error) const;
  bool validateFetchFriendList(const QString &myNumericId, QString *error) const;
  bool validateFetchConversationList(const QString &myNumericId,
                                     QString *error) const;

  void sendProfileRequest(const QString &action, const QString &requestId,
                          const QJsonObject &data, int retries,
                          bool retryOnTransient);
  bool sendProfilePayload(const QString &action, const QString &requestId,
                          const QJsonObject &data);
  void addPendingRequest(const QString &requestId, const QString &action,
                         const QJsonObject &data, int retries,
                         bool retryOnTransient);
  void clearPendingRequest(const QString &requestId);
  bool retryPendingRequest(const QString &requestId, const QString &reason);
  void failRequest(const QString &requestId, const QString &action,
                   const QString &errorMessage, int code = -1);

  bool parseProfileInfo(const QJsonObject &data, ProfileInfo *outInfo, bool strict,
                        QString *error) const;
  bool parseAddFriendResult(const QJsonObject &data, AddFriendResult *outResult,
                            QString *error) const;
  bool parseDeleteFriendResult(const QJsonObject &data, const QString &requestId,
                               int code, DeleteFriendResult *outResult,
                               QString *error) const;
  bool parseFriendList(const QJsonObject &data, QVector<FriendItem> *outFriends,
                       QString *error) const;
  bool parseConversationList(const QJsonObject &data,
                             QVector<ConversationItem> *outConversations,
                             QString *error) const;

private:
  websocketclient *m_client = nullptr;
  QHash<QString, PendingRequest> m_pendingRequests;
  static constexpr int kRequestTimeoutMs = 8 * 1000;
  static constexpr int kRetryDelayMs = 500;
  static constexpr int kMaxRetryCount = 1;
};

#endif // PROFILEAPICLIENT_H
