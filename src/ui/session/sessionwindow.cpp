#include "sessionwindow.h"
#include "protocol.h"
#include <QAbstractSocket>
#include <QDateTime>
#include <QDebug>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScrollBar>
#include <QSizePolicy>
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
    : FramelessWindowBase(parent), m_session(session), m_chatScroll(nullptr),
      m_chatContainer(nullptr), m_chatLayout(nullptr), m_inputLine(nullptr),
      m_sendBtn(nullptr), m_presenceLabel(nullptr),
      m_websocket(websocketclient::instance()) {
  setAttribute(Qt::WA_DeleteOnClose);
  setAttribute(Qt::WA_TranslucentBackground);
  setStandardTitleBarVisible(false);
  setResizeBorderWidth(8);
  initUI();
}

void SessionWindow::loadHistory(const QVector<ChatMessage> &messages) {
  for (const ChatMessage &message : messages) {
    appendPersistedMessage(message);
  }
}

void SessionWindow::setPeerIdentity(const QString &userId,
                                    const QString &numericId) {
  m_peerUserId = userId.trimmed();
  m_peerNumericId = numericId.trimmed();
}

void SessionWindow::updatePeerPresence(bool isOnline,
                                       const QString &lastSeenAtUtc) {
  m_peerIsOnline = isOnline;
  m_peerLastSeenAtUtc = lastSeenAtUtc.trimmed();
  refreshPresenceLabel();
  qInfo().noquote() << "[SessionWindow] updated peer presence user_id="
                    << m_peerUserId << "numeric_id=" << m_peerNumericId
                    << "is_online=" << m_peerIsOnline << "last_seen_at="
                    << m_peerLastSeenAtUtc;
}

void SessionWindow::initUI() {
  setWindowTitle(m_session.displayName());
  resize(760, 780);
  setMinimumSize(680, 620);

  QWidget *root = contentWidget();
  root->setAttribute(Qt::WA_TranslucentBackground);
  root->setStyleSheet("background: transparent;");
  QVBoxLayout *mainLayout = new QVBoxLayout(root);
  mainLayout->setContentsMargins(0, 0, 0, 0);
  mainLayout->setSpacing(0);

  QWidget *container = new QWidget(root);
  container->setObjectName("SessionContainer");
  container->setStyleSheet("#SessionContainer { background-color: #ffffff; "
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
  addDragRegion(header);

  // 内容区域
  QWidget *contentArea = new QWidget(container);
  contentArea->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

  QVBoxLayout *contentLayout = new QVBoxLayout(contentArea);
  contentLayout->setContentsMargins(12, 12, 12, 12);
  contentLayout->setSpacing(12);

  m_chatScroll = new QScrollArea(contentArea);
  m_chatScroll->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  m_chatScroll->setWidgetResizable(true);
  m_chatScroll->setFrameShape(QFrame::NoFrame);
  m_chatScroll->setStyleSheet("QScrollArea { background-color: #ffffff; border: "
                              "1px solid #dcdcdc; border-radius: 8px; }");
  m_chatScroll->viewport()->setStyleSheet("background-color: #ffffff;");

  m_chatContainer = new QWidget(m_chatScroll);
  m_chatContainer->setStyleSheet("background-color: #ffffff;");
  m_chatLayout = new QVBoxLayout(m_chatContainer);
  m_chatLayout->setContentsMargins(0, 10, 10, 10);
  m_chatLayout->setSpacing(8);
  m_chatLayout->setAlignment(Qt::AlignTop);

  m_chatScroll->setWidget(m_chatContainer);
  contentLayout->addWidget(m_chatScroll, 3);

  QWidget *attachmentBar = new QWidget(contentArea);
  attachmentBar->setObjectName("SessionAttachmentBar");
  attachmentBar->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  attachmentBar->setStyleSheet(
      "#SessionAttachmentBar { background-color: #fbfcfe; border: 1px solid "
      "#e6ebf2; border-radius: 10px; }");

  QHBoxLayout *attachmentLayout = new QHBoxLayout(attachmentBar);
  attachmentLayout->setContentsMargins(10, 7, 10, 7);
  attachmentLayout->setSpacing(8);

  auto attachmentButtonStyle = QStringLiteral(
      "QPushButton { min-width: 58px; min-height: 30px; background-color: "
      "#f3f7fd; color: #25476a; border: 1px solid #d7e3f3; border-radius: "
      "8px; padding: 0 12px; }"
      "QPushButton:hover { background-color: #e7f0fb; border-color: #a9c7ef; }");

  QPushButton *imageButton = new QPushButton(QStringLiteral("图片"), attachmentBar);
  imageButton->setCursor(Qt::PointingHandCursor);
  imageButton->setStyleSheet(attachmentButtonStyle);
  attachmentLayout->addWidget(imageButton, 0, Qt::AlignLeft);

  QPushButton *fileButton = new QPushButton(QStringLiteral("文件"), attachmentBar);
  fileButton->setCursor(Qt::PointingHandCursor);
  fileButton->setStyleSheet(attachmentButtonStyle);
  attachmentLayout->addWidget(fileButton, 0, Qt::AlignLeft);
  attachmentLayout->addStretch();

  connect(imageButton, &QPushButton::clicked, this,
          [this]() { requestAttachment(true); });
  connect(fileButton, &QPushButton::clicked, this,
          [this]() { requestAttachment(false); });

  contentLayout->addWidget(attachmentBar);

  QWidget *inputWrapper = new QWidget(contentArea);
  inputWrapper->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

  QGridLayout *inputLayout = new QGridLayout(inputWrapper);
  inputLayout->setContentsMargins(0, 0, 0, 0);
  inputLayout->setHorizontalSpacing(0);
  inputLayout->setVerticalSpacing(0);
  inputLayout->setRowStretch(0, 1);

  m_inputLine = new QTextEdit(inputWrapper);
  m_inputLine->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  m_inputLine->setPlaceholderText(QStringLiteral("输入消息"));
  m_inputLine->setAcceptRichText(false);
  m_inputLine->setMinimumHeight(150);
  m_inputLine->setStyleSheet(
      "QTextEdit { border: 1px solid #dcdcdc; border-radius: 8px; "
      "padding: 8px 88px 40px 8px; background: #ffffff; color: #111111; }");
  inputLayout->addWidget(m_inputLine, 0, 0);

  m_sendBtn = new QPushButton("发送", inputWrapper);
  m_sendBtn->setCursor(Qt::PointingHandCursor);
  m_sendBtn->setFixedHeight(32);
  m_sendBtn->setMinimumWidth(68);
  m_sendBtn->setStyleSheet(
      "QPushButton { background-color: #4a90e2; color: white; border: none; "
      "border-radius: 6px; padding: 6px 16px; }"
      "QPushButton:hover { background-color: #3a78d6; }");
  inputLayout->addWidget(m_sendBtn, 0, 0, Qt::AlignRight | Qt::AlignBottom);

  connect(m_sendBtn, &QPushButton::clicked, this,
          &SessionWindow::onSendClicked);
  connect(m_sendBtn, &QPushButton::clicked, this,
          &SessionWindow::sendPendingMessage);
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
  connect(m_websocket, &websocketclient::errorOccurred, this,
          [this](QAbstractSocket::SocketError, const QString &message) {
            appendStatusLine("连接错误: " + message);
          });

  contentLayout->addWidget(inputWrapper, 1);

  containerLayout->addWidget(contentArea);
  refreshPresenceLabel();
}

void SessionWindow::sendPendingMessage() {
  if (!m_inputLine || !m_chatLayout)
    return;

  QString message = m_pendingMessage;
  if (message.isEmpty())
    message = m_inputLine->toPlainText().trimmed();
  if (message.isEmpty())
    return;

  m_pendingMessage.clear();
  const QString conversationId = m_session.conversationId().trimmed();
  if (conversationId.isEmpty()) {
    appendStatusLine(QStringLiteral("缺少 conversation_id，无法发送消息"));
    qWarning() << "[SessionWindow] missing conversation_id for display_name="
               << m_session.displayName() << "peer_user_id=" << m_peerUserId
               << "peer_numeric_id=" << m_peerNumericId;
    return;
  }

  ChatMessage localMessage;
  localMessage.localId = QUuid::createUuid().toString(QUuid::WithoutBraces);
  localMessage.requestId = QUuid::createUuid().toString(QUuid::WithoutBraces);
  localMessage.conversationId = conversationId;
  localMessage.kind = ChatMessageKind::Text;
  localMessage.text = message;
  localMessage.sentAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
  localMessage.senderUserId = UserSession::instance().userId().trimmed();
  localMessage.senderNumericId = UserSession::instance().numericId().trimmed();
  localMessage.senderUsername = UserSession::instance().username().trimmed();
  const int messageIndex = appendMessage(localMessage, MessageStatus::Pending);
  m_pendingMessageIndexesByRequestId.insert(localMessage.requestId, messageIndex);
  emit messageReadyForPersistence(localMessage);

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
  m_pendingMessage = m_inputLine->toPlainText().trimmed();
}

void SessionWindow::requestAttachment(bool imageOnly) {
  const QString conversationId = m_session.conversationId().trimmed();
  if (conversationId.isEmpty()) {
    const QString kind = imageOnly ? QStringLiteral("图片")
                                   : QStringLiteral("文件");
    appendStatusLine(QStringLiteral("缺少 conversation_id，无法发送%1").arg(kind));
    qWarning() << "[SessionWindow] missing conversation_id for attachment"
               << "display_name=" << m_session.displayName()
               << "kind=" << kind;
    return;
  }

  const QString conversationName = m_session.displayName().trimmed();
  if (imageOnly) {
    emit imageAttachmentRequested(conversationId, conversationName);
  } else {
    emit fileAttachmentRequested(conversationId, conversationName);
  }
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

void SessionWindow::refreshPresenceLabel() {
  if (!m_presenceLabel) {
    return;
  }
  if (m_session.type() == Session::Type::Group) {
    m_presenceLabel->setText(QStringLiteral("群聊"));
    return;
  }
  m_presenceLabel->setText(presenceText(m_peerIsOnline, m_peerLastSeenAtUtc));
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
      if (!envelope.requestId.trimmed().isEmpty()) {
        handleMessageSendResponse(envelope);
      }
    }
    return;
  }

  qWarning() << "Protocol parse failed, source:" << sourceTag << "error:"
             << parseError << "payload:" << payload;
}

void SessionWindow::appendPersistedMessage(const ChatMessage &message) {
  if (!message.isValid()) {
    return;
  }
  const int existingIndex = findExistingMessageIndex(message);
  if (existingIndex >= 0) {
    DisplayMessage &displayMessage = m_messages[existingIndex];
    displayMessage.message = message;
    displayMessage.status = isOutgoingMessage(message) ? MessageStatus::Sent
                                                       : MessageStatus::Received;
    updateMessageBubble(existingIndex);
    return;
  }
  appendMessage(message, isOutgoingMessage(message) ? MessageStatus::Sent
                                                    : MessageStatus::Received);
}

int SessionWindow::appendMessage(const ChatMessage &message, MessageStatus status) {
  DisplayMessage displayMessage;
  displayMessage.message = message;
  displayMessage.status = status;
  m_messages.push_back(displayMessage);
  const int index = m_messages.size() - 1;
  m_messages[index].bubbleLabel =
      appendChatBubble(QString(), status != MessageStatus::Received, false);
  updateMessageBubble(index);
  return index;
}

void SessionWindow::updateMessageBubble(int index) {
  if (index < 0 || index >= m_messages.size()) {
    return;
  }

  DisplayMessage &message = m_messages[index];
  if (!message.bubbleLabel) {
    return;
  }

  const QString timeText = formatMessageTime(message.message.sentAt);
  QString bubbleText;
  if (message.status == MessageStatus::Received) {
    const QString sender =
        message.message.senderUsername.trimmed().isEmpty()
            ? QStringLiteral("对方")
            : message.message.senderUsername.trimmed();
    bubbleText = QStringLiteral("%1 %2: %3")
                     .arg(timeText, sender, renderMessageBody(message.message));
  } else {
    bubbleText = QStringLiteral("%1 我: %2 [%3]")
                     .arg(timeText, renderMessageBody(message.message),
                          messageStatusText(message.status));
    if (message.status == MessageStatus::Sent && message.message.seq > 0) {
      bubbleText += QStringLiteral(" (#%1)").arg(message.message.seq);
    }
  }
  message.bubbleLabel->setText(bubbleText);
}

int SessionWindow::findExistingMessageIndex(const ChatMessage &message) const {
  for (int index = 0; index < m_messages.size(); ++index) {
    const ChatMessage &existing = m_messages[index].message;
    if (existing.conversationId.trimmed() != message.conversationId.trimmed()) {
      continue;
    }
    if (message.seq > 0 && existing.seq > 0 && existing.seq == message.seq) {
      return index;
    }
    if (!message.messageId.trimmed().isEmpty() &&
        existing.messageId.trimmed() == message.messageId.trimmed()) {
      return index;
    }
    if (!message.requestId.trimmed().isEmpty() &&
        existing.requestId.trimmed() == message.requestId.trimmed()) {
      return index;
    }
  }
  return -1;
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

  DisplayMessage &message = m_messages[index];
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

  const bool ok = envelope.code == 0 &&
                  (envelope.hasOk ? envelope.ok
                                  : envelope.data.value("ok").toBool(true));
  if (!ok) {
    const QString errorMessage =
        envelope.message.trimmed().isEmpty()
            ? jsonStringValue(envelope.data, "message")
            : envelope.message.trimmed();
    const QString errorText = messageErrorText(envelope.code, errorMessage);
    markPendingMessageFailed(index, errorText);
    appendStatusLine(errorText);
    m_pendingMessageIndexesByRequestId.remove(requestId);
    qWarning() << "[SessionWindow] MESSAGE/SEND failed request_id=" << requestId
               << "code=" << envelope.code << "data="
               << QString::fromUtf8(
                      QJsonDocument(envelope.data).toJson(QJsonDocument::Compact));
    return;
  }

  message.message.conversationId =
      responseConversationId.isEmpty() ? message.message.conversationId
                                       : responseConversationId;
  message.message.messageId = jsonStringValue(envelope.data, "message_id");
  message.message.seq = jsonIntegerValue(envelope.data, "seq");
  const QString sentAt = jsonStringValue(envelope.data, "sent_at");
  if (!sentAt.isEmpty()) {
    message.message.sentAt = sentAt;
  }
  const QString content = jsonStringValue(envelope.data, "content");
  if (!content.isEmpty()) {
    message.message.text = content;
  }
  message.status = MessageStatus::Sent;
  updateMessageBubble(index);
  m_pendingMessageIndexesByRequestId.remove(requestId);
  emit messageReadyForPersistence(message.message);

  qInfo() << "[SessionWindow] MESSAGE/SEND ack request_id=" << requestId
          << "message_id=" << message.message.messageId
          << "seq=" << message.message.seq << "sent_at=" << message.message.sentAt;
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
  incoming.kind = ChatMessageKind::Text;
  incoming.text = jsonStringValue(envelope.data, "content");
  incoming.sentAt = jsonStringValue(envelope.data, "sent_at");
  incoming.senderUserId = jsonStringValue(envelope.data, "from_user_id");
  incoming.senderUsername = jsonStringValue(envelope.data, "from_username");

  if (incoming.text.isEmpty()) {
    qWarning() << "[SessionWindow] ignore incoming MESSAGE/SEND without content "
                  "conversation_id="
               << conversationId;
    return;
  }

  appendMessage(incoming, MessageStatus::Received);
  emit messageReadyForPersistence(incoming);
  qInfo() << "[SessionWindow] received incoming MESSAGE/SEND conversation_id="
          << conversationId << "message_id=" << incoming.messageId
          << "from_user_id=" << incoming.senderUserId;
}

void SessionWindow::markPendingMessageFailed(int index, const QString &reason) {
  if (index < 0 || index >= m_messages.size()) {
    return;
  }

  DisplayMessage &message = m_messages[index];
  message.status = MessageStatus::Failed;
  updateMessageBubble(index);
  qWarning() << "[SessionWindow] pending message failed request_id="
             << message.message.requestId << "reason=" << reason;
}

bool SessionWindow::isOutgoingMessage(const ChatMessage &message) const {
  const QString currentUserId = UserSession::instance().userId().trimmed();
  return !currentUserId.isEmpty() &&
         message.senderUserId.trimmed() == currentUserId;
}

QString SessionWindow::renderMessageBody(const ChatMessage &message) const {
  if (message.kind == ChatMessageKind::File) {
    const QString fileName = message.file.originalName.trimmed();
    return fileName.isEmpty() ? QStringLiteral("[文件]")
                              : QStringLiteral("[文件] %1").arg(fileName);
  }
  return message.text;
}


