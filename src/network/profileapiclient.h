#ifndef PROFILEAPICLIENT_H
#define PROFILEAPICLIENT_H

#include <QObject>
#include <QHash>
#include <QPointer>
#include <QTimer>

#include "protocol.h"
#include "websocketclient.h"

struct ProfileInfo {
  QString avatarUrl;
  QString nickname;
  QString signature;
  QString theme;
};
Q_DECLARE_METATYPE(ProfileInfo)

class ProfileApiClient : public QObject {
  Q_OBJECT

public:
  explicit ProfileApiClient(websocketclient *client = websocketclient::instance(),
                            QObject *parent = nullptr);

  QString requestProfileInfo(const QString &userId);
  QString setProfileInfo(const QString &userId, const QString &avatarUrl,
                         const QString &nickname, const QString &signature,
                         const QString &theme = QString());

signals:
  void profileInfoReceived(const QString &requestId, const ProfileInfo &info);
  void profileInfoSetSuccess(const QString &requestId, const ProfileInfo &info);
  void requestFailed(const QString &requestId, const QString &action,
                     const QString &error);

private slots:
  void onTextMessageReceived(const QString &message);
  void onDisconnected();

private:
  struct PendingRequest {
    QString action;
    QPointer<QTimer> timer;
  };

  QString generateRequestId() const;
  bool validateGetInfo(const QString &userId, QString *error) const;
  bool validateSetInfo(const QString &userId, const QString &avatarUrl,
                       const QString &nickname, const QString &signature,
                       const QString &theme, QString *error) const;

  void sendProfileRequest(const QString &action, const QString &requestId,
                          const QJsonObject &data);
  void addPendingRequest(const QString &requestId, const QString &action);
  void clearPendingRequest(const QString &requestId);
  void failRequest(const QString &requestId, const QString &action,
                   const QString &errorMessage);

  bool parseProfileInfo(const QJsonObject &data, ProfileInfo *outInfo,
                        QString *error) const;

private:
  websocketclient *m_client = nullptr;
  QHash<QString, PendingRequest> m_pendingRequests;
  static constexpr int kRequestTimeoutMs = 10 * 1000;
};

#endif // PROFILEAPICLIENT_H
