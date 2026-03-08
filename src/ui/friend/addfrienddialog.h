#ifndef ADDFRIENDDIALOG_H
#define ADDFRIENDDIALOG_H

#include "profileapiclient.h"

#include <QDialog>
#include <QNetworkAccessManager>
#include <QPointer>

class QLabel;
class QLineEdit;
class QPushButton;
class QNetworkReply;

class AddFriendDialog : public QDialog {
  Q_OBJECT

public:
  explicit AddFriendDialog(const QString &currentUserId,
                           ProfileApiClient *profileApiClient,
                           QWidget *parent = nullptr);
  ~AddFriendDialog() override;

private slots:
  void onQueryClicked();
  void onAddFriendClicked();
  void onUserProfileQueried(const QString &requestId, const ProfileInfo &info);
  void onAddFriendSuccess(const QString &requestId, const QString &friendUserId);
  void onRequestFailedDetailed(const QString &requestId, const QString &action,
                               int code, const QString &error);
  void onAvatarReplyFinished();

private:
  void buildUi();
  void setQueryLoading(bool loading, const QString &text = QString());
  void setAddLoading(bool loading, const QString &text = QString());
  void clearQueryResult();
  void applyQueryResult(const ProfileInfo &info);
  bool isValidNumericId(const QString &numericId) const;
  QString resolveQueryErrorMessage(int code) const;
  void requestAvatar(const QString &avatarUrl);
  void applyDefaultAvatar();

  ProfileApiClient *m_profileApiClient = nullptr;
  QString m_currentUserId;
  QString m_pendingQueryRequestId;
  QString m_pendingAddRequestId;
  QString m_lastQueriedUserId;
  QString m_lastQueriedAvatarUrl;
  bool m_queryLoading = false;
  bool m_addLoading = false;
  ProfileInfo m_lastProfile;
  QNetworkAccessManager m_avatarNetworkManager;
  QPointer<QNetworkReply> m_avatarReply;

  QLineEdit *m_numericIdEdit = nullptr;
  QPushButton *m_queryButton = nullptr;
  QPushButton *m_addButton = nullptr;
  QLabel *m_statusLabel = nullptr;
  QLabel *m_avatarLabel = nullptr;
  QLabel *m_nicknameValue = nullptr;
  QLabel *m_numericIdValue = nullptr;
  QLabel *m_signatureValue = nullptr;
};

#endif // ADDFRIENDDIALOG_H
