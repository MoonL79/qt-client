#ifndef DELETEFRIENDDIALOG_H
#define DELETEFRIENDDIALOG_H

#include "friendlistmanager.h"
#include "profileapiclient.h"

#include <QDialog>

class QLabel;
class QListWidget;
class QListWidgetItem;

class DeleteFriendDialog : public QDialog {
  Q_OBJECT

public:
  explicit DeleteFriendDialog(
      const QString &currentUserNumericId,
      const QList<friendlist::FriendItem> &friends,
      ProfileApiClient *profileApiClient, QWidget *parent = nullptr);
  void setFriends(const QList<friendlist::FriendItem> &friends);

signals:
  void friendDeleted(const DeleteFriendResult &result);

private slots:
  void onItemDoubleClicked(QListWidgetItem *item);
  void onDeleteFriendFinished(const QString &requestId,
                              const DeleteFriendResult &result);
  void onRequestFailedDetailed(const QString &requestId, const QString &action,
                               int code, const QString &error);

private:
  void buildUi();
  void refreshList();
  bool isValidNumericId(const QString &numericId) const;
  QString resolveDeleteErrorMessage(int code, const QString &error) const;

  ProfileApiClient *m_profileApiClient = nullptr;
  QString m_currentUserNumericId;
  QList<friendlist::FriendItem> m_friends;
  QString m_pendingDeleteRequestId;

  QLabel *m_tipLabel = nullptr;
  QListWidget *m_friendListWidget = nullptr;
};

#endif // DELETEFRIENDDIALOG_H
