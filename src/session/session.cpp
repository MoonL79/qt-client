#include "session.h"

#include <QUuid>

/**
 * @brief 构造并初始化会话对象。
 * @param id 会话自身的唯一标识。
 * @param displayName 会话显示名称。
 * @param type 会话类型枚举值。
 * @param conversationId 对应的会话 ID。
 * @param groupNumericId 群组数字编号；非群聊场景下可为空。
 * @return 无返回值。
 */
Session::Session(const QString &id, const QString &displayName, Type type,
                 const QString &conversationId,
                 const QString &groupNumericId)
    : m_id(id), m_displayName(displayName), m_type(type),
      m_conversationId(conversationId.trimmed()),
      m_groupNumericId(groupNumericId.trimmed()) {}

/**
 * @brief 创建create对象或数据。
 * @param displayName 字符串参数 `displayName`。
 * @param type 输入参数 `type`。
 * @param conversationId 会话 ID。
 * @param groupNumericId 群组数字编号。
 * @return 返回 Session 结果。
 */
Session Session::create(const QString &displayName, Type type,
                        const QString &conversationId,
                        const QString &groupNumericId) {
  const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
  return Session(id, displayName, type, conversationId, groupNumericId);
}

/**
 * @brief 实现 id 的核心逻辑。
 * @return 返回处理后的字符串结果。
 */
const QString &Session::id() const { return m_id; }

/**
 * @brief 实现 displayName 的核心逻辑。
 * @return 返回处理后的字符串结果。
 */
const QString &Session::displayName() const { return m_displayName; }

/**
 * @brief 实现 type 的核心逻辑。
 * @return 返回 Session::Type 结果。
 */
Session::Type Session::type() const { return m_type; }

/**
 * @brief 实现 conversationId 的核心逻辑。
 * @return 返回处理后的字符串结果。
 */
const QString &Session::conversationId() const { return m_conversationId; }

/**
 * @brief 实现 groupNumericId 的核心逻辑。
 * @return 返回处理后的字符串结果。
 */
const QString &Session::groupNumericId() const { return m_groupNumericId; }

/**
 * @brief 判断empty条件是否满足。
 * @return 返回条件判断结果，`true` 表示满足，`false` 表示不满足。
 */
bool Session::isValid() const { return !m_id.isEmpty() && !m_displayName.isEmpty(); }


