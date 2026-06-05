#include "usersession.h"
#include <QTimeZone>

namespace {
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
}

/**
 * @brief 执行instance的核心逻辑。
 * @return 返回 UserSession 结果。
 */
UserSession &UserSession::instance() {
  static UserSession s;
  return s;
}

/**
 * @brief 清理clear状态。
 * @return 无返回值。
 */
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

/**
 * @brief 设置登录context值。
 * @param userId 用户 ID。
 * @param username 用户名。
 * @param numericId 数字编号。
 * @param uploadToken 字符串参数 `uploadToken`。
 * @param uploadTokenType 字符串参数 `uploadTokenType`。
 * @param uploadTokenExpiresAtUtc 字符串参数 `uploadTokenExpiresAtUtc`。
 * @param isOnline 在线状态标记。
 * @param lastSeenAtUtc 最近在线时间字符串。
 * @return 无返回值。
 */
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

/**
 * @brief 设置在线状态值。
 * @param isOnline 在线状态标记。
 * @param lastSeenAtUtc 最近在线时间字符串。
 * @return 无返回值。
 */
void UserSession::setPresence(bool isOnline, const QString &lastSeenAtUtc) {
  m_isOnline = isOnline;
  m_lastSeenAtUtc = lastSeenAtUtc.trimmed();
  m_lastSeenAt = parseUtcIsoTime(m_lastSeenAtUtc);
}

/**
 * @brief 判断empty条件是否满足。
 * @return 返回条件判断结果，`true` 表示满足，`false` 表示不满足。
 */
bool UserSession::isLoggedIn() const { return !m_userId.isEmpty(); }

/**
 * @brief 判断valid上传令牌条件是否满足。
 * @return 返回条件判断结果，`true` 表示满足，`false` 表示不满足。
 */
bool UserSession::hasValidUploadToken() const {
  if (m_uploadToken.isEmpty()) {
    return false;
  }
  if (m_uploadTokenType.isEmpty()) {
    return false;
  }
  return !isUploadTokenExpired();
}

/**
 * @brief 判断上传令牌expired条件是否满足。
 * @return 返回条件判断结果，`true` 表示满足，`false` 表示不满足。
 */
bool UserSession::isUploadTokenExpired() const {
  if (!m_uploadTokenExpiresAt.isValid()) {
    return true;
  }
  return QDateTime::currentDateTimeUtc() >= m_uploadTokenExpiresAt;
}

/**
 * @brief 实现 userId 的核心逻辑。
 * @return 返回处理后的字符串结果。
 */
const QString &UserSession::userId() const { return m_userId; }

/**
 * @brief 实现 username 的核心逻辑。
 * @return 返回处理后的字符串结果。
 */
const QString &UserSession::username() const { return m_username; }

/**
 * @brief 实现 numericId 的核心逻辑。
 * @return 返回处理后的字符串结果。
 */
const QString &UserSession::numericId() const { return m_numericId; }

/**
 * @brief 实现 uploadToken 的核心逻辑。
 * @return 返回处理后的字符串结果。
 */
const QString &UserSession::uploadToken() const { return m_uploadToken; }

/**
 * @brief 实现 uploadTokenType 的核心逻辑。
 * @return 返回处理后的字符串结果。
 */
const QString &UserSession::uploadTokenType() const { return m_uploadTokenType; }

/**
 * @brief 执行uploadTokenExpiresAtUtc的核心逻辑。
 * @return 返回处理后的字符串结果。
 */
const QString &UserSession::uploadTokenExpiresAtUtc() const {
  return m_uploadTokenExpiresAtUtc;
}

/**
 * @brief 判断在线条件是否满足。
 * @return 返回条件判断结果，`true` 表示满足，`false` 表示不满足。
 */
bool UserSession::isOnline() const { return m_isOnline; }

/**
 * @brief 实现 lastSeenAtUtc 的核心逻辑。
 * @return 返回处理后的字符串结果。
 */
const QString &UserSession::lastSeenAtUtc() const { return m_lastSeenAtUtc; }

/**
 * @brief 实现 lastSeenAt 的核心逻辑。
 * @return 返回 QDateTime 结果。
 */
const QDateTime &UserSession::lastSeenAt() const { return m_lastSeenAt; }

/**
 * @brief 执行authorizationHeaderValue的核心逻辑。
 * @return 返回处理后的字符串结果。
 */
QString UserSession::authorizationHeaderValue() const {
  if (m_uploadTokenType.isEmpty() || m_uploadToken.isEmpty()) {
    return QString();
  }
  return QString("%1 %2").arg(m_uploadTokenType, m_uploadToken);
}



