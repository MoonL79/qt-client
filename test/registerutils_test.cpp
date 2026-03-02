#include "registerutils.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest/QtTest>

class RegisterUtilsTest : public QObject {
  Q_OBJECT

private slots:
  void invalidUsernameShouldFail();
  void invalidEmailShouldFail();
  void weakPasswordShouldFail();
  void emptyNicknameShouldFail();
  void optionalFieldTooLongShouldFail();
  void buildRegisterPayloadShouldMatchProtocol();
};

void RegisterUtilsTest::invalidUsernameShouldFail() {
  auth::RegisterInput input;
  input.username = "ab";
  input.email = "alice@example.com";
  input.password = "Abcd1234";
  input.nickname = "Alice";

  const auto result = auth::validateRegisterInput(input);
  QVERIFY(!result.ok);
  QVERIFY(result.errorMessage.contains(QStringLiteral("用户名")));
}

void RegisterUtilsTest::invalidEmailShouldFail() {
  auth::RegisterInput input;
  input.username = "alice_01";
  input.email = "bad-email";
  input.password = "Abcd1234";
  input.nickname = "Alice";

  const auto result = auth::validateRegisterInput(input);
  QVERIFY(!result.ok);
  QVERIFY(result.errorMessage.contains(QStringLiteral("邮箱")));
}

void RegisterUtilsTest::weakPasswordShouldFail() {
  auth::RegisterInput input;
  input.username = "alice_01";
  input.email = "alice@example.com";
  input.password = "abcdefgh";
  input.nickname = "Alice";

  const auto result = auth::validateRegisterInput(input);
  QVERIFY(!result.ok);
  QVERIFY(result.errorMessage.contains(QStringLiteral("密码")));
}

void RegisterUtilsTest::emptyNicknameShouldFail() {
  auth::RegisterInput input;
  input.username = "alice_01";
  input.email = "alice@example.com";
  input.password = "Abcd1234";
  input.nickname = "   ";

  const auto result = auth::validateRegisterInput(input);
  QVERIFY(!result.ok);
  QVERIFY(result.errorMessage.contains(QStringLiteral("昵称")));
}

void RegisterUtilsTest::optionalFieldTooLongShouldFail() {
  auth::RegisterInput input;
  input.username = "alice_01";
  input.email = "alice@example.com";
  input.password = "Abcd1234";
  input.nickname = "Alice";
  input.bio = QString(256, 'a');

  const auto result = auth::validateRegisterInput(input);
  QVERIFY(!result.ok);
  QVERIFY(result.errorMessage.contains(QStringLiteral("简介")));
}

void RegisterUtilsTest::buildRegisterPayloadShouldMatchProtocol() {
  auth::RegisterInput input;
  input.username = " alice_01 ";
  input.email = " alice@example.com ";
  input.password = " Abcd1234 ";
  input.nickname = " Alice ";
  input.phone = " 13800138000 ";
  input.avatarUrl = " ";
  input.bio = " hello ";

  const auto validation = auth::validateRegisterInput(input);
  QVERIFY(validation.ok);

  QString requestId;
  const QString payload = auth::createRegisterRequestPayload(
      validation.normalized, QStringLiteral("req-123"), &requestId);
  QCOMPARE(requestId, QStringLiteral("req-123"));

  const QJsonObject obj =
      QJsonDocument::fromJson(payload.toUtf8()).object();
  QCOMPARE(obj.value("type").toString(), QStringLiteral("AUTH"));
  QCOMPARE(obj.value("action").toString(), QStringLiteral("REGISTER"));
  QCOMPARE(obj.value("request_id").toString(), QStringLiteral("req-123"));

  const QJsonObject data = obj.value("data").toObject();
  QCOMPARE(data.value("username").toString(), QStringLiteral("alice_01"));
  QCOMPARE(data.value("email").toString(), QStringLiteral("alice@example.com"));
  QCOMPARE(data.value("password").toString(), QStringLiteral("Abcd1234"));
  QCOMPARE(data.value("nickname").toString(), QStringLiteral("Alice"));
  QCOMPARE(data.value("phone").toString(), QStringLiteral("13800138000"));
  QCOMPARE(data.value("avatar_url").toString(), QStringLiteral(""));
  QCOMPARE(data.value("bio").toString(), QStringLiteral("hello"));
}

QTEST_MAIN(RegisterUtilsTest)
#include "registerutils_test.moc"
