#ifndef SESSIONWINDOW_H
#define SESSIONWINDOW_H

#include "framelesswindowbase.h"
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

  struct ChatMessage {
    QString localId;
    QString requestId;
    QString conversationId;
    QString messageId;
    qint64 seq = 0;
    QString content;
    QString sentAt;
    QString senderUserId;
    QString senderUsername;
    MessageStatus status = MessageStatus::Received;
    QLabel *bubbleLabel = nullptr;
  };

  explicit SessionWindow(const Session &session, QWidget *parent = nullptr);
  void setPeerIdentity(const QString &userId, const QString &numericId);
  void updatePeerPresence(bool isOnline, const QString &lastSeenAtUtc);

signals:
  void outgoingMessageSubmitted(const QString &conversationId,
                                const QString &previewText);

protected:
  void initUI();
  void appendStatusLine(const QString &message);
  QLabel *appendChatBubble(const QString &message, bool outgoing = false,
                           bool status = false);
  void refreshPresenceLabel();
  void handleIncomingPayload(const QString &payload, const QString &sourceTag);
  int appendMessage(const ChatMessage &message);
  void updateMessageBubble(int index);
  void handleMessageSendResponse(const protocol::Envelope &envelope);
  void handleIncomingMessagePush(const protocol::Envelope &envelope);
  void markPendingMessageFailed(int index, const QString &reason);
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
  QVector<ChatMessage> m_messages;
  QHash<QString, int> m_pendingMessageIndexesByRequestId;
  void onSendClicked();
  void sendPendingMessage();

  websocketclient *m_websocket;
};

#endif // SESSIONWINDOW_H
