#include "websocketclient.h"
#include <QNetworkProxy>

// 实现 `instance` 的核心逻辑。
websocketclient *websocketclient::instance() {
  static websocketclient instance;
  return &instance;
}

// 实现 `QString` 的核心逻辑。
websocketclient::websocketclient(QObject *parent)
    : QObject(parent),
      m_socket(QString(), QWebSocketProtocol::VersionLatest, this) {
  m_socket.setProxy(QNetworkProxy(QNetworkProxy::NoProxy));
  connect(&m_socket, &QWebSocket::connected, this, &websocketclient::onConnected);
  connect(&m_socket, &QWebSocket::disconnected, this,
          &websocketclient::onDisconnected);
  connect(&m_socket, &QWebSocket::textMessageReceived, this,
          &websocketclient::onTextMessageReceived);
  connect(&m_socket, &QWebSocket::binaryMessageReceived, this,
          &websocketclient::onBinaryMessageReceived);
  connect(&m_socket, &QWebSocket::pong, this, &websocketclient::onPong);
  connect(&m_socket, &QWebSocket::stateChanged, this,
          &websocketclient::onStateChanged);
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
  connect(&m_socket, &QWebSocket::errorOccurred, this,
          &websocketclient::onErrorOccurred);
#else
  connect(&m_socket,
          QOverload<QAbstractSocket::SocketError>::of(&QWebSocket::error), this,
          &websocketclient::onErrorOccurred);
#endif
}

// 打开`open`资源或连接。
void websocketclient::open(const QUrl &url) {
  if (!url.isValid()) {
    emit errorOccurred(QAbstractSocket::SocketError::UnsupportedSocketOperationError,
                       QStringLiteral("Invalid WebSocket URL"));
    return;
  }
  m_url = url;
  m_socket.open(url);
}

// 关闭`close`资源或连接。
void websocketclient::close(QWebSocketProtocol::CloseCode code,
                            const QString &reason) {
  m_socket.close(code, reason);
}

// 发送文本消息数据。
void websocketclient::sendTextMessage(const QString &message) {
  if (!isConnected()) {
    emit errorOccurred(QAbstractSocket::SocketError::OperationError,
                       QStringLiteral("WebSocket is not connected"));
    return;
  }
  m_socket.sendTextMessage(message);
}

// 发送二进制消息数据。
void websocketclient::sendBinaryMessage(const QByteArray &data) {
  if (!isConnected()) {
    emit errorOccurred(QAbstractSocket::SocketError::OperationError,
                       QStringLiteral("WebSocket is not connected"));
    return;
  }
  m_socket.sendBinaryMessage(data);
}

// 判断已连接条件是否满足。
bool websocketclient::isConnected() const {
  return m_socket.state() == QAbstractSocket::ConnectedState;
}

// 实现 `state` 的核心逻辑。
QAbstractSocket::SocketState websocketclient::state() const {
  return m_socket.state();
}

// 实现 `url` 的核心逻辑。
QUrl websocketclient::url() const {
  return m_url;
}

// 响应已连接事件。
void websocketclient::onConnected() {
  emit connected();
}

// 响应已断开事件。
void websocketclient::onDisconnected() {
  emit disconnected();
}

// 响应文本消息接收事件。
void websocketclient::onTextMessageReceived(const QString &message) {
  emit textMessageReceived(message);
}

// 响应二进制消息接收事件。
void websocketclient::onBinaryMessageReceived(const QByteArray &data) {
  emit binaryMessageReceived(data);
}

// 响应错误occurred事件。
void websocketclient::onErrorOccurred(QAbstractSocket::SocketError error) {
  emit errorOccurred(error, m_socket.errorString());
}

// 响应状态changed事件。
void websocketclient::onStateChanged(QAbstractSocket::SocketState state) {
  emit stateChanged(state);
}

// 响应pong事件。
void websocketclient::onPong(quint64 elapsedTime, const QByteArray &payload) {
  emit pongReceived(elapsedTime, payload);
}
