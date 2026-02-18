#ifndef SESSIONWINDOW_H
#define SESSIONWINDOW_H

#include "networkclient.h"
#include <QByteArray>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPoint>
#include <QPushButton>
#include <QString>
#include <QTextCursor>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QWidget>

class SessionWindow : public QWidget {
  Q_OBJECT
public:
  explicit SessionWindow(const QString &sessionName, QWidget *parent = nullptr);

protected:
  bool eventFilter(QObject *obj, QEvent *event) override;
  void mousePressEvent(QMouseEvent *event) override;
  void mouseMoveEvent(QMouseEvent *event) override;
  void mouseReleaseEvent(QMouseEvent *event) override;

private:
  void initUI();
  QString m_sessionName;

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
  QTextEdit *m_receiveBox;
  QLineEdit *m_inputLine;
  QPushButton *m_sendBtn;
  NetworkClient *m_network;
  QString m_pendingMessage;
  void sendPendingMessage();

private slots:
  void onSendClicked();
  void onNetworkConnected();
  void onNetworkDataReceived(const QByteArray &data);
  void onNetworkErrorOccurred(const QString &error);
  void onSocketConnected();
  void onSocketReadyRead();
  void onSocketErrorOccurred(QAbstractSocket::SocketError error);
};

#endif // SESSIONWINDOW_H
