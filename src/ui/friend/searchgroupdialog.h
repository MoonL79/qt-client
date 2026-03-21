#ifndef SEARCHGROUPDIALOG_H
#define SEARCHGROUPDIALOG_H

#include "profileapiclient.h"

#include <QDialog>
#include <QIcon>

class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QPushButton;

class SearchGroupDialog : public QDialog {
  Q_OBJECT

public:
  explicit SearchGroupDialog(ProfileApiClient *profileApiClient,
                             QWidget *parent = nullptr);

signals:
  void groupJoined(const JoinGroupResult &result);

private slots:
  void onSearchClicked();
  void onGroupsListed(const QString &requestId,
                      const QVector<GroupSearchItem> &groups);
  void onJoinGroupSucceeded(const QString &requestId,
                            const JoinGroupResult &result);
  void onRequestFailedDetailed(const QString &requestId, const QString &action,
                               int code, const QString &error);
  void onItemActivated(QListWidgetItem *item);
  void onActionButtonClicked();

private:
  void buildUi();
  void renderGroups(const QVector<GroupSearchItem> &groups);
  void clearResults(const QString &statusText);
  void triggerGroupAction(int index);
  void setJoinLoading(bool loading, int index = -1);
  QIcon defaultGroupIcon() const;

  ProfileApiClient *m_profileApiClient = nullptr;
  QString m_pendingSearchRequestId;
  QString m_pendingJoinRequestId;
  int m_pendingJoinGroupIndex = -1;
  QVector<GroupSearchItem> m_groups;
  QLineEdit *m_keywordEdit = nullptr;
  QLabel *m_statusLabel = nullptr;
  QListWidget *m_resultListWidget = nullptr;
  QPushButton *m_searchButton = nullptr;
};

#endif // SEARCHGROUPDIALOG_H
