#ifndef SESSION_H
#define SESSION_H

#include <QString>

class Session {
public:
  enum class Type { Direct, Group };

  Session() = default;
  Session(const QString &id, const QString &displayName, Type type);

  static Session create(const QString &displayName, Type type);

  const QString &id() const;
  const QString &displayName() const;
  Type type() const;

  bool isValid() const;

private:
  QString m_id;
  QString m_displayName;
  Type m_type = Type::Direct;
};

#endif // SESSION_H
