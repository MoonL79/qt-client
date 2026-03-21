#ifndef SESSIONWINDOW_H
#define SESSIONWINDOW_H

#include "protocol.h"
#include "session.h"
#include "usersession.h"
#include "websocketclient.h"
#include <QAbstractSocket>
#include <QByteArray>
#include <QHash>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPoint>
#include <QPushButton>
#include <QScrollArea>
#include <QString>
#include <QVector>
#include <QVBoxLayout>
#include <QWidget>

class SessionWindow : public QWidget {
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
  bool eventFilter(QObject *obj, QEvent *event) override;
  void mousePressEvent(QMouseEvent *event) override;
  void mouseMoveEvent(QMouseEvent *event) override;
  void mouseReleaseEvent(QMouseEvent *event) override;

private:
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

  // Dragging support
  bool m_isDragging;
  QPoint m_dragPosition;

  // Resize support
  enum Direction {
    None,
    Top,
    Bottom,
    Left,
    Right,
    TopLeft,
    TopRight,
    BottomLeft,
    BottomRight
  };
  Direction m_resizeDir;
  void checkCursorShape(const QPoint &globalPos);

  // Unified mouse handling
  void handleMousePress(QMouseEvent *event);
  void handleMouseMove(QMouseEvent *event);
  void handleMouseRelease(QMouseEvent *event);

  // Network & UI helpers
  QScrollArea *m_chatScroll;
  QWidget *m_chatContainer;
  QVBoxLayout *m_chatLayout;
  QLineEdit *m_inputLine;
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
