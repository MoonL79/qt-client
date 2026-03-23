#ifndef CHATMESSAGE_H
#define CHATMESSAGE_H

#include <QMetaType>
#include <QVector>

#include <QString>

enum class ChatMessageKind { Text, File };
Q_DECLARE_METATYPE(ChatMessageKind)

struct LocalChatFileDescriptor {
  QString conversationId;
  QString localFilePath;
  QString currentUserId;
  QString originalName;
  QString contentType;
  qint64 sizeBytes = 0;

  bool isValid() const {
    return !conversationId.trimmed().isEmpty() && !localFilePath.trimmed().isEmpty() &&
           !currentUserId.trimmed().isEmpty();
  }
};
Q_DECLARE_METATYPE(LocalChatFileDescriptor)

struct ChatFileUploadResult {
  bool ok = false;
  int code = -1;
  QString message;
  QString requestId;
  QString conversationId;
  QString fileId;
  QString originalName;
  QString storedName;
  qint64 sizeBytes = 0;
  QString contentType;
  QString sha256;

  bool hasRequiredFields() const {
    return ok && !conversationId.trimmed().isEmpty() && !fileId.trimmed().isEmpty() &&
           !originalName.trimmed().isEmpty() && sizeBytes >= 0;
  }
};
Q_DECLARE_METATYPE(ChatFileUploadResult)

struct ChatFileContent {
  QString fileId;
  QString originalName;
  QString storedName;
  qint64 sizeBytes = 0;
  QString contentType;
  QString sha256;

  bool isValid() const {
    return !fileId.trimmed().isEmpty() && !originalName.trimmed().isEmpty() &&
           sizeBytes >= 0;
  }
};
Q_DECLARE_METATYPE(ChatFileContent)

struct ChatFileDownloadTask {
  QString requestId;
  QString fileId;
  QString savePath;
  QString token;
  QString tokenType;
  qint64 bytesReceived = 0;
  qint64 bytesTotal = 0;

  bool isValid() const {
    return !fileId.trimmed().isEmpty() && !savePath.trimmed().isEmpty() &&
           !token.trimmed().isEmpty();
  }
};
Q_DECLARE_METATYPE(ChatFileDownloadTask)

struct ChatMessage {
  QString localId;
  QString requestId;
  QString conversationId;
  QString messageId;
  qint64 seq = 0;
  QString sentAt;
  QString senderUserId;
  QString senderNumericId;
  QString senderUsername;
  ChatMessageKind kind = ChatMessageKind::Text;
  QString text;
  ChatFileContent file;

  bool isValid() const {
    if (conversationId.trimmed().isEmpty()) {
      return false;
    }
    if (kind == ChatMessageKind::Text) {
      return !text.trimmed().isEmpty();
    }
    return file.isValid();
  }
};
Q_DECLARE_METATYPE(ChatMessage)
Q_DECLARE_METATYPE(QVector<ChatMessage>)

#endif // CHATMESSAGE_H
