#include "session.h"

#include <QUuid>

// 实现 `trimmed` 的核心逻辑。
Session::Session(const QString &id, const QString &displayName, Type type,
                 const QString &conversationId,
                 const QString &groupNumericId)
    : m_id(id), m_displayName(displayName), m_type(type),
      m_conversationId(conversationId.trimmed()),
      m_groupNumericId(groupNumericId.trimmed()) {}

// 创建`create`对象或数据。
Session Session::create(const QString &displayName, Type type,
                        const QString &conversationId,
                        const QString &groupNumericId) {
  const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
  return Session(id, displayName, type, conversationId, groupNumericId);
}

// 实现 `id` 的核心逻辑。
const QString &Session::id() const { return m_id; }

// 实现 `displayName` 的核心逻辑。
const QString &Session::displayName() const { return m_displayName; }

// 实现 `type` 的核心逻辑。
Session::Type Session::type() const { return m_type; }

// 实现 `conversationId` 的核心逻辑。
const QString &Session::conversationId() const { return m_conversationId; }

// 实现 `groupNumericId` 的核心逻辑。
const QString &Session::groupNumericId() const { return m_groupNumericId; }

// 判断empty条件是否满足。
bool Session::isValid() const { return !m_id.isEmpty() && !m_displayName.isEmpty(); }
