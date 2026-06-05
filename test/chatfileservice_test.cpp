#include <QJsonObject>
#include <QtTest>

#include "authapiclient.h"
#include "chatfileservice.h"

class ChatFileServiceTest : public QObject {
  Q_OBJECT

private slots:
  void parseLoginResult_readsChatFileUploadToken();
  void parseFileMessage_acceptsFileMessage();
  void parseFileMessage_rejectsTextMessage();
};

// 解析登录结果reads聊天文件上传令牌并生成内部结果。
void ChatFileServiceTest::parseLoginResult_readsChatFileUploadToken() {
  protocol::Envelope envelope;
  envelope.type = QStringLiteral("AUTH");
  envelope.action = QStringLiteral("LOGIN");
  envelope.requestId = QStringLiteral("req-login");
  envelope.hasCode = true;
  envelope.code = 0;
  envelope.data = QJsonObject{
      // 实现 `QStringLiteral` 的核心逻辑。
      {QStringLiteral("ok"), true},
      // 实现 `QStringLiteral` 的核心逻辑。
      {QStringLiteral("message"), QStringLiteral("success")},
      // 实现 `QStringLiteral` 的核心逻辑。
      {QStringLiteral("chat_file_upload_token"), QStringLiteral("token-123")},
      // 实现 `QStringLiteral` 的核心逻辑。
      {QStringLiteral("chat_file_upload_token_type"), QStringLiteral("Bearer")},
      // 实现 `QStringLiteral` 的核心逻辑。
      {QStringLiteral("chat_file_upload_token_expires_at"),
       // 实现 `QStringLiteral` 的核心逻辑。
       QStringLiteral("2026-03-24T12:00:00Z")},
      {QStringLiteral("user"),
       // 实现 `QStringLiteral` 的核心逻辑。
       QJsonObject{{QStringLiteral("user_id"), QStringLiteral("1")},
                   {QStringLiteral("numeric_id"), QStringLiteral("10001")},
                   {QStringLiteral("username"), QStringLiteral("alice")},
                   {QStringLiteral("email"), QStringLiteral("a@example.com")},
                   {QStringLiteral("phone"), QStringLiteral("13800138000")},
                   {QStringLiteral("status"), 1},
                   {QStringLiteral("user_uuid"), QStringLiteral("uuid-1")},
                   {QStringLiteral("nickname"), QStringLiteral("Alice")},
                   {QStringLiteral("avatar_url"), QStringLiteral("/a.png")},
                   {QStringLiteral("bio"), QStringLiteral("hello")}}}};

  LoginResult result;
  QString error;
  QVERIFY2(AuthApiClient::parseLoginResult(envelope, &result, &error),
           qPrintable(error));
  QCOMPARE(result.uploadToken, QStringLiteral("token-123"));
  QCOMPARE(result.uploadTokenType, QStringLiteral("Bearer"));
  QCOMPARE(result.uploadTokenExpiresAtUtc,
           QStringLiteral("2026-03-24T12:00:00Z"));
}

// 解析文件消息accepts文件消息并生成内部结果。
void ChatFileServiceTest::parseFileMessage_acceptsFileMessage() {
  ChatFileService service(nullptr);
  protocol::Envelope envelope;
  envelope.type = QStringLiteral("MESSAGE");
  envelope.action = QStringLiteral("SEND");
  envelope.data = QJsonObject{
      // 实现 `QStringLiteral` 的核心逻辑。
      {QStringLiteral("conversation_id"), QStringLiteral("c1")},
      // 实现 `QStringLiteral` 的核心逻辑。
      {QStringLiteral("message_id"), QStringLiteral("m1")},
      // 实现 `QStringLiteral` 的核心逻辑。
      {QStringLiteral("seq"), 7},
      // 实现 `QStringLiteral` 的核心逻辑。
      {QStringLiteral("sent_at"), QStringLiteral("2026-03-23T12:00:00Z")},
      // 实现 `QStringLiteral` 的核心逻辑。
      {QStringLiteral("from_user_id"), QStringLiteral("1")},
      // 实现 `QStringLiteral` 的核心逻辑。
      {QStringLiteral("from_numeric_id"), QStringLiteral("10001")},
      // 实现 `QStringLiteral` 的核心逻辑。
      {QStringLiteral("from_username"), QStringLiteral("alice")},
      // 实现 `QStringLiteral` 的核心逻辑。
      {QStringLiteral("message_kind"), QStringLiteral("file")},
      // 实现 `QStringLiteral` 的核心逻辑。
      {QStringLiteral("file"),
       // 实现 `QStringLiteral` 的核心逻辑。
       QJsonObject{{QStringLiteral("file_id"), QStringLiteral("f1")},
                   {QStringLiteral("original_name"), QStringLiteral("demo.txt")},
                   {QStringLiteral("stored_name"), QStringLiteral("uuid-demo.txt")},
                   {QStringLiteral("size_bytes"), 128},
                   {QStringLiteral("content_type"), QStringLiteral("text/plain")},
                   {QStringLiteral("sha256"), QStringLiteral("abc")}}}};

  ChatMessage message;
  QString error;
  QVERIFY2(service.parseFileMessage(envelope, &message, &error),
           qPrintable(error));
  QCOMPARE(message.kind, ChatMessageKind::File);
  QCOMPARE(message.file.fileId, QStringLiteral("f1"));
  QCOMPARE(message.file.originalName, QStringLiteral("demo.txt"));
}

// 解析文件消息rejects文本消息并生成内部结果。
void ChatFileServiceTest::parseFileMessage_rejectsTextMessage() {
  ChatFileService service(nullptr);
  protocol::Envelope envelope;
  envelope.type = QStringLiteral("MESSAGE");
  envelope.action = QStringLiteral("SEND");
  envelope.data = QJsonObject{{QStringLiteral("conversation_id"), QStringLiteral("c1")},
                              // 实现 `QStringLiteral` 的核心逻辑。
                              {QStringLiteral("message_kind"),
                               // 实现 `QStringLiteral` 的核心逻辑。
                               QStringLiteral("text")},
                              {QStringLiteral("content"), QStringLiteral("hello")}};

  ChatMessage message;
  QString error;
  QVERIFY(!service.parseFileMessage(envelope, &message, &error));
  QVERIFY(!error.isEmpty());
}

QTEST_MAIN(ChatFileServiceTest)

#include "chatfileservice_test.moc"