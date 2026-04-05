#ifndef LOCALCHATSTORE_H
#define LOCALCHATSTORE_H

#include "chatmessage.h"

#include <QSqlDatabase>
#include <QString>
#include <QVector>

class LocalChatStore {
public:
  LocalChatStore();
  ~LocalChatStore();

  bool setCurrentUser(const QString &currentUserId, QString *error = nullptr);
  void clearCurrentUser();

  bool saveMessage(const ChatMessage &message, QString *error = nullptr);
  QVector<ChatMessage> loadMessages(const QString &conversationId, int limit = 100,
                                    QString *error = nullptr) const;

private:
  bool updateExistingMessage(const ChatMessage &message, QString *error);
  bool openDatabase(const QString &currentUserId, QString *error);
  bool ensureSchema(QString *error);
  QString databasePathForUser(const QString &currentUserId) const;
  QString dedupeKeyForMessage(const ChatMessage &message) const;
  bool isReady(QString *error) const;

private:
  QString m_connectionName;
  QString m_currentUserId;
  QSqlDatabase m_database;
};

#endif // LOCALCHATSTORE_H

