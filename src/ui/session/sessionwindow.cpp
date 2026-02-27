#include "sessionwindow.h"
#include "protocol.h"
#include <QAbstractSocket>
#include <QDateTime>
#include <QDebug>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextCursor>

SessionWindow::SessionWindow(const Session &session, QWidget *parent)
    : QWidget(parent), m_session(session), m_isDragging(false),
      m_resizeDir(None), m_receiveBox(nullptr), m_inputLine(nullptr),
      m_sendBtn(nullptr), m_statusLabel(nullptr), m_testBtn(nullptr),
      m_websocket(websocketclient::instance()) {
  setAttribute(Qt::WA_DeleteOnClose);
  setMouseTracking(true); // Enable mouse tracking for resize cursor feedback
  initUI();
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
  headerLayout->setContentsMargins(15, 0, 10, 0);

  // 标题文本 (居中)
  QLabel *titleLabel = new QLabel(m_session.displayName(), header);
  titleLabel->setStyleSheet("font-size: 16px; font-weight: bold; color: #333;");
  titleLabel->setAlignment(Qt::AlignCenter);

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
  headerLayout->addWidget(titleLabel);
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

  m_receiveBox = new QTextEdit(contentArea);
  m_receiveBox->setPlaceholderText("接收服务器响应");
  m_receiveBox->setReadOnly(true);
  m_receiveBox->setStyleSheet("background-color: #000000; border: 1px solid "
                              "#dcdcdc; border-radius: 4px;");
  contentLayout->addWidget(m_receiveBox);

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
}

void SessionWindow::sendPendingMessage() {
  if (!m_inputLine || !m_receiveBox)
    return;

  QString message = m_pendingMessage;
  if (message.isEmpty())
    message = m_inputLine->text().trimmed();
  if (message.isEmpty())
    return;

  m_pendingMessage.clear();
  const QString line =
      QDateTime::currentDateTime().toString("HH:mm:ss ") + "发送: " + message;
  m_receiveBox->append(line);

  QJsonObject data;
  data.insert("conversation_id", m_session.id());
  data.insert("content", message);

  const QString payload =
      protocol::createRequest("MESSAGE", "SEND", data);
  qInfo() << "MESSAGE SEND, conversation_id:" << m_session.id();
  m_websocket->sendTextMessage(payload);
  m_inputLine->clear();
}

void SessionWindow::onSendClicked() {
  if (!m_inputLine)
    return;
  m_pendingMessage = m_inputLine->text().trimmed();
}

void SessionWindow::appendStatusLine(const QString &message) {
  if (!m_receiveBox)
    return;
  const QString line =
      QDateTime::currentDateTime().toString("HH:mm:ss ") + message;
  m_receiveBox->append(line);
  qInfo() << "Session status:" << message;
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
  if (!m_receiveBox)
    return;

  protocol::Envelope envelope;
  QString parseError;
  if (protocol::parseEnvelope(payload, &envelope, &parseError)) {
    QString content;
    if (envelope.data.value("echo").isObject()) {
      const QJsonObject echo = envelope.data.value("echo").toObject();
      if (echo.value("content").isString()) {
        content = echo.value("content").toString();
      } else {
        content =
            QString::fromUtf8(QJsonDocument(echo).toJson(QJsonDocument::Compact));
      }
    } else if (envelope.data.value("content").isString()) {
      content = envelope.data.value("content").toString();
    } else {
      content = QString::fromUtf8(
          QJsonDocument(envelope.data).toJson(QJsonDocument::Compact));
    }

    QString statusText;
    if (envelope.data.value("ok").isBool()) {
      statusText = envelope.data.value("ok").toBool() ? "ok=true" : "ok=false";
    }
    if (envelope.data.value("message").isString()) {
      const QString msg = envelope.data.value("message").toString();
      statusText = statusText.isEmpty() ? msg : statusText + " " + msg;
    }

    const QString display = statusText.isEmpty() ? content : statusText + " " + content;
    const QString line = QDateTime::currentDateTime().toString("HH:mm:ss ") +
                         QString("[%1/%2] %3")
                             .arg(envelope.type, envelope.action, display.trimmed());
    m_receiveBox->append(line);
    return;
  }

  const QString line = QDateTime::currentDateTime().toString("HH:mm:ss ") +
                       QString("%1原始数据: %2")
                           .arg(sourceTag, payload);
  m_receiveBox->append(line);
  qWarning() << "Protocol parse failed, source:" << sourceTag << "error:"
             << parseError << "payload:" << payload;
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
