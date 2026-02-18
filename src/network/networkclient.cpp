#include "networkclient.h"
#include <QDebug>

NetworkClient::NetworkClient(QObject *parent)
    : QObject(parent), m_socket(new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this)) {
  connect(m_socket, &QWebSocket::connected, this, &NetworkClient::onConnected);
  connect(m_socket, &QWebSocket::disconnected, this, &NetworkClient::onDisconnected);
  connect(m_socket, &QWebSocket::errorOccurred, this, &NetworkClient::onError);
  connect(m_socket, &QWebSocket::textMessageReceived, this,
          &NetworkClient::onTextMessageReceived);
  connect(m_socket, &QWebSocket::binaryMessageReceived, this,
          &NetworkClient::onBinaryMessageReceived);
}

NetworkClient::~NetworkClient() {
  if (m_socket->state() == QAbstractSocket::ConnectedState) {
    m_socket->disconnectFromHost();
  }
}

void NetworkClient::connectToHost(const QString &host, quint16 port) {
  connectToUrl(QUrl(QString("ws://%1:%2").arg(host).arg(port)));
}

void NetworkClient::connectToUrl(const QUrl &url) {
  if (m_socket->state() == QAbstractSocket::ConnectedState ||
      m_socket->state() == QAbstractSocket::ConnectingState) {
    qWarning() << "Already connected or connecting";
    return;
  }
  qDebug() << "Connecting to" << url;
  m_socket->open(url);
}

void NetworkClient::disconnectFromHost() {
  if (m_socket->state() == QAbstractSocket::ConnectedState) {
    m_socket->disconnectFromHost();
  }
}

void NetworkClient::sendData(const QByteArray &data) {
  if (m_socket->state() != QAbstractSocket::ConnectedState) {
    qWarning() << "Not connected, cannot send data";
    emit errorOccurred("Not connected");
    return;
  }
  qDebug() << "Sending data:" << data;
  m_socket->sendBinaryMessage(data);
}

bool NetworkClient::isConnected() const {
  return m_socket->state() == QAbstractSocket::ConnectedState;
}

void NetworkClient::onConnected() {
  qDebug() << "Connected to server";
  emit connected();
}

void NetworkClient::onDisconnected() {
  qDebug() << "Disconnected from server";
  emit disconnected();
}

void NetworkClient::onReadyRead() {
  Q_UNUSED(data);
}

void NetworkClient::onError(QAbstractSocket::SocketError socketError) {
  Q_UNUSED(socketError);
  QString errorString = m_socket->errorString();
  qWarning() << "Socket error:" << errorString;
  emit errorOccurred(errorString);
}

void NetworkClient::onTextMessageReceived(const QString &message) {
  qDebug() << "Received text websocket message:" << message;
  emit dataReceived(message.toUtf8());
}

void NetworkClient::onBinaryMessageReceived(const QByteArray &message) {
  qDebug() << "Received binary websocket message:" << message;
  emit dataReceived(message);
}
