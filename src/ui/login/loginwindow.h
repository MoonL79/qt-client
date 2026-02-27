#ifndef LOGINWINDOW_H
#define LOGINWINDOW_H

#include <QMouseEvent>
#include <QPaintEvent>
#include <QPoint>
#include <QWidget>

#include "..\\..\\network\\websocketclient.h"

QT_BEGIN_NAMESPACE
namespace Ui {
class LoginWindow;
}
QT_END_NAMESPACE

class LoginWindow : public QWidget {
  Q_OBJECT

public:
  explicit LoginWindow(QWidget *parent = nullptr);
  ~LoginWindow();

signals:
  void loginSuccess(const QString &username);

 private slots:
  void onLoginClicked();
  void onRegisterClicked();
  void onCloseClicked();
  void onWebSocketConnected();
  void onWebSocketError(QAbstractSocket::SocketError error,
                        const QString &message);

protected:
  void mousePressEvent(QMouseEvent *event) override;
  void mouseMoveEvent(QMouseEvent *event) override;
  void mouseReleaseEvent(QMouseEvent *event) override;
  void paintEvent(QPaintEvent *event) override;

 private:
  Ui::LoginWindow *ui;
  QPoint m_dragPosition;
  bool m_isDragging;
  QString m_pendingUsername;
};

#endif // LOGINWINDOW_H
