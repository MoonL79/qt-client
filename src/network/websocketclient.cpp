#include "websocketclient.h"
#include <QNetworkProxy>

/**
 * @brief 执行instance的核心逻辑。
 * @return 返回处理得到的对象指针。
 */
websocketclient *websocketclient::instance() {
  static websocketclient instance;
  return &instance;
}

/**
 * @brief 构造并初始化websocketclient实例。
 * @param parent 父级对象指针，用于管理当前对象的生命周期。
 * @return 无返回值。
 */
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

/**
 * @brief 打开open资源或连接。
 * @param url URL 地址。
 * @return 无返回值。
 */
void websocketclient::open(const QUrl &url) {
  if (!url.isValid()) {
    emit errorOccurred(QAbstractSocket::SocketError::UnsupportedSocketOperationError,
                       QStringLiteral("Invalid WebSocket URL"));
    return;
  }
  m_url = url;
  m_socket.open(url);
}

/**
 * @brief 关闭close资源或连接。
 * @param code 输入参数 `code`。
 * @param reason 字符串参数 `reason`。
 * @return 无返回值。
 */
void websocketclient::close(QWebSocketProtocol::CloseCode code,
                            const QString &reason) {
  m_socket.close(code, reason);
}

/**
 * @brief 发送文本消息数据。
 * @param message 消息文本或提示信息。
 * @return 无返回值。
 */
void websocketclient::sendTextMessage(const QString &message) {
  if (!isConnected()) {
    emit errorOccurred(QAbstractSocket::SocketError::OperationError,
                       QStringLiteral("WebSocket is not connected"));
    return;
  }
  m_socket.sendTextMessage(message);
}

/**
 * @brief 发送二进制消息数据。
 * @param data 输入数据。
 * @return 无返回值。
 */
void websocketclient::sendBinaryMessage(const QByteArray &data) {
  if (!isConnected()) {
    emit errorOccurred(QAbstractSocket::SocketError::OperationError,
                       QStringLiteral("WebSocket is not connected"));
    return;
  }
  m_socket.sendBinaryMessage(data);
}

/**
 * @brief 判断已连接条件是否满足。
 * @return 返回条件判断结果，`true` 表示满足，`false` 表示不满足。
 */
bool websocketclient::isConnected() const {
  return m_socket.state() == QAbstractSocket::ConnectedState;
}

/**
 * @brief 执行state的核心逻辑。
 * @return 返回 QAbstractSocket::SocketState 结果。
 */
QAbstractSocket::SocketState websocketclient::state() const {
  return m_socket.state();
}

/**
 * @brief 执行url的核心逻辑。
 * @return 返回解析得到的 URL 对象。
 */
QUrl websocketclient::url() const {
  return m_url;
}

/**
 * @brief 响应已连接事件。
 * @return 无返回值。
 */
void websocketclient::onConnected() {
  emit connected();
}

/**
 * @brief 响应已断开事件。
 * @return 无返回值。
 */
void websocketclient::onDisconnected() {
  emit disconnected();
}

/**
 * @brief 响应文本消息接收事件。
 * @param message 消息文本或提示信息。
 * @return 无返回值。
 */
void websocketclient::onTextMessageReceived(const QString &message) {
  emit textMessageReceived(message);
}

/**
 * @brief 响应二进制消息接收事件。
 * @param data 输入数据。
 * @return 无返回值。
 */
void websocketclient::onBinaryMessageReceived(const QByteArray &data) {
  emit binaryMessageReceived(data);
}

/**
 * @brief 响应错误occurred事件。
 * @param error 错误信息相关参数。
 * @return 无返回值。
 */
void websocketclient::onErrorOccurred(QAbstractSocket::SocketError error) {
  emit errorOccurred(error, m_socket.errorString());
}

/**
 * @brief 响应状态changed事件。
 * @param state 输入参数 `state`。
 * @return 无返回值。
 */
void websocketclient::onStateChanged(QAbstractSocket::SocketState state) {
  emit stateChanged(state);
}

/**
 * @brief 响应pong事件。
 * @param elapsedTime 时间相关参数。
 * @param payload 原始载荷字符串。
 * @return 无返回值。
 */
void websocketclient::onPong(quint64 elapsedTime, const QByteArray &payload) {
  emit pongReceived(elapsedTime, payload);
}


