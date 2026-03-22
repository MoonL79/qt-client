#ifndef LEAVEGROUPDIALOG_H
#define LEAVEGROUPDIALOG_H

#include "conversationlistmanager.h"
#include "profileapiclient.h"

#include <QDialog>

class QLabel;
class QListWidget;
class QListWidgetItem;

class LeaveGroupDialog : public QDialog {
  Q_OBJECT

public:
  explicit LeaveGroupDialog(
      const QList<conversationlist::ConversationItem> &conversations,
      ProfileApiClient *profileApiClient, QWidget *parent = nullptr);
  void setConversations(
      const QList<conversationlist::ConversationItem> &conversations);

signals:
  void groupLeft(const LeaveGroupResult &result);

private slots:
  void onItemDoubleClicked(QListWidgetItem *item);
  void onLeaveGroupFinished(const QString &requestId,
                            const LeaveGroupResult &result);
  void onRequestFailedDetailed(const QString &requestId, const QString &action,
                               int code, const QString &error);

private:
  void buildUi();
  void refreshList();
  QString resolveLeaveErrorMessage(int code, const QString &error) const;

  ProfileApiClient *m_profileApiClient = nullptr;
  QList<conversationlist::ConversationItem> m_conversations;
  QString m_pendingLeaveRequestId;

  QLabel *m_tipLabel = nullptr;
  QListWidget *m_groupListWidget = nullptr;
};

#endif // LEAVEGROUPDIALOG_H
