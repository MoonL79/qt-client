#ifndef WIDGET_H
#define WIDGET_H

#include "conversationlistmanager.h"
#include "friendlistmanager.h"
#include "profileapiclient.h"
#include "session.h"
#include "chatmessage.h"

#include <QNetworkAccessManager>
#include <QNetworkDiskCache>
#include <QNetworkReply>
#include <QHash>
#include <QList>
#include <QListWidget>
#include <QPointer>
#include <QJsonObject>
#include <QColor>
#include <QSet>
#include <QPoint>
#include <QMouseEvent>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPushButton>
#include <QTabBar>
#include <QTabWidget>
#include <QToolButton>
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
class QMenu;
class SettingsWindow;
class AddFriendDialog;
class DeleteFriendDialog;
class CreateGroupDialog;
class SearchGroupDialog;
class LeaveGroupDialog;
class DismissGroupDialog;
class SessionWindow;
class ChatFileService;
class LocalChatStore;
class MessageSyncClient;

class Widget : public QWidget
{
    Q_OBJECT

public:
    Widget(QWidget *parent = nullptr);
    ~Widget();
    
    /**
     * @brief 设置当前登录用户信息的接口
     * @param username 用户名。
     * @param avatarPath 头像路径。
     * @param signature 个性签名内容。
     * @return 无返回值。
     */
    void setUserInfo(const QString& username,
                     const QString& avatarPath = ":/resources/avatar.png",
                     const QString& signature = QString());
    void setCurrentUserId(const QString& userId);
    void setCurrentUserNumericId(const QString& numericId);
    void setProfileApiClient(ProfileApiClient* profileApiClient);
    void setThemeColor(const QString &colorHex);

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
        QString groupNumericId;
        int conversationType = 0;
        QString displayName;
        QString avatarUrl;
        int memberCount = 0;
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

    struct PendingFileTransferState {
        QString uploadRequestId;
        QString sendRequestId;
        QString conversationId;
        QString localFilePath;

        void clear() {
            uploadRequestId.clear();
            sendRequestId.clear();
            conversationId.clear();
            localFilePath.clear();
        }
    };

    struct PendingFileDownloadState {
        QString conversationId;
        QString originalName;
        QString savePath;
    };

    struct ConversationSyncTask {
        QString conversationId;
        qint64 serverLastSeq = 0;
        bool onDemand = false;
    };

    struct ActiveConversationSyncState {
        QString conversationId;
        qint64 localLastSeq = 0;
        qint64 serverLastSeq = 0;
        qint64 nextAfterSeq = 0;
        qint64 ackUpToSeq = 0;
        bool hasMore = false;
        bool onDemand = false;

        void clear() {
            conversationId.clear();
            localLastSeq = 0;
            serverLastSeq = 0;
            nextAfterSeq = 0;
            ackUpToSeq = 0;
            hasMore = false;
            onDemand = false;
        }

        bool isActive() const { return !conversationId.trimmed().isEmpty(); }
    };

    void initUI();
    void initAvatarHttpClient();
    void addSessionItem(const Session &session);
    void requestConversationList(bool force = false, bool silent = false);
    void requestFriendListForContacts(bool force = false, bool silent = false);
    void refreshConversationListUi();
    void refreshGroupListUi();
    void refreshContactListUi();
    void handleLeaveGroupResult(const LeaveGroupResult &result);
    void handleDismissGroupResult(const DismissGroupResult &result);
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
    QIcon conversationIcon(int conversationType) const;
    QListWidget *listWidgetForConversationType(int conversationType) const;
    QListWidgetItem *findConversationItemInList(QListWidget *listWidget,
                                                const QString &conversationId) const;
    QListWidgetItem *upsertConversationListItemToList(
        QListWidget *targetList, const ConversationListState &state,
        const conversationlist::ConversationItem *conversationItem);
    QListWidgetItem *findConversationItemByConversationId(const QString &conversationId) const;
    QListWidgetItem *upsertConversationListItem(const ConversationListState &state,
                                                const conversationlist::ConversationItem *conversationItem);
    void applyConversationStateToItem(QListWidgetItem *item,
                                      const ConversationListState &state,
                                      const conversationlist::ConversationItem *conversationItem);
    void resetConversationUnread(const QString &conversationId);
    QString buildSessionItemText(int conversationType,
                                 const QString &displayName,
                                 const QString &groupNumericId,
                                 const QString &numericId,
                                 bool isOnline,
                                 int userStatus,
                                 const QString &preview,
                                 int memberCount,
                                 bool preferGroupMeta,
                                 int unreadCount) const;
    QString elidePreview(const QString &preview) const;
    void applyTopPanelThemeColor(const QColor &color);
    void applyMainThemeColor(const QColor &color);
    bool ensureAttachmentTransferReady(const QString &attachmentLabel);
    void startAttachmentTransferForConversation(
        const QString &conversationId, const QString &conversationName,
        const QString &dialogTitle, const QString &fileFilter,
        const QString &attachmentLabel);
    void startFileDownloadForMessage(const ChatMessage &message,
                                     bool chooseSavePath);
    void scheduleInitialConversationSync();
    void beginInitialConversationSyncIfNeeded();
    void enqueueConversationSyncTask(const QString &conversationId,
                                     qint64 serverLastSeq, bool onDemand,
                                     bool prioritize = false);
    void startNextConversationSyncTask();
    void startConversationPull(const QString &conversationId, qint64 afterSeq,
                               qint64 serverLastSeq, bool onDemand);
    void continueActiveConversationSync();
    void finalizeActiveConversationSync();
    void requestConversationIncrementalSync(const QString &conversationId);
    qint64 serverLastSeqForConversation(const QString &conversationId) const;
    bool storeAndRouteMessage(const ChatMessage &message, bool incrementUnread);
    void updateConversationStateFromMessage(const ChatMessage &message,
                                            bool incrementUnread);
    QString previewTextForMessage(const ChatMessage &message) const;

    Ui::Widget *ui;
    
    // UI Components
    QWidget* m_topPanel;
    QLabel* m_avatarLabel;
    QLabel* m_nameLabel;
    QLabel* m_signatureLabel;
    QPushButton* m_settingsButton = nullptr;
    QPushButton* m_minButton = nullptr;
    QPushButton* m_closeButton = nullptr;
    QToolButton* m_quickActionButton = nullptr;
    QMenu* m_quickActionMenu = nullptr;
    QNetworkAccessManager* m_avatarNetworkManager = nullptr;
    QNetworkDiskCache* m_avatarDiskCache = nullptr;
    QString m_currentUserId;
    QString m_currentUserNumericId;
    QString m_currentDisplayName;
    QString m_currentSignature;
    QString m_currentAvatarUrl;
    QColor m_topPanelThemeColor;
    ProfileApiClient* m_profileApiClient = nullptr;
    QPointer<SettingsWindow> m_settingsWindow;
    QPointer<AddFriendDialog> m_addFriendDialog;
    QPointer<DeleteFriendDialog> m_deleteFriendDialog;
    QPointer<CreateGroupDialog> m_createGroupDialog;
    QPointer<SearchGroupDialog> m_searchGroupDialog;
    QPointer<LeaveGroupDialog> m_leaveGroupDialog;
    QPointer<DismissGroupDialog> m_dismissGroupDialog;
    conversationlist::ConversationListManager m_conversationListManager;
    friendlist::FriendListManager m_friendListManager;
    QString m_pendingConversationListRequestId;
    QString m_pendingFriendListRequestId;
    QSet<QString> m_silentConversationListRequestIds;
    QSet<QString> m_silentFriendListRequestIds;
    QString m_pendingOpenConversationId;
    QTimer* m_conversationListRefreshTimer = nullptr;
    QTabWidget* m_tabWidget = nullptr;
    QListWidget* m_sessionList = nullptr;
    QListWidget* m_groupList = nullptr;
    QListWidget* m_contactList = nullptr;
    QHash<QString, Session> m_sessionsById;
    QHash<QString, QPointer<SessionWindow>> m_sessionWindowsByUserId;
    QHash<QString, QPointer<SessionWindow>> m_sessionWindowsByNumericId;
    QHash<QString, QPointer<SessionWindow>> m_sessionWindowsByConversationId;
    QHash<QString, ConversationListState> m_conversationStatesByConversationId;
    ChatFileService* m_chatFileService = nullptr;
    MessageSyncClient* m_messageSyncClient = nullptr;
    LocalChatStore* m_localChatStore = nullptr;
    PendingFileTransferState m_pendingFileTransfer;
    QHash<QString, PendingFileDownloadState> m_pendingFileDownloads;
    QList<ConversationSyncTask> m_conversationSyncQueue;
    QSet<QString> m_queuedConversationSyncIds;
    ActiveConversationSyncState m_activeConversationSync;
    QString m_pendingMessagePullRequestId;
    QString m_pendingMessageAckRequestId;
    bool m_pendingInitialConversationSync = false;
    
    /**
     * @brief Dragging support
     * @param item 数据项对象。
     * @return 返回布尔结果。
     */
    bool m_isDragging;
    QPoint m_dragPosition;

private slots:
    void onSessionDoubleClicked(QListWidgetItem *item);
    void onOpenSettings();
    void onOpenAddFriend();
    void onOpenDeleteFriend();
    void onOpenCreateGroup();
    void onOpenSearchGroup();
    void onOpenLeaveGroup();
    void onOpenDismissGroup();
    void onOpenFileTransfer();
    void onAvatarReplyFinished(QNetworkReply *reply);
    void onConversationListPayloadReceived(const QString &requestId,
                                           const QJsonObject &data);
    void onConversationListFailed(const QString &requestId, int code,
                                  const QString &message);
    void onFriendListPayloadReceived(const QString &requestId,
                                     const QJsonObject &data);
    void onFriendListFailed(const QString &requestId, int code,
                            const QString &message);
    void onProfileServerRequestReceived(const QString &requestId,
                                        const QString &action,
                                        const QJsonObject &data);
    void onLeaveGroupFinished(const QString &requestId,
                              const LeaveGroupResult &result);
    void onDismissGroupFinished(const QString &requestId,
                                const DismissGroupResult &result);
};
#endif // WIDGET_H


