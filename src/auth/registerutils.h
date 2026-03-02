#ifndef REGISTERUTILS_H
#define REGISTERUTILS_H

#include <QJsonObject>
#include <QString>

namespace auth {

struct RegisterInput {
  QString username;
  QString email;
  QString password;
  QString nickname;
  QString phone;
  QString avatarUrl;
  QString bio;
};

struct RegisterValidationResult {
  bool ok = false;
  QString errorMessage;
  RegisterInput normalized;
};

RegisterValidationResult validateRegisterInput(const RegisterInput &input);
QJsonObject buildRegisterData(const RegisterInput &normalizedInput);
QString createRegisterRequestPayload(const RegisterInput &normalizedInput,
                                     const QString &requestId = QString(),
                                     QString *outRequestId = nullptr);

} // namespace auth

#endif // REGISTERUTILS_H
