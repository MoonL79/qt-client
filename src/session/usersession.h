#ifndef USERSESSION_H
#define USERSESSION_H

#include <QDateTime>
#include <QString>

class UserSession {
public:
  static UserSession &instance();

  void clear();
  void setLoginContext(const QString &userId, const QString &username,
                       const QString &uploadToken,
                       const QString &uploadTokenType,
                       const QString &uploadTokenExpiresAtUtc);

  bool isLoggedIn() const;
  bool hasValidUploadToken() const;
  bool isUploadTokenExpired() const;

  const QString &userId() const;
  const QString &username() const;
  const QString &uploadToken() const;
  const QString &uploadTokenType() const;
  const QString &uploadTokenExpiresAtUtc() const;
  QString authorizationHeaderValue() const;

private:
  UserSession() = default;

  QString m_userId;
  QString m_username;
  QString m_uploadToken;
  QString m_uploadTokenType;
  QString m_uploadTokenExpiresAtUtc;
  QDateTime m_uploadTokenExpiresAt;
};

#endif // USERSESSION_H
