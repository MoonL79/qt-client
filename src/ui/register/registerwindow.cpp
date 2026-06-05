#include "registerwindow.h"

#include "protocol.h"
#include "ui_registerwindow.h"
#include "websocketclient.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QStyle>
#include <QUrl>
#include <QtGlobal>

namespace {
constexpr const char *kWebSocketUrlEnv = "QT_SERVER_WS_URL";
constexpr const char *kWebSocketHostEnv = "QT_SERVER_WS_HOST";
constexpr const char *kWebSocketPortEnv = "QT_SERVER_WS_PORT";
constexpr const char *kDefaultWebSocketHost = "192.168.14.133";
constexpr int kDefaultWebSocketPort = 12345;
constexpr int kRegisterTimeoutMs = 10000;
constexpr int kRegisterWindowBaseWidth = 560;
constexpr int kRegisterWindowExpandDelta = 100;

// 解析并确定WebSocketurl结果。
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

// 判断当前注册响应条件是否满足。
bool isCurrentRegisterResponse(const protocol::Envelope &envelope,
                               const QString &pendingRequestId) {
  if (pendingRequestId.isEmpty()) {
    return false;
  }
  if (envelope.requestId == pendingRequestId) {
    return true;
  }
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
         originalRequest.type == "AUTH" && originalRequest.action == "REGISTER";
}

// 提取响应消息信息。
QString extractResponseMessage(const protocol::Envelope &envelope) {
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
    return QString::fromUtf8(
               QJsonDocument(data.value("error").toObject())
                   .toJson(QJsonDocument::Compact))
        .trimmed();
  }
  return QString();
}

// 判断注册成功条件是否满足。
bool isRegisterSuccess(const protocol::Envelope &envelope) {
  if (!envelope.hasCode || envelope.code != 0) {
    return false;
  }
  return envelope.data.value("ok").toBool(false);
}
} // namespace

// 实现 `ui` 的核心逻辑。
RegisterWindow::RegisterWindow(QWidget *parent)
    : QWidget(parent), ui(new Ui::RegisterWindow) {
  ui->setupUi(this);
  setWindowTitle("注册 - IM聊天软件");
  setFixedSize(kRegisterWindowBaseWidth, 620);
  setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
  setAttribute(Qt::WA_TranslucentBackground);
  setAttribute(Qt::WA_DeleteOnClose, true);

  ui->passwordEdit->setEchoMode(QLineEdit::Password);
  ui->confirmPasswordEdit->setEchoMode(QLineEdit::Password);

  connect(ui->registerButton, &QPushButton::clicked, this,
          &RegisterWindow::onRegisterClicked);
  connect(ui->backButton, &QPushButton::clicked, this,
          &RegisterWindow::onBackClicked);
  connect(ui->closeButton, &QPushButton::clicked, this,
          &RegisterWindow::onCloseClicked);
  connect(ui->passwordEdit, &QLineEdit::returnPressed, this,
          &RegisterWindow::onRegisterClicked);
  connect(ui->confirmPasswordEdit, &QLineEdit::returnPressed, this,
          &RegisterWindow::onRegisterClicked);

  m_requestTimer.setSingleShot(true);
  m_requestTimer.setInterval(kRegisterTimeoutMs);
  connect(&m_requestTimer, &QTimer::timeout, this,
          &RegisterWindow::onRequestTimeout);

  auto ws = websocketclient::instance();
  connect(ws, &websocketclient::connected, this,
          &RegisterWindow::onWebSocketConnected);
  connect(ws, &websocketclient::textMessageReceived, this,
          &RegisterWindow::onWebSocketTextMessage);
  connect(ws, &websocketclient::disconnected, this,
          &RegisterWindow::onWebSocketDisconnected);
  connect(ws, &websocketclient::errorOccurred, this,
          &RegisterWindow::onWebSocketError);
}

// 析构 RegisterWindow 实例并释放相关资源。
RegisterWindow::~RegisterWindow() { delete ui; }

// 设置lineedit错误值。
void RegisterWindow::setLineEditError(QLineEdit *lineEdit, bool hasError) {
  if (!lineEdit) {
    return;
  }
  lineEdit->setProperty("error", hasError);
  lineEdit->style()->unpolish(lineEdit);
  lineEdit->style()->polish(lineEdit);
  lineEdit->update();
}

// 清理fielderrors状态。
void RegisterWindow::clearFieldErrors() {
  setLineEditError(ui->usernameEdit, false);
  setLineEditError(ui->emailEdit, false);
  setLineEditError(ui->passwordEdit, false);
  setLineEditError(ui->confirmPasswordEdit, false);
  setLineEditError(ui->nicknameEdit, false);
  setLineEditError(ui->phoneEdit, false);
}

// 设置注册loading值。
void RegisterWindow::setRegisterLoading(bool loading, const QString &text) {
  ui->registerButton->setEnabled(!loading);
  ui->registerButton->setText(text);
}

// 实现 `resetPendingState` 的核心逻辑。
void RegisterWindow::resetPendingState() {
  m_isRegisterPending = false;
  m_pendingRegisterRequestId.clear();
  m_requestTimer.stop();
  setRegisterLoading(false, "注 册");
}

// 应用normalizedinput配置。
void RegisterWindow::applyNormalizedInput(const auth::RegisterInput &normalized) {
  ui->usernameEdit->setText(normalized.username);
  ui->emailEdit->setText(normalized.email);
  ui->passwordEdit->setText(normalized.password);
  ui->nicknameEdit->setText(normalized.nickname);
  ui->phoneEdit->setText(normalized.phone);
}

// 发送注册请求数据。
void RegisterWindow::sendRegisterRequest() {
  QString requestId;
  const QString payload = auth::createRegisterRequestPayload(
      m_pendingInput, QString(), &requestId);
  m_pendingRegisterRequestId = requestId;
  websocketclient::instance()->sendTextMessage(payload);
  setRegisterLoading(true, "注册中...");
  m_requestTimer.start();
}

// 响应注册点击事件。
void RegisterWindow::onRegisterClicked() {
  clearFieldErrors();

  if (!m_hasExpandedOnRegisterClick) {
    setFixedSize(width() + kRegisterWindowExpandDelta, height());
    m_hasExpandedOnRegisterClick = true;
  }

  if (m_isRegisterPending) {
    return;
  }

  auth::RegisterInput rawInput;
  rawInput.username = ui->usernameEdit->text();
  rawInput.email = ui->emailEdit->text();
  rawInput.password = ui->passwordEdit->text();
  const QString confirmPassword = ui->confirmPasswordEdit->text().trimmed();
  rawInput.nickname = ui->nicknameEdit->text();
  rawInput.phone = ui->phoneEdit->text();
  rawInput.avatarUrl.clear();
  rawInput.bio.clear();

  const auth::RegisterValidationResult validation =
      auth::validateRegisterInput(rawInput);
  applyNormalizedInput(validation.normalized);
  if (!validation.ok) {
    if (validation.errorMessage.contains("用户名")) {
      setLineEditError(ui->usernameEdit, true);
      ui->usernameEdit->setFocus();
    } else if (validation.errorMessage.contains("邮箱")) {
      setLineEditError(ui->emailEdit, true);
      ui->emailEdit->setFocus();
    } else if (validation.errorMessage.contains("密码")) {
      setLineEditError(ui->passwordEdit, true);
      ui->passwordEdit->setFocus();
    } else if (validation.errorMessage.contains("昵称")) {
      setLineEditError(ui->nicknameEdit, true);
      ui->nicknameEdit->setFocus();
    } else if (validation.errorMessage.contains("手机号")) {
      setLineEditError(ui->phoneEdit, true);
      ui->phoneEdit->setFocus();
    }
    QMessageBox::warning(this, "输入错误", validation.errorMessage);
    return;
  }
  if (validation.normalized.password != confirmPassword) {
    setLineEditError(ui->confirmPasswordEdit, true);
    QMessageBox::warning(this, "输入错误", "两次输入的密码不一致");
    ui->confirmPasswordEdit->setFocus();
    return;
  }

  m_pendingInput = validation.normalized;
  m_isRegisterPending = true;
  setRegisterLoading(true, "连接中...");

  auto ws = websocketclient::instance();
  if (!ws->isConnected()) {
    const QUrl wsUrl = resolveWebSocketUrl();
    qInfo() << "Open websocket for register, url=" << wsUrl.toString();
    ws->open(wsUrl);
  } else {
    onWebSocketConnected();
  }
}

// 关闭`close`资源或连接。
void RegisterWindow::onBackClicked() { close(); }

// 关闭`close`资源或连接。
void RegisterWindow::onCloseClicked() { close(); }

// 响应WebSocket已连接事件。
void RegisterWindow::onWebSocketConnected() {
  if (!m_isRegisterPending) {
    return;
  }
  sendRegisterRequest();
}

// 响应WebSocket文本消息事件。
void RegisterWindow::onWebSocketTextMessage(const QString &message) {
  if (!m_isRegisterPending || m_pendingRegisterRequestId.isEmpty()) {
    return;
  }

  protocol::Envelope envelope;
  QString parseError;
  if (!protocol::parseEnvelope(message, &envelope, &parseError)) {
    resetPendingState();
    QMessageBox::warning(this, "注册失败",
                         QStringLiteral("响应解析失败: %1")
                             .arg(parseError.isEmpty()
                                      ? QStringLiteral("未知协议错误")
                                      : parseError));
    return;
  }
  if (!isCurrentRegisterResponse(envelope, m_pendingRegisterRequestId)) {
    return;
  }
  if (envelope.type != "AUTH" || envelope.action != "REGISTER") {
    const QString msg = extractResponseMessage(envelope);
    resetPendingState();
    QMessageBox::warning(
        this, "注册失败",
        msg.isEmpty() ? QStringLiteral("响应类型不匹配") : msg);
    return;
  }

  if (isRegisterSuccess(envelope)) {
    const QJsonObject user = envelope.data.value("user").toObject();
    const QString username = user.value("username").toString(m_pendingInput.username);
    resetPendingState();
    QMessageBox::information(this, "注册成功",
                             QStringLiteral("注册成功，请登录。"));
    emit registerSuccess(username);
    close();
    return;
  }

  const int code = envelope.hasCode ? envelope.code : -1;
  const QString dataMessage = extractResponseMessage(envelope);
  QString displayMessage;
  if (code == 2006) {
    displayMessage = QStringLiteral("用户名或邮箱已存在");
  } else if (code == 2007) {
    displayMessage = QStringLiteral("注册失败，请稍后重试");
  } else if (code == 1003) {
    displayMessage = QStringLiteral("参数不合法，请检查输入");
  } else {
    const QString base = dataMessage.isEmpty() ? QStringLiteral("注册失败") : dataMessage;
    displayMessage = QStringLiteral("%1 (code=%2)").arg(base).arg(code);
  }

  resetPendingState();
  QMessageBox::warning(this, "注册失败", displayMessage);
}

// 响应WebSocket已断开事件。
void RegisterWindow::onWebSocketDisconnected() {
  if (!m_isRegisterPending) {
    return;
  }
  resetPendingState();
  QMessageBox::warning(this, "连接断开", "连接已断开，请重试注册。");
}

// 响应WebSocket错误事件。
void RegisterWindow::onWebSocketError(QAbstractSocket::SocketError,
                                      const QString &message) {
  if (!m_isRegisterPending) {
    return;
  }
  resetPendingState();
  QMessageBox::warning(this, "注册失败",
                       QStringLiteral("网络异常，请重试。%1").arg(message));
}

// 响应请求timeout事件。
void RegisterWindow::onRequestTimeout() {
  if (!m_isRegisterPending) {
    return;
  }
  resetPendingState();
  QMessageBox::warning(this, "注册超时", "请求超时，请重试。");
}

// 实现 `paintEvent` 的核心逻辑。
void RegisterWindow::paintEvent(QPaintEvent *) {
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing);
  QPainterPath path;
  path.addRoundedRect(rect(), 20, 20);
  painter.fillPath(path, QColor("#f5f5f5"));
  painter.setPen(QPen(QColor("#d9d9d9"), 1));
  painter.drawPath(path);
}

// 实现 `mousePressEvent` 的核心逻辑。
void RegisterWindow::mousePressEvent(QMouseEvent *event) {
  if (event->button() == Qt::LeftButton) {
    m_isDragging = true;
    m_dragPosition = event->globalPosition().toPoint() - frameGeometry().topLeft();
    event->accept();
  }
}

// 实现 `mouseMoveEvent` 的核心逻辑。
void RegisterWindow::mouseMoveEvent(QMouseEvent *event) {
  if (m_isDragging && (event->buttons() & Qt::LeftButton)) {
    move(event->globalPosition().toPoint() - m_dragPosition);
    event->accept();
  }
}

// 实现 `mouseReleaseEvent` 的核心逻辑。
void RegisterWindow::mouseReleaseEvent(QMouseEvent *event) {
  if (event->button() == Qt::LeftButton) {
    m_isDragging = false;
    event->accept();
  }
}

