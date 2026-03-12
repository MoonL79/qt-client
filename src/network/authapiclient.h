#ifndef AUTHAPICLIENT_H
#define AUTHAPICLIENT_H

#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QTimer>

#include "protocol.h"
#include "websocketclient.h"

struct AuthUserInfo {
  QString userId;
  QString numericId;
  QString username;
  QString email;
  QString phone;
  int status = 0;
  QString userUuid;
  QString nickname;
  QString avatarUrl;
  QString bio;
};
Q_DECLARE_METATYPE(AuthUserInfo)

struct PresenceInfo {
  bool hasPresence = false;
  bool isOnline = false;
  QString lastSeenAtUtc;
  QDateTime lastSeenAt;
};
Q_DECLARE_METATYPE(PresenceInfo)

struct LoginResult {
  bool ok = false;
  QString message;
  QString requestId;
  int code = -1;
  AuthUserInfo user;
  QString uploadToken;
  QString uploadTokenType;
  QString uploadTokenExpiresAtUtc;
  QDateTime uploadTokenExpiresAt;
  PresenceInfo presence;
};
Q_DECLARE_METATYPE(LoginResult)

struct LogoutResult {
  bool ok = false;
  QString message;
  QString requestId;
  int code = -1;
  QString userId;
  QString numericId;
  bool offline = false;
  QString lastSeenAtUtc;
  QDateTime lastSeenAt;
};
Q_DECLARE_METATYPE(LogoutResult)

class AuthApiClient : public QObject {
  Q_OBJECT

public:
  explicit AuthApiClient(websocketclient *client = websocketclient::instance(),
                         QObject *parent = nullptr);

  QString login(const QString &username, const QString &password);
  QString logout();
  QString logout(const QString &token);

  static bool isCurrentLoginResponse(const protocol::Envelope &envelope,
                                     const QString &pendingRequestId);
  static QString extractAuthErrorMessage(const protocol::Envelope &envelope,
                                         const QString &fallbackAction = QString());
  static bool isLoginSuccessEnvelope(const protocol::Envelope &envelope);
  static bool parseLoginResult(const protocol::Envelope &envelope,
                               LoginResult *outResult, QString *error = nullptr);

signals:
  void loginSucceeded(const QString &requestId, const LoginResult &result);
  void logoutSucceeded(const QString &requestId, const LogoutResult &result);
  void authRequestFailed(const QString &requestId, const QString &action,
                         const QString &error);
  void authRequestFailedDetailed(const QString &requestId, const QString &action,
                                 int code, const QString &error);

private slots:
  void onTextMessageReceived(const QString &message);
  void onDisconnected();

private:
  struct PendingRequest {
    QString action;
    QPointer<QTimer> timer;
  };

  QString generateRequestId() const;
  bool sendAuthPayload(const QString &action, const QString &requestId,
                       const QJsonObject &data);
  void addPendingRequest(const QString &requestId, const QString &action);
  void clearPendingRequest(const QString &requestId);
  void failRequest(const QString &requestId, const QString &action,
                   const QString &errorMessage, int code = -1);
  bool parseLogoutResult(const protocol::Envelope &envelope, LogoutResult *outResult,
                         QString *error = nullptr) const;

private:
  websocketclient *m_client = nullptr;
  QHash<QString, PendingRequest> m_pendingRequests;
  static constexpr int kRequestTimeoutMs = 8 * 1000;
};

#endif // AUTHAPICLIENT_H
