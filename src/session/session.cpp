#include "session.h"

#include <QUuid>

Session::Session(const QString &id, const QString &displayName, Type type,
                 const QString &conversationId,
                 const QString &groupNumericId)
    : m_id(id), m_displayName(displayName), m_type(type),
      m_conversationId(conversationId.trimmed()),
      m_groupNumericId(groupNumericId.trimmed()) {}

Session Session::create(const QString &displayName, Type type,
                        const QString &conversationId,
                        const QString &groupNumericId) {
  const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
  return Session(id, displayName, type, conversationId, groupNumericId);
}

const QString &Session::id() const { return m_id; }

const QString &Session::displayName() const { return m_displayName; }

Session::Type Session::type() const { return m_type; }

const QString &Session::conversationId() const { return m_conversationId; }

const QString &Session::groupNumericId() const { return m_groupNumericId; }

bool Session::isValid() const { return !m_id.isEmpty() && !m_displayName.isEmpty(); }
