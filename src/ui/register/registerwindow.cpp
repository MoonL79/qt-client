#include "registerwindow.h"

#include "protocol.h"
#include "ui_registerwindow.h"
#include "websocketclient.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QUrl>

namespace {
constexpr const char *kWebSocketUrl = "ws://192.168.14.133:12345";
constexpr int kRegisterTimeoutMs = 10000;
constexpr int kRegisterWindowBaseWidth = 560;
constexpr int kRegisterWindowExpandDelta = 100;

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

bool isRegisterSuccess(const protocol::Envelope &envelope) {
  if (!envelope.hasCode || envelope.code != 0) {
    return false;
  }
  return envelope.data.value("ok").toBool(false);
}
} // namespace

RegisterWindow::RegisterWindow(QWidget *parent)
    : QWidget(parent), ui(new Ui::RegisterWindow) {
  ui->setupUi(this);
  setWindowTitle("注册 - IM聊天软件");
  setFixedSize(kRegisterWindowBaseWidth, 620);
  setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
  setAttribute(Qt::WA_TranslucentBackground);

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

RegisterWindow::~RegisterWindow() { delete ui; }

void RegisterWindow::setRegisterLoading(bool loading, const QString &text) {
  ui->registerButton->setEnabled(!loading);
  ui->registerButton->setText(text);
}

void RegisterWindow::resetPendingState() {
  m_isRegisterPending = false;
  m_pendingRegisterRequestId.clear();
  m_requestTimer.stop();
  setRegisterLoading(false, "注 册");
}

void RegisterWindow::applyNormalizedInput(const auth::RegisterInput &normalized) {
  ui->usernameEdit->setText(normalized.username);
  ui->emailEdit->setText(normalized.email);
  ui->passwordEdit->setText(normalized.password);
  ui->nicknameEdit->setText(normalized.nickname);
  ui->phoneEdit->setText(normalized.phone);
}

void RegisterWindow::sendRegisterRequest() {
  QString requestId;
  const QString payload = auth::createRegisterRequestPayload(
      m_pendingInput, QString(), &requestId);
  m_pendingRegisterRequestId = requestId;
  websocketclient::instance()->sendTextMessage(payload);
  setRegisterLoading(true, "注册中...");
  m_requestTimer.start();
}

void RegisterWindow::onRegisterClicked() {
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
    QMessageBox::warning(this, "输入错误", validation.errorMessage);
    return;
  }
  if (validation.normalized.password != confirmPassword) {
    QMessageBox::warning(this, "输入错误", "两次输入的密码不一致");
    ui->confirmPasswordEdit->setFocus();
    return;
  }

  m_pendingInput = validation.normalized;
  m_isRegisterPending = true;
  setRegisterLoading(true, "连接中...");

  auto ws = websocketclient::instance();
  if (!ws->isConnected()) {
    ws->open(QUrl(QString::fromLatin1(kWebSocketUrl)));
  } else {
    onWebSocketConnected();
  }
}

void RegisterWindow::onBackClicked() { close(); }

void RegisterWindow::onCloseClicked() { close(); }

void RegisterWindow::onWebSocketConnected() {
  if (!m_isRegisterPending) {
    return;
  }
  sendRegisterRequest();
}

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

void RegisterWindow::onWebSocketDisconnected() {
  if (!m_isRegisterPending) {
    return;
  }
  resetPendingState();
  QMessageBox::warning(this, "连接断开", "连接已断开，请重试注册。");
}

void RegisterWindow::onWebSocketError(QAbstractSocket::SocketError,
                                      const QString &message) {
  if (!m_isRegisterPending) {
    return;
  }
  resetPendingState();
  QMessageBox::warning(this, "注册失败",
                       QStringLiteral("网络异常，请重试。%1").arg(message));
}

void RegisterWindow::onRequestTimeout() {
  if (!m_isRegisterPending) {
    return;
  }
  resetPendingState();
  QMessageBox::warning(this, "注册超时", "请求超时，请重试。");
}

void RegisterWindow::paintEvent(QPaintEvent *) {
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing);
  QPainterPath path;
  path.addRoundedRect(rect(), 20, 20);
  painter.fillPath(path, QColor("#f5f5f5"));
  painter.setPen(QPen(QColor("#d9d9d9"), 1));
  painter.drawPath(path);
}

void RegisterWindow::mousePressEvent(QMouseEvent *event) {
  if (event->button() == Qt::LeftButton) {
    m_isDragging = true;
    m_dragPosition = event->globalPosition().toPoint() - frameGeometry().topLeft();
    event->accept();
  }
}

void RegisterWindow::mouseMoveEvent(QMouseEvent *event) {
  if (m_isDragging && (event->buttons() & Qt::LeftButton)) {
    move(event->globalPosition().toPoint() - m_dragPosition);
    event->accept();
  }
}

void RegisterWindow::mouseReleaseEvent(QMouseEvent *event) {
  if (event->button() == Qt::LeftButton) {
    m_isDragging = false;
    event->accept();
  }
}
