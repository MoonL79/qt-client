#ifndef CHATFILESERVICE_H
#define CHATFILESERVICE_H

#include <QHash>
#include <QNetworkAccessManager>
#include <QPointer>

#include "chatmessage.h"
#include "protocol.h"
#include "websocketclient.h"

class QNetworkReply;

class ChatFileService : public QObject {
  Q_OBJECT

public:
  explicit ChatFileService(websocketclient *client = websocketclient::instance(),
                           QObject *parent = nullptr);

  QString uploadFile(const QString &conversationId, const QString &localFilePath,
                     const QString &currentUserId, const QString &token = QString(),
                     const QString &tokenType = QString());
  QString sendFileMessage(const QString &conversationId,
                          const ChatFileUploadResult &uploadResult);
  QString downloadFile(const QString &fileId, const QString &savePath,
                       const QString &token = QString(),
                       const QString &tokenType = QString());

  bool handleIncomingMessage(const QString &payload, ChatMessage *outMessage = nullptr,
                             QString *error = nullptr);
  bool handleIncomingMessage(const protocol::Envelope &envelope,
                             ChatMessage *outMessage = nullptr,
                             QString *error = nullptr);
  bool canHandleMessage(const QString &payload) const;
  bool canHandleMessage(const protocol::Envelope &envelope) const;
  bool parseFileMessage(const QString &payload, ChatMessage *outMessage,
                        QString *error = nullptr) const;
  bool parseFileMessage(const protocol::Envelope &envelope, ChatMessage *outMessage,
                        QString *error = nullptr) const;

  QVector<ChatMessage> messagesForConversation(const QString &conversationId) const;

signals:
  void uploadProgress(const QString &requestId, qint64 bytesSent, qint64 bytesTotal);
  void uploadFinished(const QString &requestId, const ChatFileUploadResult &result);
  void fileMessageSendSucceeded(const QString &requestId, const ChatMessage &message);
  void fileMessageReceived(const ChatMessage &message);
  void downloadProgress(const QString &requestId, qint64 bytesReceived,
                        qint64 bytesTotal);
  void downloadFinished(const QString &requestId, const ChatFileDownloadTask &task);
  void requestFailed(const QString &requestId, const QString &action, int code,
                     const QString &error);

private slots:
  void onTextMessageReceived(const QString &message);
  void onDisconnected();

private:
  struct PendingUpload {
    QString action;
    QString filePath;
    QPointer<QNetworkReply> reply;
  };

  struct PendingSend {
    QString action;
    QString conversationId;
    ChatFileUploadResult uploadResult;
  };

  struct PendingDownload {
    QString action;
    ChatFileDownloadTask task;
    QPointer<QNetworkReply> reply;
  };

  QString normalizeTokenType(const QString &tokenType) const;
  QString resolveAuthorizationHeader(const QString &token,
                                     const QString &tokenType) const;
  QUrl buildUploadUrl() const;
  QUrl buildDownloadUrl(const QString &fileId) const;
  bool validateSessionToken(QString *error) const;
  void failRequest(const QString &requestId, const QString &action, int code,
                   const QString &error);
  void cacheMessage(const ChatMessage &message);

  ChatFileUploadResult parseUploadResponse(const QString &requestId, int httpCode,
                                           const QByteArray &body,
                                           QString *error) const;
  bool parseDownloadResponse(const QString &requestId, const QString &fileId,
                             const QString &savePath, QNetworkReply *reply,
                             QString *error);

private:
  websocketclient *m_client = nullptr;
  QNetworkAccessManager m_networkManager;
  QHash<QString, PendingUpload> m_pendingUploads;
  QHash<QString, PendingSend> m_pendingSends;
  QHash<QString, PendingDownload> m_pendingDownloads;
  QHash<QString, QVector<ChatMessage>> m_messagesByConversation;
};

#endif // CHATFILESERVICE_H