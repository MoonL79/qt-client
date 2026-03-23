#ifndef DISMISSGROUPDIALOG_H
#define DISMISSGROUPDIALOG_H

#include "conversationlistmanager.h"
#include "profileapiclient.h"

#include <QDialog>
#include <QHash>

class QLabel;
class QListWidget;
class QListWidgetItem;

class DismissGroupDialog : public QDialog {
  Q_OBJECT

public:
  explicit DismissGroupDialog(
      const QString &currentUserId,
      const QList<conversationlist::ConversationItem> &conversations,
      ProfileApiClient *profileApiClient, QWidget *parent = nullptr);
  void setConversations(
      const QList<conversationlist::ConversationItem> &conversations);

private slots:
  void onItemDoubleClicked(QListWidgetItem *item);
  void onGroupsListed(const QString &requestId,
                      const QVector<GroupSearchItem> &groups);
  void onDismissGroupFinished(const QString &requestId,
                              const DismissGroupResult &result);
  void onRequestFailedDetailed(const QString &requestId, const QString &action,
                               int code, const QString &error);

private:
  void buildUi();
  void requestOwnerInfoForGroups();
  void refreshList();
  QString resolveDismissErrorMessage(int code, const QString &error) const;

  ProfileApiClient *m_profileApiClient = nullptr;
  QString m_currentUserId;
  QList<conversationlist::ConversationItem> m_conversations;
  QString m_pendingDismissRequestId;
  QHash<QString, QString> m_ownerUserIdByGroupNumericId;
  QHash<QString, QString> m_ownerLookupRequestIdToGroupNumericId;

  QLabel *m_tipLabel = nullptr;
  QListWidget *m_groupListWidget = nullptr;
};

#endif // DISMISSGROUPDIALOG_H
