#ifndef SESSIONWINDOW_H
#define SESSIONWINDOW_H

#include "session.h"
#include "websocketclient.h"
#include <QAbstractSocket>
#include <QByteArray>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPoint>
#include <QPushButton>
#include <QScrollArea>
#include <QString>
#include <QVBoxLayout>
#include <QWidget>

class SessionWindow : public QWidget {
  Q_OBJECT
public:
  explicit SessionWindow(const Session &session, QWidget *parent = nullptr);
  void setFriendIdentity(const QString &userId, const QString &numericId);
  void updateFriendPresence(bool isOnline, const QString &lastSeenAtUtc);

protected:
  bool eventFilter(QObject *obj, QEvent *event) override;
  void mousePressEvent(QMouseEvent *event) override;
  void mouseMoveEvent(QMouseEvent *event) override;
  void mouseReleaseEvent(QMouseEvent *event) override;

private:
  void initUI();
  void appendStatusLine(const QString &message);
  void appendChatBubble(const QString &message, bool outgoing = false,
                        bool status = false);
  void updateConnectionStatus(QAbstractSocket::SocketState state);
  void refreshPresenceLabel();
  void handleIncomingPayload(const QString &payload, const QString &sourceTag);
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
  QLabel *m_statusLabel;
  QLabel *m_presenceLabel;
  QPushButton *m_testBtn;
  QString m_pendingMessage;
  QString m_friendUserId;
  QString m_friendNumericId;
  QString m_friendLastSeenAtUtc;
  bool m_friendIsOnline = false;
  void onSendClicked();
  void sendPendingMessage();
  void onTestConnection();

  websocketclient *m_websocket;
};

#endif // SESSIONWINDOW_H
