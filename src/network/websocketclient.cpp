#include "websocketclient.h"

websocketclient::websocketclient(QObject *parent)
    : QObject(parent), m_socket(QString(), QWebSocketProtocol::VersionLatest, this) {
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

void websocketclient::open(const QUrl &url) {
  if (!url.isValid()) {
    emit errorOccurred(QAbstractSocket::SocketError::UnsupportedSocketOperationError,
                       QStringLiteral("Invalid WebSocket URL"));
    return;
  }
  m_url = url;
  m_socket.open(url);
}

void websocketclient::close(QWebSocketProtocol::CloseCode code,
                            const QString &reason) {
  m_socket.close(code, reason);
}

void websocketclient::sendTextMessage(const QString &message) {
  if (!isConnected()) {
    emit errorOccurred(QAbstractSocket::SocketError::OperationError,
                       QStringLiteral("WebSocket is not connected"));
    return;
  }
  m_socket.sendTextMessage(message);
}

void websocketclient::sendBinaryMessage(const QByteArray &data) {
  if (!isConnected()) {
    emit errorOccurred(QAbstractSocket::SocketError::OperationError,
                       QStringLiteral("WebSocket is not connected"));
    return;
  }
  m_socket.sendBinaryMessage(data);
}

bool websocketclient::isConnected() const {
  return m_socket.state() == QAbstractSocket::ConnectedState;
}

QAbstractSocket::SocketState websocketclient::state() const {
  return m_socket.state();
}

QUrl websocketclient::url() const {
  return m_url;
}

void websocketclient::onConnected() {
  emit connected();
}

void websocketclient::onDisconnected() {
  emit disconnected();
}

void websocketclient::onTextMessageReceived(const QString &message) {
  emit textMessageReceived(message);
}

void websocketclient::onBinaryMessageReceived(const QByteArray &data) {
  emit binaryMessageReceived(data);
}

void websocketclient::onErrorOccurred(QAbstractSocket::SocketError error) {
  emit errorOccurred(error, m_socket.errorString());
}

void websocketclient::onStateChanged(QAbstractSocket::SocketState state) {
  emit stateChanged(state);
}

void websocketclient::onPong(quint64 elapsedTime, const QByteArray &payload) {
  emit pongReceived(elapsedTime, payload);
}
