#include "loginwindow.h"
#include "protocol.h"
#include "registerwindow.h"
#include "usersession.h"
#include "ui_loginwindow.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QRegularExpression>
#include <QUuid>
#include <QDebug>
#include <QtGlobal>

namespace {
constexpr const char *kWebSocketUrlEnv = "QT_SERVER_WS_URL";
constexpr const char *kWebSocketHostEnv = "QT_SERVER_WS_HOST";
constexpr const char *kWebSocketPortEnv = "QT_SERVER_WS_PORT";
constexpr const char *kDefaultWebSocketHost = "192.168.14.133";
constexpr int kDefaultWebSocketPort = 12345;

QUrl resolveWebSocketUrl() {
  const QString urlFromEnv = qEnvironmentVariable(kWebSocketUrlEnv).trimmed();
  if (!urlFromEnv.isEmpty()) {
    const QUrl envUrl(urlFromEnv);
    if (envUrl.isValid() && !envUrl.scheme().isEmpty() &&
        !envUrl.host().trimmed().isEmpty()) {
      return envUrl;
    }
  }

  QString host = qEnvironmentVariable(kWebSocketHostEnv).trimmed();
  if (host.isEmpty()) {
    host = QString::fromLatin1(kDefaultWebSocketHost);
  }

  bool ok = false;
  int port = qEnvironmentVariableIntValue(kWebSocketPortEnv, &ok);
  if (!ok || port <= 0 || port > 65535) {
    port = kDefaultWebSocketPort;
  }

  QUrl url;
  url.setScheme("ws");
  url.setHost(host);
  url.setPort(port);
  return url;
}

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

QString toUnsignedString(const QJsonValue &value) {
  if (value.isString()) {
    const QString s = value.toString().trimmed();
    static const QRegularExpression re(QStringLiteral("^\\d+$"));
    if (re.match(s).hasMatch()) {
      return s;
    }
  }
  if (value.isDouble()) {
    const qint64 v = value.toInteger(-1);
    if (v >= 0) {
      return QString::number(v);
    }
  }
  return QString();
}

QString extractLoginUserId(const protocol::Envelope &envelope) {
  const QJsonObject &data = envelope.data;

  QString userId = toUnsignedString(data.value("user_id"));
  if (!userId.isEmpty()) {
    return userId;
  }
  userId = toUnsignedString(data.value("uid"));
  if (!userId.isEmpty()) {
    return userId;
  }
  userId = toUnsignedString(data.value("id"));
  if (!userId.isEmpty()) {
    return userId;
  }

  if (data.value("user").isObject()) {
    const QJsonObject userObj = data.value("user").toObject();
    userId = toUnsignedString(userObj.value("user_id"));
    if (!userId.isEmpty()) {
      return userId;
    }
    userId = toUnsignedString(userObj.value("uid"));
    if (!userId.isEmpty()) {
      return userId;
    }
    userId = toUnsignedString(userObj.value("id"));
    if (!userId.isEmpty()) {
      return userId;
    }
  }

  return QString();
}

QString extractLoginUsername(const protocol::Envelope &envelope,
                             const QString &fallback) {
  const QJsonObject &data = envelope.data;
  if (data.value("username").isString()) {
    const QString username = data.value("username").toString().trimmed();
    if (!username.isEmpty()) {
      return username;
    }
  }
  if (data.value("user").isObject()) {
    const QJsonObject userObj = data.value("user").toObject();
    if (userObj.value("username").isString()) {
      const QString username = userObj.value("username").toString().trimmed();
      if (!username.isEmpty()) {
        return username;
      }
    }
    if (userObj.value("nickname").isString()) {
      const QString nickname = userObj.value("nickname").toString().trimmed();
      if (!nickname.isEmpty()) {
        return nickname;
      }
    }
  }
  return fallback;
}

QString extractUploadToken(const protocol::Envelope &envelope) {
  return envelope.data.value("upload_token").toString().trimmed();
}

QString extractUploadTokenType(const protocol::Envelope &envelope) {
  return envelope.data.value("upload_token_type").toString().trimmed();
}

QString extractUploadTokenExpiresAt(const protocol::Envelope &envelope) {
  return envelope.data.value("upload_token_expires_at").toString().trimmed();
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
  UserSession::instance().clear();
  qInfo() << "Start login request for user:" << m_pendingUsername;
  if (!ws->isConnected()) {
    const QUrl wsUrl = resolveWebSocketUrl();
    qInfo() << "Open websocket for login, url=" << wsUrl.toString();
    ws->open(wsUrl);
  } else {
    onWebSocketConnected();
  }
}

void LoginWindow::onRegisterClicked() {
  if (!m_registerWindow) {
    m_registerWindow = new RegisterWindow(nullptr);
    connect(m_registerWindow, &RegisterWindow::registerSuccess, this,
            [this](const QString &username) {
              ui->usernameEdit->setText(username.trimmed());
              ui->passwordEdit->clear();
              this->show();
              this->raise();
              this->activateWindow();
            });
    connect(m_registerWindow, &QObject::destroyed, this, [this]() {
      m_registerWindow = nullptr;
      this->show();
      this->raise();
      this->activateWindow();
    });
  }

  this->hide();
  m_registerWindow->show();
  m_registerWindow->raise();
  m_registerWindow->activateWindow();
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
    const QString loginUsername =
        extractLoginUsername(envelope, m_pendingUsername);
    const QString userId = extractLoginUserId(envelope);
    const QString uploadToken = extractUploadToken(envelope);
    const QString uploadTokenType = extractUploadTokenType(envelope);
    const QString uploadTokenExpiresAt = extractUploadTokenExpiresAt(envelope);

    UserSession::instance().setLoginContext(userId, loginUsername, uploadToken,
                                            uploadTokenType,
                                            uploadTokenExpiresAt);

    qInfo() << "Login success for user:" << loginUsername << "user_id:" << userId;
    if (userId.isEmpty()) {
      qWarning() << "Login response does not include valid numeric user_id";
    }
    if (uploadToken.isEmpty() || uploadTokenType.isEmpty() ||
        uploadTokenExpiresAt.isEmpty()) {
      qWarning() << "Login response missing upload token fields, user_id:" << userId
                 << "token_type:" << uploadTokenType
                 << "expires_at:" << uploadTokenExpiresAt;
      QMessageBox::warning(this, "登录提示",
                           "登录成功，但上传凭证缺失或不完整，头像上传将不可用。");
    } else if (UserSession::instance().isUploadTokenExpired()) {
      qWarning() << "Upload token already expired or invalid timestamp, user_id:"
                 << userId << "expires_at:" << uploadTokenExpiresAt;
      QMessageBox::warning(this, "登录提示",
                           "登录成功，但上传凭证已过期或时间格式无效，请重新登录。");
    } else {
      qInfo() << "Upload token received for user_id:" << userId
              << "token_type:" << uploadTokenType
              << "expires_at:" << uploadTokenExpiresAt;
    }
    m_pendingPassword.clear();
    emit loginSuccess(loginUsername, userId);
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
