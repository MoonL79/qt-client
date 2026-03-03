#ifndef REGISTERWINDOW_H
#define REGISTERWINDOW_H

#include <QAbstractSocket>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPoint>
#include <QTimer>
#include <QWidget>

#include "registerutils.h"

QT_BEGIN_NAMESPACE
namespace Ui {
class RegisterWindow;
}
QT_END_NAMESPACE

class RegisterWindow : public QWidget {
  Q_OBJECT

public:
  explicit RegisterWindow(QWidget *parent = nullptr);
  ~RegisterWindow();

signals:
  void registerSuccess(const QString &username);

private slots:
  void onRegisterClicked();
  void onBackClicked();
  void onCloseClicked();
  void onWebSocketConnected();
  void onWebSocketTextMessage(const QString &message);
  void onWebSocketDisconnected();
  void onWebSocketError(QAbstractSocket::SocketError error,
                        const QString &message);
  void onRequestTimeout();

protected:
  void mousePressEvent(QMouseEvent *event) override;
  void mouseMoveEvent(QMouseEvent *event) override;
  void mouseReleaseEvent(QMouseEvent *event) override;
  void paintEvent(QPaintEvent *event) override;

private:
  void setLineEditError(QLineEdit *lineEdit, bool hasError);
  void clearFieldErrors();
  void setRegisterLoading(bool loading, const QString &text);
  void resetPendingState();
  void applyNormalizedInput(const auth::RegisterInput &normalized);
  void sendRegisterRequest();

  Ui::RegisterWindow *ui;
  QPoint m_dragPosition;
  bool m_isDragging = false;
  bool m_isRegisterPending = false;
  bool m_hasExpandedOnRegisterClick = false;
  auth::RegisterInput m_pendingInput;
  QString m_pendingRegisterRequestId;
  QTimer m_requestTimer;
};

#endif // REGISTERWINDOW_H
