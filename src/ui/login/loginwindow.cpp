#include "loginwindow.h"
#include "protocol.h"
#include "ui_loginwindow.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QUuid>
#include <QDebug>

namespace {
constexpr const char *kWebSocketUrl = "ws://192.168.14.133:12345";

QString extractLoginErrorMessage(const protocol::Envelope &envelope) {
  const QJsonObject &data = envelope.data;

  if (data.value("message").isString()) {
    return data.value("message").toString().trimmed();
  }
  if (data.value("error").isString()) {
    return data.value("error").toString().trimmed();
  }
  if (data.value("reason").isString()) {
    return data.value("reason").toString().trimmed();
  }
  if (data.value("detail").isString()) {
    return data.value("detail").toString().trimmed();
  }

  if (data.value("error").isObject()) {
    const QJsonObject errObj = data.value("error").toObject();
    if (errObj.value("message").isString()) {
      return errObj.value("message").toString().trimmed();
    }
    if (errObj.value("detail").isString()) {
      return errObj.value("detail").toString().trimmed();
    }
    return QString::fromUtf8(
               QJsonDocument(errObj).toJson(QJsonDocument::Compact))
        .trimmed();
  }

  if (envelope.hasCode && envelope.code != 0) {
    return QStringLiteral("登录失败，错误码: %1").arg(envelope.code);
  }

  if (data.value("code").isString()) {
    return QStringLiteral("登录失败，错误码: %1").arg(data.value("code").toString());
  }
  if (data.value("code").isDouble()) {
    return QStringLiteral("登录失败，错误码: %1").arg(data.value("code").toInt());
  }

  return QStringLiteral("登录失败，未返回错误详情");
}

bool isCurrentLoginResponse(const protocol::Envelope &envelope,
                            const QString &pendingRequestId) {
  if (pendingRequestId.isEmpty()) {
    return false;
  }

  // Normal case: server echoes current request_id.
  if (envelope.requestId == pendingRequestId) {
    return true;
  }

  // Some error packets may not carry request_id and only include received_payload.
  if (!envelope.requestId.isEmpty()) {
    return false;
  }

  const QJsonValue receivedPayload = envelope.data.value("received_payload");
  if (!receivedPayload.isString()) {
    return false;
  }

  protocol::Envelope originalRequest;
  if (!protocol::parseEnvelope(receivedPayload.toString(), &originalRequest)) {
    return false;
  }

  return originalRequest.requestId == pendingRequestId &&
         originalRequest.type == "AUTH" && originalRequest.action == "LOGIN";
}

bool isLoginSuccess(const protocol::Envelope &envelope) {
  if (envelope.hasCode) {
    return envelope.code == 0;
  }
  // Backward compatibility for old server responses.
  return envelope.data.value("ok").toBool(false);
}
}

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

  auto ws = websocketclient::instance();
  connect(ws, &websocketclient::connected, this,
          &LoginWindow::onWebSocketConnected);
  connect(ws, &websocketclient::textMessageReceived, this,
          &LoginWindow::onWebSocketTextMessage);
  connect(ws, &websocketclient::errorOccurred, this,
          &LoginWindow::onWebSocketError);

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
  const QString password = ui->passwordEdit->text();

  if (username.isEmpty()) {
    QMessageBox::warning(this, "输入错误", "用户名不能为空");
    ui->usernameEdit->setFocus();
    return;
  }

  if (password.isEmpty()) {
    QMessageBox::warning(this, "输入错误", "密码不能为空");
    ui->passwordEdit->setFocus();
    return;
  }

  m_pendingUsername = username;
  m_pendingPassword = password;
  m_pendingLoginRequestId.clear();
  m_isLoginPending = true;
  ui->loginButton->setEnabled(false);
  ui->loginButton->setText("连接中...");

  auto ws = websocketclient::instance();
  qInfo() << "Start login request for user:" << m_pendingUsername;
  if (!ws->isConnected()) {
    ws->open(QUrl(QString::fromLatin1(kWebSocketUrl)));
  } else {
    onWebSocketConnected();
  }
}

void LoginWindow::onRegisterClicked() {
  // TODO: 后续实现注册功能
  QMessageBox::information(this, "提示", "注册功能正在开发中...");
}

void LoginWindow::onCloseClicked() { close(); }

void LoginWindow::onWebSocketConnected() {
  if (!m_isLoginPending)
    return;

  QJsonObject data;
  data.insert("username", m_pendingUsername);
  data.insert("password", m_pendingPassword);
  m_pendingLoginRequestId =
      QUuid::createUuid().toString(QUuid::WithoutBraces);
  const QString payload =
      protocol::createRequest("AUTH", "LOGIN", data, m_pendingLoginRequestId);
  websocketclient::instance()->sendTextMessage(payload);
  qInfo() << "AUTH LOGIN sent, request_id:" << m_pendingLoginRequestId;
  ui->loginButton->setText("登录中...");
}

void LoginWindow::onWebSocketTextMessage(const QString &message) {
  if (!m_isLoginPending || m_pendingLoginRequestId.isEmpty())
    return;

  protocol::Envelope envelope;
  QString parseError;
  if (!protocol::parseEnvelope(message, &envelope, &parseError)) {
    m_isLoginPending = false;
    m_pendingLoginRequestId.clear();
    m_pendingPassword.clear();
    ui->loginButton->setEnabled(true);
    ui->loginButton->setText("登录");
    QMessageBox::warning(this, "登录失败",
                         QStringLiteral("响应解析失败: %1")
                             .arg(parseError.isEmpty()
                                      ? QStringLiteral("未知协议错误")
                                      : parseError));
    return;
  }

  if (!isCurrentLoginResponse(envelope, m_pendingLoginRequestId))
    return;

  const QString responseMessage = extractLoginErrorMessage(envelope);
  if (envelope.type != "AUTH" || envelope.action != "LOGIN") {
    m_isLoginPending = false;
    m_pendingLoginRequestId.clear();
    m_pendingPassword.clear();
    ui->loginButton->setEnabled(true);
    ui->loginButton->setText("登录");
    QMessageBox::warning(this, "登录失败", responseMessage);
    return;
  }

  m_isLoginPending = false;
  m_pendingLoginRequestId.clear();
  ui->loginButton->setEnabled(true);
  ui->loginButton->setText("登录");

  if (isLoginSuccess(envelope)) {
    qInfo() << "Login success for user:" << m_pendingUsername;
    m_pendingPassword.clear();
    emit loginSuccess(m_pendingUsername);
    return;
  }

  qWarning() << "Login failed, reason:" << responseMessage;
  m_pendingPassword.clear();
  QMessageBox::warning(this, "登录失败", responseMessage);
}

void LoginWindow::onWebSocketError(QAbstractSocket::SocketError,
                                   const QString &message) {
  qWarning() << "WebSocket error during login:" << message;
  m_isLoginPending = false;
  m_pendingLoginRequestId.clear();
  m_pendingPassword.clear();
  ui->loginButton->setEnabled(true);
  ui->loginButton->setText("登录");
  QMessageBox::warning(this, "连接失败", message);
}

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
