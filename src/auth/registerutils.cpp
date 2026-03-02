#include "registerutils.h"

#include "protocol.h"

#include <QRegularExpression>
#include <QUuid>

namespace auth {
namespace {

bool hasRequiredPasswordComplexity(const QString &password) {
  bool hasUpper = false;
  bool hasLower = false;
  bool hasDigit = false;

  for (const QChar ch : password) {
    if (ch.isUpper()) {
      hasUpper = true;
    } else if (ch.isLower()) {
      hasLower = true;
    } else if (ch.isDigit()) {
      hasDigit = true;
    }
  }
  return hasUpper && hasLower && hasDigit;
}

} // namespace

RegisterValidationResult validateRegisterInput(const RegisterInput &input) {
  RegisterValidationResult result;
  result.normalized.username = input.username.trimmed();
  result.normalized.email = input.email.trimmed();
  result.normalized.password = input.password.trimmed();
  result.normalized.nickname = input.nickname.trimmed();
  result.normalized.phone = input.phone.trimmed();
  result.normalized.avatarUrl = input.avatarUrl.trimmed();
  result.normalized.bio = input.bio.trimmed();

  static const QRegularExpression usernameRegex(
      QStringLiteral("^[A-Za-z0-9_]{3,32}$"));
  if (!usernameRegex.match(result.normalized.username).hasMatch()) {
    result.errorMessage =
        QStringLiteral("用户名需为 3~32 位，仅允许字母、数字、下划线");
    return result;
  }

  if (result.normalized.email.isEmpty() || result.normalized.email.size() > 128) {
    result.errorMessage = QStringLiteral("邮箱不能为空且长度不能超过 128");
    return result;
  }
  static const QRegularExpression emailRegex(
      QStringLiteral("^[^\\s@]+@[^\\s@]+\\.[^\\s@]+$"));
  if (!emailRegex.match(result.normalized.email).hasMatch()) {
    result.errorMessage = QStringLiteral("邮箱格式不合法");
    return result;
  }

  const int passwordLength = result.normalized.password.size();
  if (passwordLength < 8 || passwordLength > 64) {
    result.errorMessage = QStringLiteral("密码长度需为 8~64");
    return result;
  }
  if (!hasRequiredPasswordComplexity(result.normalized.password)) {
    result.errorMessage = QStringLiteral("密码必须包含大写字母、小写字母和数字");
    return result;
  }

  if (result.normalized.nickname.isEmpty() || result.normalized.nickname.size() > 64) {
    result.errorMessage = QStringLiteral("昵称不能为空且长度不能超过 64");
    return result;
  }

  if (result.normalized.phone.size() > 32) {
    result.errorMessage = QStringLiteral("手机号长度不能超过 32");
    return result;
  }
  if (result.normalized.avatarUrl.size() > 255) {
    result.errorMessage = QStringLiteral("头像 URL 长度不能超过 255");
    return result;
  }
  if (result.normalized.bio.size() > 255) {
    result.errorMessage = QStringLiteral("个人简介长度不能超过 255");
    return result;
  }

  result.ok = true;
  return result;
}

QJsonObject buildRegisterData(const RegisterInput &normalizedInput) {
  QJsonObject data;
  data.insert("username", normalizedInput.username);
  data.insert("email", normalizedInput.email);
  data.insert("password", normalizedInput.password);
  data.insert("nickname", normalizedInput.nickname);
  data.insert("phone", normalizedInput.phone);
  data.insert("avatar_url", normalizedInput.avatarUrl);
  data.insert("bio", normalizedInput.bio);
  return data;
}

QString createRegisterRequestPayload(const RegisterInput &normalizedInput,
                                     const QString &requestId,
                                     QString *outRequestId) {
  const QString effectiveRequestId =
      requestId.isEmpty() ? QUuid::createUuid().toString(QUuid::WithoutBraces)
                          : requestId;
  if (outRequestId) {
    *outRequestId = effectiveRequestId;
  }
  return protocol::createRequest("AUTH", "REGISTER",
                                 buildRegisterData(normalizedInput),
                                 effectiveRequestId);
}

} // namespace auth
