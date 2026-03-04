#include "usersession.h"
#include <QTimeZone>

namespace {
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
}

UserSession &UserSession::instance() {
  static UserSession s;
  return s;
}

void UserSession::clear() {
  m_userId.clear();
  m_username.clear();
  m_uploadToken.clear();
  m_uploadTokenType.clear();
  m_uploadTokenExpiresAtUtc.clear();
  m_uploadTokenExpiresAt = QDateTime();
}

void UserSession::setLoginContext(const QString &userId, const QString &username,
                                  const QString &uploadToken,
                                  const QString &uploadTokenType,
                                  const QString &uploadTokenExpiresAtUtc) {
  m_userId = userId.trimmed();
  m_username = username.trimmed();
  m_uploadToken = uploadToken.trimmed();
  m_uploadTokenType = uploadTokenType.trimmed();
  m_uploadTokenExpiresAtUtc = uploadTokenExpiresAtUtc.trimmed();
  m_uploadTokenExpiresAt = parseUtcIsoTime(m_uploadTokenExpiresAtUtc);
}

bool UserSession::isLoggedIn() const { return !m_userId.isEmpty(); }

bool UserSession::hasValidUploadToken() const {
  if (m_uploadToken.isEmpty()) {
    return false;
  }
  if (m_uploadTokenType.isEmpty()) {
    return false;
  }
  return !isUploadTokenExpired();
}

bool UserSession::isUploadTokenExpired() const {
  if (!m_uploadTokenExpiresAt.isValid()) {
    return true;
  }
  return QDateTime::currentDateTimeUtc() >= m_uploadTokenExpiresAt;
}

const QString &UserSession::userId() const { return m_userId; }

const QString &UserSession::username() const { return m_username; }

const QString &UserSession::uploadToken() const { return m_uploadToken; }

const QString &UserSession::uploadTokenType() const { return m_uploadTokenType; }

const QString &UserSession::uploadTokenExpiresAtUtc() const {
  return m_uploadTokenExpiresAtUtc;
}

QString UserSession::authorizationHeaderValue() const {
  if (m_uploadTokenType.isEmpty() || m_uploadToken.isEmpty()) {
    return QString();
  }
  return QString("%1 %2").arg(m_uploadTokenType, m_uploadToken);
}
