#ifndef WIDGET_H
#define WIDGET_H

#include "friendlistmanager.h"
#include "profileapiclient.h"
#include "session.h"

#include <QNetworkAccessManager>
#include <QNetworkDiskCache>
#include <QNetworkReply>
#include <QHash>
#include <QListWidget>
#include <QPointer>
#include <QPoint>
#include <QMouseEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QUrl>
#include <QWidget>
#include <QVBoxLayout>

QT_BEGIN_NAMESPACE
namespace Ui {
class Widget;
}
QT_END_NAMESPACE
class QPixmap;
class SettingsWindow;
class AddFriendDialog;
class DeleteFriendDialog;

class Widget : public QWidget
{
    Q_OBJECT

public:
    Widget(QWidget *parent = nullptr);
    ~Widget();
    
    // 设置当前登录用户信息的接口
    void setUserInfo(const QString& username,
                     const QString& avatarPath = ":/resources/avatar.png",
                     const QString& signature = QString());
    void setCurrentUserId(const QString& userId);
    void setCurrentUserNumericId(const QString& numericId);
    void setProfileApiClient(ProfileApiClient* profileApiClient);

signals:
    void logoutRequested();

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    void initUI();
    void initAvatarHttpClient();
    void addSessionItem(const Session &session);
    void requestFriendList(bool force = false);
    void refreshFriendListUi();
    QUrl resolveAvatarUrl(const QString &avatarUrl) const;
    void requestAvatarImage(const QString &avatarUrl);
    void applyAvatarPixmap(const QPixmap &pixmap);
    void applyDefaultAvatar();
    void syncFriendListToDeleteDialog();

    Ui::Widget *ui;
    
    // UI Components
    QWidget* m_topPanel;
    QLabel* m_avatarLabel;
    QLabel* m_nameLabel;
    QLabel* m_signatureLabel;
    QNetworkAccessManager* m_avatarNetworkManager = nullptr;
    QNetworkDiskCache* m_avatarDiskCache = nullptr;
    QString m_currentUserId;
    QString m_currentUserNumericId;
    QString m_currentDisplayName;
    QString m_currentSignature;
    QString m_currentAvatarUrl;
    ProfileApiClient* m_profileApiClient = nullptr;
    QPointer<SettingsWindow> m_settingsWindow;
    QPointer<AddFriendDialog> m_addFriendDialog;
    QPointer<DeleteFriendDialog> m_deleteFriendDialog;
    QPushButton* m_addFriendBtn = nullptr;
    friendlist::FriendListManager m_friendListManager;
    QString m_pendingFriendListRequestId;
    QTimer* m_friendListRefreshTimer = nullptr;
    
    QListWidget* m_sessionList;
    QHash<QString, Session> m_sessionsById;
    
    // Dragging support
    bool m_isDragging;
    QPoint m_dragPosition;

private slots:
    void onSessionDoubleClicked(QListWidgetItem *item);
    void onOpenSettings();
    void onOpenAddFriend();
    void onOpenDeleteFriend();
    void onAvatarReplyFinished(QNetworkReply *reply);
    void onFriendListPayloadReceived(const QString &requestId,
                                     const QJsonObject &data);
    void onFriendListFailed(const QString &requestId, int code,
                            const QString &message);
};
#endif // WIDGET_H
