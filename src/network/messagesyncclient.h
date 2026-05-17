#ifndef MESSAGESYNCCLIENT_H
#define MESSAGESYNCCLIENT_H

#include "chatmessage.h"
#include "protocol.h"
#include "websocketclient.h"

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QVector>

struct MessagePullResult {
  QString conversationId;
  int conversationType = 0;
  int pulledCount = 0;
  bool hasMore = false;
  qint64 nextAfterSeq = 0;
  qint64 serverLastSeq = 0;
  QVector<ChatMessage> messages;
};
Q_DECLARE_METATYPE(MessagePullResult)

struct MessageAckResult {
  QString conversationId;
  qint64 ackedUpToSeq = 0;
  int affectedCount = 0;
};
Q_DECLARE_METATYPE(MessageAckResult)

class MessageSyncClient : public QObject {
  Q_OBJECT

public:
  explicit MessageSyncClient(
      websocketclient *client = websocketclient::instance(),
      QObject *parent = nullptr);

  QString pullMessages(const QString &conversationId, qint64 afterSeq,
                       int limit = 100);
  QString acknowledgeUpToSeq(const QString &conversationId, qint64 upToSeq,
                             bool delivered = true);

  static bool parseIncomingSendEnvelope(const protocol::Envelope &envelope,
                                        ChatMessage *outMessage,
                                        QString *error = nullptr);
  static bool parsePulledMessageObject(const QJsonObject &messageObject,
                                       ChatMessage *outMessage,
                                       QString *error = nullptr);

signals:
  void pullSucceeded(const QString &requestId, const MessagePullResult &result);
  void ackSucceeded(const QString &requestId, const MessageAckResult &result);
  void requestFailed(const QString &requestId, const QString &action, int code,
                     const QString &error);

private slots:
  void onTextMessageReceived(const QString &message);
  void onDisconnected();

private:
  struct PendingRequest {
    QString action;
    QPointer<QTimer> timer;
  };

  QString generateRequestId() const;
  bool sendMessagePayload(const QString &action, const QString &requestId,
                          const QJsonObject &data);
  void addPendingRequest(const QString &requestId, const QString &action);
  void clearPendingRequest(const QString &requestId);
  void failRequest(const QString &requestId, const QString &action,
                   int code, const QString &errorMessage);

  bool parsePullResult(const protocol::Envelope &envelope,
                       MessagePullResult *outResult,
                       QString *error = nullptr) const;
  bool parseAckResult(const protocol::Envelope &envelope,
                      MessageAckResult *outResult,
                      QString *error = nullptr) const;

private:
  websocketclient *m_client = nullptr;
  QHash<QString, PendingRequest> m_pendingRequests;
  static constexpr int kRequestTimeoutMs = 8 * 1000;
};

#endif // MESSAGESYNCCLIENT_H
