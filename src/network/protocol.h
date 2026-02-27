#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <QJsonObject>
#include <QString>

namespace protocol {

struct Envelope {
  QString type;
  QString action;
  QString requestId;
  QJsonObject data;
  bool isValid = false;
};

QString createRequest(const QString &type, const QString &action,
                      const QJsonObject &data,
                      const QString &requestId = QString());

bool parseEnvelope(const QString &payload, Envelope *outEnvelope,
                   QString *errorMessage = nullptr);

} // namespace protocol

#endif // PROTOCOL_H
