#ifndef CREATEGROUPDIALOG_H
#define CREATEGROUPDIALOG_H

#include "friendlistmanager.h"
#include "profileapiclient.h"

#include <QDialog>

class QLabel;
class QLineEdit;
class QListWidget;

class CreateGroupDialog : public QDialog {
  Q_OBJECT

public:
  explicit CreateGroupDialog(const QList<friendlist::FriendItem> &friends,
                             ProfileApiClient *profileApiClient,
                             QWidget *parent = nullptr);

  void setFriends(const QList<friendlist::FriendItem> &friends);

signals:
  void groupCreated(const CreateGroupResult &result);

private slots:
  void onCreateClicked();
  void onCreateGroupSucceeded(const QString &requestId,
                              const CreateGroupResult &result);
  void onRequestFailedDetailed(const QString &requestId, const QString &action,
                               int code, const QString &error);

private:
  void buildUi();
  void refreshFriendList();
  bool isValidNumericId(const QString &numericId) const;
  QStringList selectedMemberNumericIds() const;

  ProfileApiClient *m_profileApiClient = nullptr;
  QList<friendlist::FriendItem> m_friends;
  QString m_pendingCreateRequestId;

  QLineEdit *m_groupNameEdit = nullptr;
  QListWidget *m_friendListWidget = nullptr;
  QLabel *m_statusLabel = nullptr;
};

#endif // CREATEGROUPDIALOG_H
