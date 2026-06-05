#include "usersession.h"
#include <QTimeZone>

namespace {
// 解析utciso时间并生成内部结果。
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

// 实现 `instance` 的核心逻辑。
UserSession &UserSession::instance() {
  static UserSession s;
  return s;
}

// 清理`clear`状态。
void UserSession::clear() {
  m_userId.clear();
  m_username.clear();
  m_numericId.clear();
  m_uploadToken.clear();
  m_uploadTokenType.clear();
  m_uploadTokenExpiresAtUtc.clear();
  m_uploadTokenExpiresAt = QDateTime();
  m_isOnline = false;
  m_lastSeenAtUtc.clear();
  m_lastSeenAt = QDateTime();
}

// 设置登录context值。
void UserSession::setLoginContext(const QString &userId, const QString &username,
                                  const QString &numericId,
                                  const QString &uploadToken,
                                  const QString &uploadTokenType,
                                  const QString &uploadTokenExpiresAtUtc,
                                  bool isOnline,
                                  const QString &lastSeenAtUtc) {
  m_userId = userId.trimmed();
  m_username = username.trimmed();
  m_numericId = numericId.trimmed();
  m_uploadToken = uploadToken.trimmed();
  m_uploadTokenType = uploadTokenType.trimmed();
  m_uploadTokenExpiresAtUtc = uploadTokenExpiresAtUtc.trimmed();
  m_uploadTokenExpiresAt = parseUtcIsoTime(m_uploadTokenExpiresAtUtc);
  setPresence(isOnline, lastSeenAtUtc);
}

// 设置在线状态值。
void UserSession::setPresence(bool isOnline, const QString &lastSeenAtUtc) {
  m_isOnline = isOnline;
  m_lastSeenAtUtc = lastSeenAtUtc.trimmed();
  m_lastSeenAt = parseUtcIsoTime(m_lastSeenAtUtc);
}

// 判断empty条件是否满足。
bool UserSession::isLoggedIn() const { return !m_userId.isEmpty(); }

// 判断valid上传令牌条件是否满足。
bool UserSession::hasValidUploadToken() const {
  if (m_uploadToken.isEmpty()) {
    return false;
  }
  if (m_uploadTokenType.isEmpty()) {
    return false;
  }
  return !isUploadTokenExpired();
}

// 判断上传令牌expired条件是否满足。
bool UserSession::isUploadTokenExpired() const {
  if (!m_uploadTokenExpiresAt.isValid()) {
    return true;
  }
  return QDateTime::currentDateTimeUtc() >= m_uploadTokenExpiresAt;
}

// 实现 `userId` 的核心逻辑。
const QString &UserSession::userId() const { return m_userId; }

// 实现 `username` 的核心逻辑。
const QString &UserSession::username() const { return m_username; }

// 实现 `numericId` 的核心逻辑。
const QString &UserSession::numericId() const { return m_numericId; }

// 实现 `uploadToken` 的核心逻辑。
const QString &UserSession::uploadToken() const { return m_uploadToken; }

// 实现 `uploadTokenType` 的核心逻辑。
const QString &UserSession::uploadTokenType() const { return m_uploadTokenType; }

// 实现 `uploadTokenExpiresAtUtc` 的核心逻辑。
const QString &UserSession::uploadTokenExpiresAtUtc() const {
  return m_uploadTokenExpiresAtUtc;
}

// 判断在线条件是否满足。
bool UserSession::isOnline() const { return m_isOnline; }

// 实现 `lastSeenAtUtc` 的核心逻辑。
const QString &UserSession::lastSeenAtUtc() const { return m_lastSeenAtUtc; }

// 实现 `lastSeenAt` 的核心逻辑。
const QDateTime &UserSession::lastSeenAt() const { return m_lastSeenAt; }

// 实现 `authorizationHeaderValue` 的核心逻辑。
QString UserSession::authorizationHeaderValue() const {
  if (m_uploadTokenType.isEmpty() || m_uploadToken.isEmpty()) {
    return QString();
  }
  return QString("%1 %2").arg(m_uploadTokenType, m_uploadToken);
}
