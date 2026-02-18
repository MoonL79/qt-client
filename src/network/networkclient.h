#ifndef NETWORKCLIENT_H
#define NETWORKCLIENT_H

#include <QObject>
#include <QByteArray>
#include <QDebug>
#include <QUrl>
#include <QWebSocket>

class NetworkClient : public QObject {
  Q_OBJECT

public:
  explicit NetworkClient(QObject *parent = nullptr);
  ~NetworkClient();

  void connectToHost(const QString &host, quint16 port);
  void connectToUrl(const QUrl &url);
  void disconnectFromHost();
  void sendData(const QByteArray &data);
  bool isConnected() const;

signals:
  void connected();
  void disconnected();
  void dataReceived(const QByteArray &data);
  void errorOccurred(const QString &errorString);

private slots:
  void onConnected();
  void onDisconnected();
  void onReadyRead();
  void onError(QAbstractSocket::SocketError socketError);
  void onTextMessageReceived(const QString &message);
  void onBinaryMessageReceived(const QByteArray &message);

private:
  QWebSocket *m_socket;
};

#endif // NETWORKCLIENT_H
