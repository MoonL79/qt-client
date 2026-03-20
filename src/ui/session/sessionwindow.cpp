#include "sessionwindow.h"
#include "protocol.h"
#include <QAbstractSocket>
#include <QDateTime>
#include <QDebug>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScrollBar>
#include <QTimer>
#include <QUuid>

namespace {
QString presenceText(bool isOnline, const QString &lastSeenAtUtc) {
  if (isOnline) {
    return QStringLiteral("在线");
  }
  const QString trimmed = lastSeenAtUtc.trimmed();
  if (trimmed.isEmpty()) {
    return QStringLiteral("离线");
  }
  return QStringLiteral("离线 · 最近在线 %1").arg(trimmed);
}

QString jsonStringValue(const QJsonObject &obj, const char *key) {
  const QJsonValue value = obj.value(QLatin1String(key));
  if (value.isString()) {
    return value.toString().trimmed();
  }
  if (value.isDouble()) {
    return QString::number(static_cast<qint64>(value.toDouble()));
  }
  return QString();
}

qint64 jsonIntegerValue(const QJsonObject &obj, const char *key,
                        qint64 defaultValue = 0) {
  const QJsonValue value = obj.value(QLatin1String(key));
  if (value.isDouble()) {
    return static_cast<qint64>(value.toDouble(defaultValue));
  }
  if (value.isString()) {
    bool ok = false;
    const qint64 parsed = value.toString().trimmed().toLongLong(&ok);
    return ok ? parsed : defaultValue;
  }
  return defaultValue;
}

QString formatMessageTime(const QString &utcIsoTime) {
  const QString trimmed = utcIsoTime.trimmed();
  if (trimmed.isEmpty()) {
    return QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));
  }

  const QDateTime parsed = QDateTime::fromString(trimmed, Qt::ISODate);
  if (!parsed.isValid()) {
    return QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));
  }
  return parsed.toLocalTime().toString(QStringLiteral("HH:mm:ss"));
}

QString messageStatusText(SessionWindow::MessageStatus status) {
  switch (status) {
  case SessionWindow::MessageStatus::Pending:
    return QStringLiteral("发送中");
  case SessionWindow::MessageStatus::Sent:
    return QStringLiteral("已发送");
  case SessionWindow::MessageStatus::Failed:
    return QStringLiteral("发送失败");
  case SessionWindow::MessageStatus::Received:
    return QStringLiteral("已接收");
  }
  return QStringLiteral("未知状态");
}

QString messageErrorText(int code, const QString &fallback) {
  switch (code) {
  case 2001:
    return QStringLiteral("发送失败：未登录");
  case 2005:
    return QStringLiteral("发送失败：不是会话成员或已被禁言");
  case 4001:
    return QStringLiteral("发送失败：消息参数非法");
  case 4002:
    return QStringLiteral("发送失败：消息过大");
  case 4005:
    return QStringLiteral("发送失败：会话不存在");
  case 1099:
    return QStringLiteral("发送失败：服务端内部错误");
  default:
    break;
  }

  const QString trimmed = fallback.trimmed();
  if (!trimmed.isEmpty()) {
    return QStringLiteral("发送失败：%1").arg(trimmed);
  }
  return QStringLiteral("发送失败：未知错误(%1)").arg(code);
}
} // namespace

SessionWindow::SessionWindow(const Session &session, QWidget *parent)
    : QWidget(parent), m_session(session), m_isDragging(false),
      m_resizeDir(None), m_chatScroll(nullptr), m_chatContainer(nullptr),
      m_chatLayout(nullptr), m_inputLine(nullptr), m_sendBtn(nullptr),
      m_statusLabel(nullptr), m_presenceLabel(nullptr), m_testBtn(nullptr),
      m_websocket(websocketclient::instance()) {
  setAttribute(Qt::WA_DeleteOnClose);
  setMouseTracking(true); // Enable mouse tracking for resize cursor feedback
  initUI();
}

void SessionWindow::setFriendIdentity(const QString &userId,
                                      const QString &numericId) {
  m_friendUserId = userId.trimmed();
  m_friendNumericId = numericId.trimmed();
}

void SessionWindow::updateFriendPresence(bool isOnline,
                                         const QString &lastSeenAtUtc) {
  m_friendIsOnline = isOnline;
  m_friendLastSeenAtUtc = lastSeenAtUtc.trimmed();
  refreshPresenceLabel();
  qInfo().noquote() << "[SessionWindow] updated friend presence user_id="
                    << m_friendUserId << "numeric_id=" << m_friendNumericId
                    << "is_online=" << m_friendIsOnline
                    << "last_seen_at=" << m_friendLastSeenAtUtc;
}

void SessionWindow::initUI() {
  setWindowTitle(m_session.displayName());
  resize(600, 400);

  // 1. 去除系统标题栏
  setWindowFlags(Qt::FramelessWindowHint | Qt::WindowMinMaxButtonsHint);
  setAttribute(Qt::WA_TranslucentBackground);

  // 2. 主容器与样式
  QVBoxLayout *mainLayout = new QVBoxLayout(this);
  // 设置 5px
  // 的边距，这部分区域实际上是透明的，专门用于捕获鼠标移动事件来实现调整大小
  // 只有鼠标位于这 5px 的边缘区域时，事件才会直接发送给
  // SessionWindow，而不是被子控件遮挡
  mainLayout->setContentsMargins(2, 2, 2, 2);
  mainLayout->setSpacing(0);

  QWidget *container = new QWidget(this);
  container->setObjectName("SessionContainer");
  container->setStyleSheet("#SessionContainer { background-color: #f5f5f5; "
                           "border: 1px solid #dcdcdc; border-radius: 4px; }");
  mainLayout->addWidget(container);

  QVBoxLayout *containerLayout = new QVBoxLayout(container);
  containerLayout->setContentsMargins(0, 0, 0, 0);
  containerLayout->setSpacing(0);

  // 3. 自定义顶部标题栏
  QWidget *header = new QWidget(container);
  header->setFixedHeight(50);
  header->setStyleSheet(
      "background-color: #ffffff; border-bottom: 1px solid #e0e0e0; "
      "border-top-left-radius: 4px; border-top-right-radius: 4px;");

  QHBoxLayout *headerLayout = new QHBoxLayout(header);
  headerLayout->setContentsMargins(15, 6, 10, 6);

  // 标题文本 (居中)
  QLabel *titleLabel = new QLabel(m_session.displayName(), header);
  titleLabel->setStyleSheet("font-size: 16px; font-weight: bold; color: #333;");
  titleLabel->setAlignment(Qt::AlignCenter);
  m_presenceLabel = new QLabel(QStringLiteral("离线"), header);
  m_presenceLabel->setStyleSheet("font-size: 12px; color: #7a7a7a;");
  m_presenceLabel->setAlignment(Qt::AlignCenter);

  QVBoxLayout *titleLayout = new QVBoxLayout();
  titleLayout->setContentsMargins(0, 0, 0, 0);
  titleLayout->setSpacing(2);
  titleLayout->addWidget(titleLabel, 0, Qt::AlignCenter);
  titleLayout->addWidget(m_presenceLabel, 0, Qt::AlignCenter);

  // 关闭按钮
  QPushButton *closeBtn = new QPushButton("×", header);
  closeBtn->setFixedSize(30, 30);
  closeBtn->setStyleSheet(
      "QPushButton { border: none; font-weight: bold; color: #555; font-size: "
      "20px; background: transparent; }"
      "QPushButton:hover { background-color: #ff4d4d; color: white; "
      "border-radius: 4px; }");
  connect(closeBtn, &QPushButton::clicked, this, &QWidget::close);

  // 布局组装：使用弹簧将标题挤到中间（这里简单处理，左侧加弹簧，右侧加弹簧和按钮）
  // 为了严格居中，通常需要更复杂的布局，这里使用简单方式：
  // 左侧 Spacer - Title - Right Spacer - CloseBtn
  // 注意：如果有 CloseBtn 存在，绝对居中需要补偿右侧宽度。
  // 这里简化为：Title 占据主要空间并居中显示

  headerLayout->addStretch();
  headerLayout->addLayout(titleLayout);
  headerLayout->addStretch();
  headerLayout->addWidget(closeBtn);

  containerLayout->addWidget(header);

  // Enable mouse tracking
  container->setMouseTracking(true);
  header->setMouseTracking(true);

  // Install event filter
  container->installEventFilter(this);
  header->installEventFilter(this);

  // 内容区域
  QWidget *contentArea = new QWidget(container);
  contentArea->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  contentArea->setMouseTracking(true);   // Enable for content too
  contentArea->installEventFilter(this); // Filter for content too

  QVBoxLayout *contentLayout = new QVBoxLayout(contentArea);
  contentLayout->setContentsMargins(12, 12, 12, 12);
  contentLayout->setSpacing(12);

  m_chatScroll = new QScrollArea(contentArea);
  m_chatScroll->setWidgetResizable(true);
  m_chatScroll->setFrameShape(QFrame::NoFrame);
  m_chatScroll->setStyleSheet("QScrollArea { background-color: #ffffff; border: "
                              "1px solid #dcdcdc; border-radius: 8px; }");

  m_chatContainer = new QWidget(m_chatScroll);
  m_chatLayout = new QVBoxLayout(m_chatContainer);
  m_chatLayout->setContentsMargins(0, 10, 10, 10);
  m_chatLayout->setSpacing(8);
  m_chatLayout->setAlignment(Qt::AlignTop);

  m_chatScroll->setWidget(m_chatContainer);
  contentLayout->addWidget(m_chatScroll);

  QHBoxLayout *statusLayout = new QHBoxLayout();
  statusLayout->setContentsMargins(0, 0, 0, 0);
  statusLayout->setSpacing(8);

  m_statusLabel = new QLabel("连接状态: 未连接", contentArea);
  m_statusLabel->setStyleSheet("color: #666;");
  statusLayout->addWidget(m_statusLabel);
  statusLayout->addStretch();

  m_testBtn = new QPushButton("测试连接", contentArea);
  m_testBtn->setCursor(Qt::PointingHandCursor);
  m_testBtn->setStyleSheet(
      "QPushButton { background-color: #5c6bc0; color: white; border: none; "
      "border-radius: 4px; padding: 4px 12px; }"
      "QPushButton:hover { background-color: #4c5bb0; }");
  statusLayout->addWidget(m_testBtn);

  contentLayout->addLayout(statusLayout);

  QHBoxLayout *inputLayout = new QHBoxLayout();
  inputLayout->setSpacing(8);

  m_inputLine = new QLineEdit(contentArea);
  m_inputLine->setPlaceholderText("输入测试消息");
  m_inputLine->setStyleSheet(
      "border: 1px solid #dcdcdc; border-radius: 4px; padding: 4px;");
  inputLayout->addWidget(m_inputLine);

  m_sendBtn = new QPushButton("发送", contentArea);
  m_sendBtn->setCursor(Qt::PointingHandCursor);
  m_sendBtn->setStyleSheet(
      "QPushButton { background-color: #4a90e2; color: white; border: none; "
      "border-radius: 4px; padding: 6px 16px; }"
      "QPushButton:hover { background-color: #3a78d6; }");
  inputLayout->addWidget(m_sendBtn);

  connect(m_sendBtn, &QPushButton::clicked, this,
          &SessionWindow::onSendClicked);

  connect(m_inputLine, &QLineEdit::returnPressed, m_sendBtn,
          &QPushButton::click);
  connect(m_sendBtn, &QPushButton::clicked, this,
          &SessionWindow::sendPendingMessage);
  connect(m_testBtn, &QPushButton::clicked, this,
          &SessionWindow::onTestConnection);
  connect(m_websocket, &websocketclient::textMessageReceived, this,
          [this](const QString &message) {
            handleIncomingPayload(message, QStringLiteral("文本"));
            qDebug() << "Received text payload: " << message << Qt::endl;
          });
  connect(m_websocket, &websocketclient::binaryMessageReceived, this,
          [this](const QByteArray &data) {
            const QString payload = QString::fromUtf8(data);
            handleIncomingPayload(payload, QStringLiteral("二进制"));
            qDebug() << "Received binary payload: " << data << Qt::endl;
          });
  connect(m_websocket, &websocketclient::connected, this, [this]() {
    updateConnectionStatus(QAbstractSocket::ConnectedState);
    appendStatusLine("已连接");
  });
  connect(m_websocket, &websocketclient::disconnected, this, [this]() {
    updateConnectionStatus(QAbstractSocket::UnconnectedState);
    appendStatusLine("已断开");
  });
  connect(m_websocket, &websocketclient::stateChanged, this,
          [this](QAbstractSocket::SocketState state) {
            updateConnectionStatus(state);
          });
  connect(m_websocket, &websocketclient::errorOccurred, this,
          [this](QAbstractSocket::SocketError, const QString &message) {
            appendStatusLine("连接错误: " + message);
            updateConnectionStatus(m_websocket->state());
          });

  contentLayout->addLayout(inputLayout);

  containerLayout->addWidget(contentArea);
  refreshPresenceLabel();
}

void SessionWindow::sendPendingMessage() {
  if (!m_inputLine || !m_chatLayout)
    return;

  QString message = m_pendingMessage;
  if (message.isEmpty())
    message = m_inputLine->text().trimmed();
  if (message.isEmpty())
    return;

  m_pendingMessage.clear();
  const QString conversationId = m_session.conversationId().trimmed();
  if (conversationId.isEmpty()) {
    appendStatusLine(QStringLiteral("缺少 conversation_uuid，无法发送消息"));
    qWarning() << "[SessionWindow] missing conversation_uuid for display_name="
               << m_session.displayName() << "friend_user_id=" << m_friendUserId
               << "friend_numeric_id=" << m_friendNumericId;
    return;
  }

  ChatMessage localMessage;
  localMessage.localId = QUuid::createUuid().toString(QUuid::WithoutBraces);
  localMessage.requestId = QUuid::createUuid().toString(QUuid::WithoutBraces);
  localMessage.conversationId = conversationId;
  localMessage.content = message;
  localMessage.sentAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
  localMessage.senderUserId = UserSession::instance().userId().trimmed();
  localMessage.senderUsername = UserSession::instance().username().trimmed();
  localMessage.status = MessageStatus::Pending;
  const int messageIndex = appendMessage(localMessage);
  m_pendingMessageIndexesByRequestId.insert(localMessage.requestId, messageIndex);

  QJsonObject data;
  data.insert("conversation_id", conversationId);
  data.insert("content", message);

  const QString payload =
      protocol::createRequest("MESSAGE", "SEND", data, localMessage.requestId);
  qInfo() << "[SessionWindow] MESSAGE SEND request_id=" << localMessage.requestId
          << "conversation_id=" << conversationId;
  if (!m_websocket || !m_websocket->isConnected()) {
    markPendingMessageFailed(messageIndex, QStringLiteral("连接未建立"));
    appendStatusLine(QStringLiteral("发送失败：WebSocket 未连接"));
    return;
  }
  m_websocket->sendTextMessage(payload);
  m_inputLine->clear();
  emit outgoingMessageSubmitted(conversationId, message);
}

void SessionWindow::onSendClicked() {
  if (!m_inputLine)
    return;
  m_pendingMessage = m_inputLine->text().trimmed();
}

void SessionWindow::appendStatusLine(const QString &message) {
  const QString line =
      QDateTime::currentDateTime().toString("HH:mm:ss ") + message;
  appendChatBubble(line, false, true);
  qInfo() << "Session status:" << message;
}

QLabel *SessionWindow::appendChatBubble(const QString &message, bool outgoing,
                                        bool status) {
  if (!m_chatLayout || !m_chatContainer || !m_chatScroll)
    return nullptr;

  QWidget *row = new QWidget(m_chatContainer);
  QHBoxLayout *rowLayout = new QHBoxLayout(row);
  rowLayout->setContentsMargins(0, 0, 0, 0);

  QLabel *bubble = new QLabel(message, row);
  bubble->setWordWrap(true);
  bubble->setTextInteractionFlags(Qt::TextSelectableByMouse);
  bubble->setMaximumWidth(420);

  if (status) {
    bubble->setStyleSheet("QLabel { background: #f1f3f5; color: #4f5b66; "
                          "border-radius: 10px; padding: 8px 12px; }");
    rowLayout->addWidget(bubble);
    rowLayout->addStretch();
  } else if (outgoing) {
    bubble->setStyleSheet("QLabel { background: #e2f0ff; color: #1f3552; "
                          "border-radius: 12px; padding: 8px 12px; }");
    rowLayout->addStretch();
    rowLayout->addWidget(bubble);
  } else {
    bubble->setStyleSheet("QLabel { background: #f7f7f8; color: #2f2f2f; "
                          "border-radius: 12px; padding: 8px 12px; }");
    rowLayout->addWidget(bubble);
    rowLayout->addStretch();
  }

  m_chatLayout->addWidget(row);
  QTimer::singleShot(0, this, [this]() {
    if (m_chatScroll && m_chatScroll->verticalScrollBar()) {
      m_chatScroll->verticalScrollBar()->setValue(
          m_chatScroll->verticalScrollBar()->maximum());
    }
  });
  return bubble;
}

void SessionWindow::updateConnectionStatus(QAbstractSocket::SocketState state) {
  if (!m_statusLabel)
    return;
  QString stateText = "未知";
  switch (state) {
  case QAbstractSocket::UnconnectedState:
    stateText = "未连接";
    break;
  case QAbstractSocket::HostLookupState:
    stateText = "解析地址中";
    break;
  case QAbstractSocket::ConnectingState:
    stateText = "连接中";
    break;
  case QAbstractSocket::ConnectedState:
    stateText = "已连接";
    break;
  case QAbstractSocket::BoundState:
    stateText = "已绑定";
    break;
  case QAbstractSocket::ListeningState:
    stateText = "监听中";
    break;
  case QAbstractSocket::ClosingState:
    stateText = "关闭中";
    break;
  }
  m_statusLabel->setText("连接状态: " + stateText);
}

void SessionWindow::refreshPresenceLabel() {
  if (!m_presenceLabel) {
    return;
  }
  m_presenceLabel->setText(presenceText(m_friendIsOnline, m_friendLastSeenAtUtc));
}

void SessionWindow::onTestConnection() {
  if (!m_websocket)
    return;
  const auto state = m_websocket->state();
  updateConnectionStatus(state);
  if (state == QAbstractSocket::ConnectedState) {
    appendStatusLine("连通性测试: 已连接");
  } else {
    appendStatusLine("连通性测试: 未连接");
  }
}

void SessionWindow::handleIncomingPayload(const QString &payload,
                                          const QString &sourceTag) {
  if (!m_chatLayout)
    return;

  protocol::Envelope envelope;
  QString parseError;
  if (protocol::parseEnvelope(payload, &envelope, &parseError)) {
    if (envelope.type == QStringLiteral("MESSAGE") &&
        envelope.action == QStringLiteral("SEND")) {
      if (envelope.requestId.trimmed().isEmpty()) {
        handleIncomingMessagePush(envelope);
      } else {
        handleMessageSendResponse(envelope);
      }
    }
    return;
  }

  qWarning() << "Protocol parse failed, source:" << sourceTag << "error:"
             << parseError << "payload:" << payload;
}

int SessionWindow::appendMessage(const ChatMessage &message) {
  m_messages.push_back(message);
  const int index = m_messages.size() - 1;
  m_messages[index].bubbleLabel =
      appendChatBubble(QString(), message.status != MessageStatus::Received, false);
  updateMessageBubble(index);
  return index;
}

void SessionWindow::updateMessageBubble(int index) {
  if (index < 0 || index >= m_messages.size()) {
    return;
  }

  ChatMessage &message = m_messages[index];
  if (!message.bubbleLabel) {
    return;
  }

  const QString timeText = formatMessageTime(message.sentAt);
  QString bubbleText;
  if (message.status == MessageStatus::Received) {
    const QString sender =
        message.senderUsername.trimmed().isEmpty() ? QStringLiteral("对方")
                                                   : message.senderUsername.trimmed();
    bubbleText = QStringLiteral("%1 %2: %3")
                     .arg(timeText, sender, message.content);
  } else {
    bubbleText = QStringLiteral("%1 我: %2 [%3]")
                     .arg(timeText, message.content, messageStatusText(message.status));
    if (message.status == MessageStatus::Sent && message.seq > 0) {
      bubbleText += QStringLiteral(" (#%1)").arg(message.seq);
    }
  }
  message.bubbleLabel->setText(bubbleText);
}

void SessionWindow::handleMessageSendResponse(const protocol::Envelope &envelope) {
  const QString requestId = envelope.requestId.trimmed();
  if (requestId.isEmpty()) {
    return;
  }

  const auto it = m_pendingMessageIndexesByRequestId.constFind(requestId);
  if (it == m_pendingMessageIndexesByRequestId.cend()) {
    return;
  }

  const int index = it.value();
  if (index < 0 || index >= m_messages.size()) {
    m_pendingMessageIndexesByRequestId.remove(requestId);
    return;
  }

  ChatMessage &message = m_messages[index];
  const QString responseConversationId =
      jsonStringValue(envelope.data, "conversation_id");
  if (!responseConversationId.isEmpty() &&
      responseConversationId != m_session.conversationId().trimmed()) {
    qWarning() << "[SessionWindow] ignore MESSAGE/SEND response with mismatched "
                  "conversation_id request_id="
               << requestId << "response_conversation_id="
               << responseConversationId << "session_conversation_id="
               << m_session.conversationId();
    return;
  }

  const bool ok = envelope.code == 0 && envelope.data.value("ok").toBool(false);
  if (!ok) {
    const QString errorText =
        messageErrorText(envelope.code, jsonStringValue(envelope.data, "message"));
    markPendingMessageFailed(index, errorText);
    appendStatusLine(errorText);
    m_pendingMessageIndexesByRequestId.remove(requestId);
    qWarning() << "[SessionWindow] MESSAGE/SEND failed request_id=" << requestId
               << "code=" << envelope.code << "data="
               << QString::fromUtf8(
                      QJsonDocument(envelope.data).toJson(QJsonDocument::Compact));
    return;
  }

  message.conversationId = responseConversationId.isEmpty() ? message.conversationId
                                                            : responseConversationId;
  message.messageId = jsonStringValue(envelope.data, "message_id");
  message.seq = jsonIntegerValue(envelope.data, "seq");
  const QString sentAt = jsonStringValue(envelope.data, "sent_at");
  if (!sentAt.isEmpty()) {
    message.sentAt = sentAt;
  }
  const QString content = jsonStringValue(envelope.data, "content");
  if (!content.isEmpty()) {
    message.content = content;
  }
  message.status = MessageStatus::Sent;
  updateMessageBubble(index);
  m_pendingMessageIndexesByRequestId.remove(requestId);

  qInfo() << "[SessionWindow] MESSAGE/SEND ack request_id=" << requestId
          << "message_id=" << message.messageId << "seq=" << message.seq
          << "sent_at=" << message.sentAt;
}

void SessionWindow::handleIncomingMessagePush(const protocol::Envelope &envelope) {
  const QString conversationId = jsonStringValue(envelope.data, "conversation_id");
  if (conversationId.isEmpty() ||
      conversationId != m_session.conversationId().trimmed()) {
    return;
  }

  ChatMessage incoming;
  incoming.localId = QUuid::createUuid().toString(QUuid::WithoutBraces);
  incoming.conversationId = conversationId;
  incoming.messageId = jsonStringValue(envelope.data, "message_id");
  incoming.seq = jsonIntegerValue(envelope.data, "seq");
  incoming.content = jsonStringValue(envelope.data, "content");
  incoming.sentAt = jsonStringValue(envelope.data, "sent_at");
  incoming.senderUserId = jsonStringValue(envelope.data, "from_user_id");
  incoming.senderUsername = jsonStringValue(envelope.data, "from_username");
  incoming.status = MessageStatus::Received;

  if (incoming.content.isEmpty()) {
    qWarning() << "[SessionWindow] ignore incoming MESSAGE/SEND without content "
                  "conversation_id="
               << conversationId;
    return;
  }

  appendMessage(incoming);
  qInfo() << "[SessionWindow] received incoming MESSAGE/SEND conversation_id="
          << conversationId << "message_id=" << incoming.messageId
          << "from_user_id=" << incoming.senderUserId;
}

void SessionWindow::markPendingMessageFailed(int index, const QString &reason) {
  if (index < 0 || index >= m_messages.size()) {
    return;
  }

  ChatMessage &message = m_messages[index];
  message.status = MessageStatus::Failed;
  updateMessageBubble(index);
  qWarning() << "[SessionWindow] pending message failed request_id="
             << message.requestId << "reason=" << reason;
}

bool SessionWindow::eventFilter(QObject *obj, QEvent *event) {
  if (event->type() == QEvent::MouseMove ||
      event->type() == QEvent::MouseButtonPress ||
      event->type() == QEvent::MouseButtonRelease) {

    QMouseEvent *mouseEvent = static_cast<QMouseEvent *>(event);
    if (event->type() == QEvent::MouseMove)
      handleMouseMove(mouseEvent);
    else if (event->type() == QEvent::MouseButtonPress)
      handleMousePress(mouseEvent);
    else if (event->type() == QEvent::MouseButtonRelease)
      handleMouseRelease(mouseEvent);

    return false;
  }
  return QWidget::eventFilter(obj, event);
}

// --- 拖拽与缩放支持 ---
void SessionWindow::checkCursorShape(const QPoint &globalPos) {
  const int margin = 8;
  QPoint pos = mapFromGlobal(globalPos);

  int w = width();
  int h = height();
  int x = pos.x();
  int y = pos.y();

  bool left = x < margin;
  bool right = x > w - margin;
  bool top = y < margin;
  bool bottom = y > h - margin;

  if (top && left)
    m_resizeDir = TopLeft;
  else if (top && right)
    m_resizeDir = TopRight;
  else if (bottom && left)
    m_resizeDir = BottomLeft;
  else if (bottom && right)
    m_resizeDir = BottomRight;
  else if (top)
    m_resizeDir = Top;
  else if (bottom)
    m_resizeDir = Bottom;
  else if (left)
    m_resizeDir = Left;
  else if (right)
    m_resizeDir = Right;
  else
    m_resizeDir = None;

  if (m_resizeDir == TopLeft || m_resizeDir == BottomRight)
    setCursor(Qt::SizeFDiagCursor);
  else if (m_resizeDir == TopRight || m_resizeDir == BottomLeft)
    setCursor(Qt::SizeBDiagCursor);
  else if (m_resizeDir == Left || m_resizeDir == Right)
    setCursor(Qt::SizeHorCursor);
  else if (m_resizeDir == Top || m_resizeDir == Bottom)
    setCursor(Qt::SizeVerCursor);
  else
    setCursor(Qt::ArrowCursor);
}

void SessionWindow::handleMousePress(QMouseEvent *event) {
  if (event->button() == Qt::LeftButton) {
    if (m_resizeDir != None) {
      // Resize mode started
    } else {
      // Move mode
      m_isDragging = true;
      m_dragPosition =
          event->globalPosition().toPoint() - frameGeometry().topLeft();
    }
  }
}

void SessionWindow::handleMouseMove(QMouseEvent *event) {
  if (event->buttons() & Qt::LeftButton) {
    if (m_resizeDir != None) {
      // Handle resizing
      QPoint globalPos = event->globalPosition().toPoint();
      QRect rect = geometry();
      int minW = minimumWidth();
      int minH = minimumHeight();

      if (m_resizeDir == Left || m_resizeDir == TopLeft ||
          m_resizeDir == BottomLeft) {
        int newW = rect.right() - globalPos.x();
        if (newW > minW)
          rect.setLeft(globalPos.x());
      }
      if (m_resizeDir == Right || m_resizeDir == TopRight ||
          m_resizeDir == BottomRight) {
        rect.setRight(globalPos.x());
      }
      if (m_resizeDir == Top || m_resizeDir == TopLeft ||
          m_resizeDir == TopRight) {
        int newH = rect.bottom() - globalPos.y();
        if (newH > minH)
          rect.setTop(globalPos.y());
      }
      if (m_resizeDir == Bottom || m_resizeDir == BottomLeft ||
          m_resizeDir == BottomRight) {
        rect.setBottom(globalPos.y());
      }
      setGeometry(rect);
    } else if (m_isDragging) {
      move(event->globalPosition().toPoint() - m_dragPosition);
    }
  } else {
    checkCursorShape(event->globalPosition().toPoint());
  }
}

void SessionWindow::handleMouseRelease(QMouseEvent *event) {
  if (event->button() == Qt::LeftButton) {
    m_isDragging = false;
  }
}

void SessionWindow::mousePressEvent(QMouseEvent *event) {
  handleMousePress(event);
  event->accept();
}

void SessionWindow::mouseMoveEvent(QMouseEvent *event) {
  handleMouseMove(event);
  event->accept();
}

void SessionWindow::mouseReleaseEvent(QMouseEvent *event) {
  handleMouseRelease(event);
  event->accept();
}
