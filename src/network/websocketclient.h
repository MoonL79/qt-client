#ifndef WEBSOCKETCLIENT_H
#define WEBSOCKETCLIENT_H

#include <QObject>
#include <QAbstractSocket>
#include <QUrl>
#include <QtWebSockets/QWebSocket>

class websocketclient : public QObject
{
    Q_OBJECT
public:
    explicit websocketclient(QObject *parent = nullptr);
    ~websocketclient() override = default;

public:
    void open(const QUrl &url);
    void close(QWebSocketProtocol::CloseCode code = QWebSocketProtocol::CloseCodeNormal,
               const QString &reason = QString());
    void sendTextMessage(const QString &message);
    void sendBinaryMessage(const QByteArray &data);
    bool isConnected() const;
    QAbstractSocket::SocketState state() const;
    QUrl url() const;

signals:
    void connected();
    void disconnected();
    void textMessageReceived(const QString &message);
    void binaryMessageReceived(const QByteArray &data);
    void errorOccurred(QAbstractSocket::SocketError error, const QString &message);
    void stateChanged(QAbstractSocket::SocketState state);
    void pongReceived(quint64 elapsedTime, const QByteArray &payload);

private slots:
    void onConnected();
    void onDisconnected();
    void onTextMessageReceived(const QString &message);
    void onBinaryMessageReceived(const QByteArray &data);
    void onErrorOccurred(QAbstractSocket::SocketError error);
    void onStateChanged(QAbstractSocket::SocketState state);
    void onPong(quint64 elapsedTime, const QByteArray &payload);

private:
    QWebSocket m_socket;
    QUrl m_url;
};

#endif // WEBSOCKETCLIENT_H
