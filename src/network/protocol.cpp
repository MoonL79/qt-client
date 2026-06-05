#include "protocol.h"

#include <QJsonDocument>
#include <QJsonParseError>
#include <QUuid>

namespace protocol {

/**
 * @brief 创建请求对象或数据。
 * @param type 字符串参数 `type`。
 * @param action 字符串参数 `action`。
 * @param data 请求或响应数据对象。
 * @param requestId 请求 ID，用于匹配异步请求与响应。
 * @return 返回处理后的字符串结果。
 */
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

/**
 * @brief 解析envelope并生成内部结果。
 * @param payload 原始载荷字符串。
 * @param outEnvelope 输出参数 `outEnvelope`，用于承接函数处理结果。
 * @param errorMessage 错误信息输出参数。
 * @return 返回本次处理是否成功。
 */
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
  const QJsonValue code = obj.value("code");
  const QJsonValue ok = obj.value("ok");
  const QJsonValue message = obj.value("message");
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
  outEnvelope->hasCode = false;
  outEnvelope->code = 0;
  outEnvelope->hasOk = false;
  outEnvelope->ok = false;
  outEnvelope->message.clear();
  if (code.isDouble()) {
    outEnvelope->code = code.toInt();
    outEnvelope->hasCode = true;
  } else if (code.isString()) {
    bool ok = false;
    const int parsedCode = code.toString().toInt(&ok);
    if (ok) {
      outEnvelope->code = parsedCode;
      outEnvelope->hasCode = true;
    }
  }
  if (ok.isBool()) {
    outEnvelope->ok = ok.toBool();
    outEnvelope->hasOk = true;
  }
  if (message.isString()) {
    outEnvelope->message = message.toString().trimmed();
  }
  outEnvelope->data = data.toObject();
  outEnvelope->isValid = true;
  return true;
}

} // namespace protocol



