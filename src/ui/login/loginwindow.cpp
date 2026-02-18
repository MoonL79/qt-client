#include "loginwindow.h"
#include "ui_loginwindow.h"
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>

LoginWindow::LoginWindow(QWidget *parent)
    : QWidget(parent), ui(new Ui::LoginWindow), m_isDragging(false) {
  ui->setupUi(this);

  // 设置窗口标题和大小
  setWindowTitle("登录 - IM聊天软件");
  setFixedSize(400, 500);

  // 设置无边框窗口
  setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
  setAttribute(Qt::WA_TranslucentBackground);

  // 连接信号槽
  connect(ui->loginButton, &QPushButton::clicked, this,
          &LoginWindow::onLoginClicked);
  connect(ui->registerButton, &QPushButton::clicked, this,
          &LoginWindow::onRegisterClicked);
  connect(ui->closeButton, &QPushButton::clicked, this,
          &LoginWindow::onCloseClicked);

  // 设置密码框为密码模式
  ui->passwordEdit->setEchoMode(QLineEdit::Password);
}

LoginWindow::~LoginWindow() { delete ui; }

void LoginWindow::paintEvent(QPaintEvent *event) {
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing);

  // 绘制圆角矩形背景
  QPainterPath path;
  path.addRoundedRect(rect(), 20, 20);

  painter.fillPath(path, QColor("#f5f5f5"));

  // 绘制边框（可选）
  painter.setPen(QPen(QColor("#d9d9d9"), 1));
  painter.drawPath(path);
}

void LoginWindow::onLoginClicked() {
  QString username = ui->usernameEdit->text().trimmed();
  
  // 1. 取消用户名与密码的限制，点击登录即可进入主界面
  if (username.isEmpty()) {
      username = "User"; // 默认用户名
  }

  // 直接发射登录成功信号
  emit loginSuccess(username);
}

void LoginWindow::onRegisterClicked() {
  // TODO: 后续实现注册功能
  QMessageBox::information(this, "提示", "注册功能正在开发中...");
}

void LoginWindow::onCloseClicked() { close(); }

void LoginWindow::mousePressEvent(QMouseEvent *event) {
  if (event->button() == Qt::LeftButton) {
    m_isDragging = true;
    m_dragPosition =
        event->globalPosition().toPoint() - frameGeometry().topLeft();
    event->accept();
  }
}

void LoginWindow::mouseMoveEvent(QMouseEvent *event) {
  if (m_isDragging && (event->buttons() & Qt::LeftButton)) {
    move(event->globalPosition().toPoint() - m_dragPosition);
    event->accept();
  }
}

void LoginWindow::mouseReleaseEvent(QMouseEvent *event) {
  if (event->button() == Qt::LeftButton) {
    m_isDragging = false;
    event->accept();
  }
}
