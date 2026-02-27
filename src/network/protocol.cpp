#include "protocol.h"

#include <QJsonDocument>
#include <QJsonParseError>
#include <QUuid>

namespace protocol {

QString createRequest(const QString &type, const QString &action,
                      const QJsonObject &data, const QString &requestId) {
  QJsonObject envelope;
  envelope.insert("type", type);
  envelope.insert("action", action);
  envelope.insert("request_id",
                  requestId.isEmpty()
                      ? QUuid::createUuid().toString(QUuid::WithoutBraces)
                      : requestId);
  envelope.insert("data", data);
  return QString::fromUtf8(QJsonDocument(envelope).toJson(QJsonDocument::Compact));
}

bool parseEnvelope(const QString &payload, Envelope *outEnvelope,
                   QString *errorMessage) {
  if (!outEnvelope) {
    return false;
  }

  QJsonParseError parseError;
  const QJsonDocument doc =
      QJsonDocument::fromJson(payload.toUtf8(), &parseError);
  if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
    if (errorMessage) {
      *errorMessage = parseError.errorString();
    }
    return false;
  }

  const QJsonObject obj = doc.object();
  const QJsonValue type = obj.value("type");
  const QJsonValue action = obj.value("action");
  const QJsonValue requestId = obj.value("request_id");
  const QJsonValue data = obj.value("data");

  if (!type.isString() || !action.isString() || !requestId.isString() ||
      !data.isObject()) {
    if (errorMessage) {
      *errorMessage = QStringLiteral("Envelope missing required fields");
    }
    return false;
  }

  outEnvelope->type = type.toString();
  outEnvelope->action = action.toString();
  outEnvelope->requestId = requestId.toString();
  outEnvelope->data = data.toObject();
  outEnvelope->isValid = true;
  return true;
}

} // namespace protocol
