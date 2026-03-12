#ifndef FRIENDLISTMANAGER_H
#define FRIENDLISTMANAGER_H

#include <QByteArray>
#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QString>

class QListWidget;

namespace friendlist {

struct FriendItem {
  QString userId;
  QString numericId;
  QString username;
  QString nickname;
  QString displayName;
  QString avatarUrl;
  QString bio;
  int status = 0;
  int userStatus = 0;
  bool isOnline = false;
  QString lastSeenAtUtc;
  QDateTime lastSeenAt;
};

class FriendListManager {
public:
  bool updateFromJson(const QByteArray &jsonBytes);
  bool updateFromResponse(const QJsonObject &data);
  const QList<FriendItem> &friends() const;
  void clear();

  static void refreshListWidget(QListWidget *listWidget,
                                const QList<FriendItem> &friends);

private:
  QList<FriendItem> m_friends;
};

} // namespace friendlist

#endif // FRIENDLISTMANAGER_H
