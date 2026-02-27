#include "session.h"

#include <QUuid>

Session::Session(const QString &id, const QString &displayName, Type type)
    : m_id(id), m_displayName(displayName), m_type(type) {}

Session Session::create(const QString &displayName, Type type) {
  const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
  return Session(id, displayName, type);
}

const QString &Session::id() const { return m_id; }

const QString &Session::displayName() const { return m_displayName; }

Session::Type Session::type() const { return m_type; }

bool Session::isValid() const { return !m_id.isEmpty() && !m_displayName.isEmpty(); }
