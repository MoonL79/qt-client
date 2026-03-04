#ifndef SETTINGSWINDOW_H
#define SETTINGSWINDOW_H

#include "profileapiclient.h"

#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTextEdit>
#include <QUrl>
#include <QWidget>

class SettingsWindow : public QWidget {
  Q_OBJECT

public:
  explicit SettingsWindow(const QString &userId, ProfileApiClient *profileApiClient,
                          QWidget *parent = nullptr);
  ~SettingsWindow() override;

signals:
  void profileApplied(const QString &displayName, const QString &avatarUrl);

private slots:
  void onRefreshClicked();
  void onSaveClicked();
  void onChooseAvatarClicked();
  void onUploadAvatarClicked();
  void onProfileInfoReceived(const QString &requestId, const ProfileInfo &info);
  void onProfileSetSuccess(const QString &requestId, const ProfileInfo &info);
  void onProfileRequestFailed(const QString &requestId, const QString &action,
                              const QString &error);
  void onUploadReplyFinished();
  void onAvatarPreviewReplyFinished();

private:
  void buildUi();
  void applyProfileToUi(const ProfileInfo &info);
  bool validateInput(QString *error) const;
  bool validateProfileTextInput(QString *error) const;
  bool validateSelectedAvatarFile(QString *error) const;
  bool hasValidUserId() const;
  void updateActionButtons();
  void setLoading(bool loading, const QString &statusText);
  void setSaving(bool saving, const QString &statusText);
  void setUploading(bool uploading, const QString &statusText);
  QUrl buildUploadEndpoint() const;
  QUrl resolveAvatarUrlForPreview(const QString &avatarUrl) const;
  void updateAvatarPreviewFromLocal(const QString &filePath);
  void updateAvatarPreviewFromUrl(const QString &avatarUrl);
  void applyDefaultAvatarPreview();
  QString extractMessageFromJson(const QJsonObject &obj) const;

  ProfileApiClient *m_profileApiClient = nullptr;
  QString m_userId;
  QString m_pendingGetRequestId;
  QString m_pendingSetRequestId;
  QString m_pendingUploadRequestId;
  QString m_avatarUrl;
  QString m_selectedAvatarFilePath;
  bool m_loading = false;
  bool m_saving = false;
  bool m_uploading = false;
  QNetworkAccessManager m_uploadNetworkManager;
  QPointer<QNetworkReply> m_uploadReply;
  QPointer<QNetworkReply> m_avatarPreviewReply;

  QLineEdit *m_nicknameEdit = nullptr;
  QTextEdit *m_signatureEdit = nullptr;
  QLabel *m_avatarPreviewLabel = nullptr;
  QPushButton *m_chooseAvatarButton = nullptr;
  QPushButton *m_uploadAvatarButton = nullptr;
  QPushButton *m_refreshButton = nullptr;
  QPushButton *m_saveButton = nullptr;
  QLabel *m_statusLabel = nullptr;
};

#endif // SETTINGSWINDOW_H
