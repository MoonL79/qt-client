#ifndef WIDGET_H
#define WIDGET_H

#include "conversationlistmanager.h"
#include "friendlistmanager.h"
#include "profileapiclient.h"
#include "session.h"

#include <QNetworkAccessManager>
#include <QNetworkDiskCache>
#include <QNetworkReply>
#include <QHash>
#include <QListWidget>
#include <QPointer>
#include <QJsonObject>
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
class SessionWindow;

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
    struct ConversationListState {
        QString conversationId;
        QString conversationUuid;
        int conversationType = 0;
        QString displayName;
        QString avatarUrl;
        QString peerUserId;
        QString peerNumericId;
        QString peerUsername;
        QString peerNickname;
        QString peerAvatarUrl;
        QString peerBio;
        int peerStatus = 0;
        bool peerIsOnline = false;
        QString peerLastSeenAt;
        QString lastMessagePreview;
        int unreadCount = 0;
        bool placeholder = false;
    };

    void initUI();
    void initAvatarHttpClient();
    void addSessionItem(const Session &session);
    void requestConversationList(bool force = false);
    void requestFriendListForContacts(bool force = false);
    void refreshConversationListUi();
    void updateConversationListItem(
        const conversationlist::ConversationItem &conversationItem);
    void handleIncomingRealtimePayload(const QString &payload,
                                       const QString &sourceTag);
    void handleMessageEnvelope(const protocol::Envelope &envelope);
    void handlePresenceEnvelope(const QJsonObject &data);
    QUrl resolveAvatarUrl(const QString &avatarUrl) const;
    void requestAvatarImage(const QString &avatarUrl);
    void applyAvatarPixmap(const QPixmap &pixmap);
    void applyDefaultAvatar();
    void syncFriendListToDeleteDialog();
    QListWidgetItem *findSessionItemByConversationId(const QString &conversationId) const;
    QListWidgetItem *upsertConversationListItem(const ConversationListState &state,
                                                const conversationlist::ConversationItem *conversationItem);
    void applyConversationStateToItem(QListWidgetItem *item,
                                      const ConversationListState &state,
                                      const conversationlist::ConversationItem *conversationItem);
    void resetConversationUnread(const QString &conversationId);
    QString buildSessionItemText(const QString &displayName,
                                 const QString &numericId,
                                 bool isOnline,
                                 int userStatus,
                                 const QString &preview,
                                 int unreadCount) const;
    QString elidePreview(const QString &preview) const;

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
    conversationlist::ConversationListManager m_conversationListManager;
    friendlist::FriendListManager m_friendListManager;
    QString m_pendingConversationListRequestId;
    QString m_pendingFriendListRequestId;
    QTimer* m_conversationListRefreshTimer = nullptr;
    
    QListWidget* m_sessionList;
    QHash<QString, Session> m_sessionsById;
    QHash<QString, QPointer<SessionWindow>> m_sessionWindowsByUserId;
    QHash<QString, QPointer<SessionWindow>> m_sessionWindowsByNumericId;
    QHash<QString, QPointer<SessionWindow>> m_sessionWindowsByConversationId;
    QHash<QString, ConversationListState> m_conversationStatesByConversationId;
    
    // Dragging support
    bool m_isDragging;
    QPoint m_dragPosition;

private slots:
    void onSessionDoubleClicked(QListWidgetItem *item);
    void onOpenSettings();
    void onOpenAddFriend();
    void onOpenDeleteFriend();
    void onAvatarReplyFinished(QNetworkReply *reply);
    void onConversationListPayloadReceived(const QString &requestId,
                                           const QJsonObject &data);
    void onConversationListFailed(const QString &requestId, int code,
                                  const QString &message);
    void onFriendListPayloadReceived(const QString &requestId,
                                     const QJsonObject &data);
    void onFriendListFailed(const QString &requestId, int code,
                            const QString &message);
};
#endif // WIDGET_H
