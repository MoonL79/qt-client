#ifndef USERSESSION_H
#define USERSESSION_H

#include <QDateTime>
#include <QString>

class UserSession {
public:
  static UserSession &instance();

  void clear();
  void setLoginContext(const QString &userId, const QString &username,
                       const QString &numericId,
                       const QString &uploadToken,
                       const QString &uploadTokenType,
                       const QString &uploadTokenExpiresAtUtc, bool isOnline,
                       const QString &lastSeenAtUtc);
  void setPresence(bool isOnline, const QString &lastSeenAtUtc);

  bool isLoggedIn() const;
  bool hasValidUploadToken() const;
  bool isUploadTokenExpired() const;

  const QString &userId() const;
  const QString &username() const;
  const QString &numericId() const;
  const QString &uploadToken() const;
  const QString &uploadTokenType() const;
  const QString &uploadTokenExpiresAtUtc() const;
  bool isOnline() const;
  const QString &lastSeenAtUtc() const;
  const QDateTime &lastSeenAt() const;
  QString authorizationHeaderValue() const;

private:
  UserSession() = default;

  QString m_userId;
  QString m_username;
  QString m_numericId;
  QString m_uploadToken;
  QString m_uploadTokenType;
  QString m_uploadTokenExpiresAtUtc;
  QDateTime m_uploadTokenExpiresAt;
  bool m_isOnline = false;
  QString m_lastSeenAtUtc;
  QDateTime m_lastSeenAt;
};

#endif // USERSESSION_H
