#ifndef PROFILEAPICLIENT_H
#define PROFILEAPICLIENT_H

#include <QObject>
#include <QHash>
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

struct FriendItem {
  QString userId;
  QString numericId;
  QString username;
  int status = 0;
  QString nickname;
  QString avatarUrl;
  QString bio;
};
Q_DECLARE_METATYPE(FriendItem)
Q_DECLARE_METATYPE(QVector<FriendItem>)

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
  QString fetchFriendList(const QString &myNumericId);

signals:
  void profileInfoReceived(const QString &requestId, const ProfileInfo &info);
  void profileInfoSetSuccess(const QString &requestId, const ProfileInfo &info);
  void userProfileQueried(const QString &requestId, const ProfileInfo &info);
  void addFriendSuccess(const QString &requestId, const AddFriendResult &result);
  void friendListFetched(const QString &requestId,
                         const QVector<FriendItem> &friends);
  void friendListFailed(const QString &requestId, int code,
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
  bool validateFetchFriendList(const QString &myNumericId, QString *error) const;

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
  bool parseFriendList(const QJsonObject &data, QVector<FriendItem> *outFriends,
                       QString *error) const;

private:
  websocketclient *m_client = nullptr;
  QHash<QString, PendingRequest> m_pendingRequests;
  static constexpr int kRequestTimeoutMs = 8 * 1000;
  static constexpr int kRetryDelayMs = 500;
  static constexpr int kMaxRetryCount = 1;
};

#endif // PROFILEAPICLIENT_H
