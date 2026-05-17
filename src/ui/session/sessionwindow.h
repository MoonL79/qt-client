#ifndef SESSIONWINDOW_H
#define SESSIONWINDOW_H

#include "framelesswindowbase.h"
#include "chatmessage.h"
#include "protocol.h"
#include "session.h"
#include "usersession.h"
#include "websocketclient.h"
#include <QAbstractSocket>
#include <QByteArray>
#include <QHash>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QString>
#include <QTextEdit>
#include <QVector>
#include <QVBoxLayout>

class SessionWindow : public FramelessWindowBase {
  Q_OBJECT
public:
  enum class MessageStatus { Pending, Sent, Failed, Received };

  struct DisplayMessage {
    ChatMessage message;
    MessageStatus status = MessageStatus::Received;
    QLabel *bubbleLabel = nullptr;
  };

  explicit SessionWindow(const Session &session, QWidget *parent = nullptr);
  void setPeerIdentity(const QString &userId, const QString &numericId);
  void updatePeerPresence(bool isOnline, const QString &lastSeenAtUtc);
  void loadHistory(const QVector<ChatMessage> &messages);
  void appendPersistedMessage(const ChatMessage &message);

signals:
  void outgoingMessageSubmitted(const QString &conversationId,
                                const QString &previewText);
  void messageReadyForPersistence(const ChatMessage &message);
  void imageAttachmentRequested(const QString &conversationId,
                                const QString &conversationName);
  void fileAttachmentRequested(const QString &conversationId,
                               const QString &conversationName);

protected:
  void initUI();
  void appendStatusLine(const QString &message);
  QLabel *appendChatBubble(const QString &message, bool outgoing = false,
                           bool status = false);
  void refreshPresenceLabel();
  void handleIncomingPayload(const QString &payload, const QString &sourceTag);
  int appendMessage(const ChatMessage &message, MessageStatus status);
  void updateMessageBubble(int index);
  int findExistingMessageIndex(const ChatMessage &message) const;
  void handleMessageSendResponse(const protocol::Envelope &envelope);
  void handleIncomingMessagePush(const protocol::Envelope &envelope);
  void markPendingMessageFailed(int index, const QString &reason);
  bool isOutgoingMessage(const ChatMessage &message) const;
  QString renderMessageBody(const ChatMessage &message) const;
  Session m_session;

  // Network & UI helpers
  QScrollArea *m_chatScroll;
  QWidget *m_chatContainer;
  QVBoxLayout *m_chatLayout;
  QTextEdit *m_inputLine;
  QPushButton *m_sendBtn;
  QLabel *m_presenceLabel;
  QString m_pendingMessage;
  QString m_peerUserId;
  QString m_peerNumericId;
  QString m_peerLastSeenAtUtc;
  bool m_peerIsOnline = false;
  QVector<DisplayMessage> m_messages;
  QHash<QString, int> m_pendingMessageIndexesByRequestId;
  void onSendClicked();
  void sendPendingMessage();
  void requestAttachment(bool imageOnly);

  websocketclient *m_websocket;
};

#endif // SESSIONWINDOW_H
