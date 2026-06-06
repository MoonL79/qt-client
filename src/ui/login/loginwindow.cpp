#include "loginwindow.h"
#include "authapiclient.h"
#include "protocol.h"
#include "registerwindow.h"
#include "usersession.h"
#include "ui_loginwindow.h"
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QUuid>
#include <QDebug>
#include <QtGlobal>

namespace {
constexpr const char *kWebSocketUrlEnv = "QT_SERVER_WS_URL";
constexpr const char *kWebSocketHostEnv = "QT_SERVER_WS_HOST";
constexpr const char *kWebSocketPortEnv = "QT_SERVER_WS_PORT";
constexpr const char *kDefaultWebSocketHost = "192.168.14.133";
constexpr int kDefaultWebSocketPort = 12345;
constexpr auto kLoginSettingsRelativePath = "login/login.ini";
constexpr auto kLoginUsernameKey = "login/username";
constexpr auto kLoginPasswordKey = "login/password";
constexpr auto kLoginRememberPasswordKey = "login/remember_password";

QString loginSettingsPath() {
  QString basePath =
      QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)
          .trimmed();
  if (basePath.isEmpty()) {
    basePath = QDir::homePath() + QStringLiteral("/.qt-client");
  }
  return QDir(basePath).filePath(QString::fromUtf8(kLoginSettingsRelativePath));
}

bool ensureLoginSettingsDirectory() {
  const QFileInfo info(loginSettingsPath());
  QDir directory = info.dir();
  return directory.exists() || directory.mkpath(QStringLiteral("."));
}

/**
 * @brief 解析并确定WebSocketurl结果。
 * @return 返回解析得到的 URL 对象。
 */
QUrl resolveWebSocketUrl() {
  const QString urlFromEnv = qEnvironmentVariable(kWebSocketUrlEnv).trimmed();
  if (!urlFromEnv.isEmpty()) {
    const QUrl envUrl(urlFromEnv);
    if (envUrl.isValid() && !envUrl.scheme().isEmpty() &&
        !envUrl.host().trimmed().isEmpty()) {
      return envUrl;
    }
  }

  QString host = qEnvironmentVariable(kWebSocketHostEnv).trimmed();
  if (host.isEmpty()) {
    host = QString::fromLatin1(kDefaultWebSocketHost);
  }

  bool ok = false;
  int port = qEnvironmentVariableIntValue(kWebSocketPortEnv, &ok);
  if (!ok || port <= 0 || port > 65535) {
    port = kDefaultWebSocketPort;
  }

  QUrl url;
  url.setScheme("ws");
  url.setHost(host);
  url.setPort(port);
  return url;
}

/**
 * @brief 提取登录错误消息信息。
 * @param envelope 协议封装数据。
 * @return 返回处理后的字符串结果。
 */
QString extractLoginErrorMessage(const protocol::Envelope &envelope) {
  const QJsonObject &data = envelope.data;

  if (data.value("message").isString()) {
    return data.value("message").toString().trimmed();
  }
  if (data.value("error").isString()) {
    return data.value("error").toString().trimmed();
  }
  if (data.value("reason").isString()) {
    return data.value("reason").toString().trimmed();
  }
  if (data.value("detail").isString()) {
    return data.value("detail").toString().trimmed();
  }

  if (data.value("error").isObject()) {
    const QJsonObject errObj = data.value("error").toObject();
    if (errObj.value("message").isString()) {
      return errObj.value("message").toString().trimmed();
    }
    if (errObj.value("detail").isString()) {
      return errObj.value("detail").toString().trimmed();
    }
    return QString::fromUtf8(
               QJsonDocument(errObj).toJson(QJsonDocument::Compact))
        .trimmed();
  }

  if (envelope.hasCode && envelope.code != 0) {
    return QStringLiteral("登录失败，错误码: %1").arg(envelope.code);
  }

  if (data.value("code").isString()) {
    return QStringLiteral("登录失败，错误码: %1").arg(data.value("code").toString());
  }
  if (data.value("code").isDouble()) {
    return QStringLiteral("登录失败，错误码: %1").arg(data.value("code").toInt());
  }

  return QStringLiteral("登录失败，未返回错误详情");
}

/**
 * @brief 判断当前登录响应条件是否满足。
 * @param envelope 协议封装数据。
 * @param pendingRequestId 请求 ID，用于关联本次业务操作。
 * @return 返回条件判断结果，`true` 表示满足，`false` 表示不满足。
 */
bool isCurrentLoginResponse(const protocol::Envelope &envelope,
                            const QString &pendingRequestId) {
  if (pendingRequestId.isEmpty()) {
    return false;
  }

  /**
   * @brief   // Normal case: server echoes current request_id.
   * @param arg1 输入参数 `arg1`。
   * @return 无返回值。
   */
  if (envelope.requestId == pendingRequestId) {
    return true;
  }

  /**
   * @brief   // Some error packets may not carry request_id and only include received_payload.
   * @param arg1 输入参数 `arg1`。
   * @return 无返回值。
   */
  if (!envelope.requestId.isEmpty()) {
    return false;
  }

  const QJsonValue receivedPayload = envelope.data.value("received_payload");
  if (!receivedPayload.isString()) {
    return false;
  }

  protocol::Envelope originalRequest;
  if (!protocol::parseEnvelope(receivedPayload.toString(), &originalRequest)) {
    return false;
  }

  return originalRequest.requestId == pendingRequestId &&
         originalRequest.type == "AUTH" && originalRequest.action == "LOGIN";
}

/**
 * @brief 判断登录成功条件是否满足。
 * @param envelope 协议封装数据。
 * @return 返回条件判断结果，`true` 表示满足，`false` 表示不满足。
 */
bool isLoginSuccess(const protocol::Envelope &envelope) {
  if (envelope.hasCode) {
    return envelope.code == 0;
  }
  // Backward compatibility for old server responses.
  return envelope.data.value("ok").toBool(false);
}

/**
 * @brief 执行toUnsignedString的核心逻辑。
 * @param value 待处理的值。
 * @return 返回处理后的字符串结果。
 */
QString toUnsignedString(const QJsonValue &value) {
  if (value.isString()) {
    const QString s = value.toString().trimmed();
    static const QRegularExpression re(QStringLiteral("^\\d+$"));
    if (re.match(s).hasMatch()) {
      return s;
    }
  }
  if (value.isDouble()) {
    const qint64 v = value.toInteger(-1);
    if (v >= 0) {
      return QString::number(v);
    }
  }
  return QString();
}

/**
 * @brief 提取登录user编号信息。
 * @param envelope 协议封装数据。
 * @return 返回处理后的字符串结果。
 */
QString extractLoginUserId(const protocol::Envelope &envelope) {
  const QJsonObject &data = envelope.data;

  QString userId = toUnsignedString(data.value("user_id"));
  if (!userId.isEmpty()) {
    return userId;
  }
  userId = toUnsignedString(data.value("uid"));
  if (!userId.isEmpty()) {
    return userId;
  }
  userId = toUnsignedString(data.value("id"));
  if (!userId.isEmpty()) {
    return userId;
  }

  if (data.value("user").isObject()) {
    const QJsonObject userObj = data.value("user").toObject();
    userId = toUnsignedString(userObj.value("user_id"));
    if (!userId.isEmpty()) {
      return userId;
    }
    userId = toUnsignedString(userObj.value("uid"));
    if (!userId.isEmpty()) {
      return userId;
    }
    userId = toUnsignedString(userObj.value("id"));
    if (!userId.isEmpty()) {
      return userId;
    }
  }

  return QString();
}

/**
 * @brief 提取登录数字编号信息。
 * @param envelope 协议封装数据。
 * @return 返回处理后的字符串结果。
 */
QString extractLoginNumericId(const protocol::Envelope &envelope) {
  const QJsonObject &data = envelope.data;

  QString numericId = toUnsignedString(data.value("numeric_id"));
  if (!numericId.isEmpty()) {
    return numericId;
  }
  if (data.value("user").isObject()) {
    const QJsonObject userObj = data.value("user").toObject();
    numericId = toUnsignedString(userObj.value("numeric_id"));
    if (!numericId.isEmpty()) {
      return numericId;
    }
  }
  return QString();
}

/**
 * @brief 提取登录username信息。
 * @param envelope 协议封装数据。
 * @param fallback 字符串参数 `fallback`。
 * @return 返回处理后的字符串结果。
 */
QString extractLoginUsername(const protocol::Envelope &envelope,
                             const QString &fallback) {
  const QJsonObject &data = envelope.data;
  if (data.value("username").isString()) {
    const QString username = data.value("username").toString().trimmed();
    if (!username.isEmpty()) {
      return username;
    }
  }
  if (data.value("user").isObject()) {
    const QJsonObject userObj = data.value("user").toObject();
    if (userObj.value("username").isString()) {
      const QString username = userObj.value("username").toString().trimmed();
      if (!username.isEmpty()) {
        return username;
      }
    }
    if (userObj.value("nickname").isString()) {
      const QString nickname = userObj.value("nickname").toString().trimmed();
      if (!nickname.isEmpty()) {
        return nickname;
      }
    }
  }
  return fallback;
}

/**
 * @brief 提取上传令牌信息。
 * @param envelope 协议封装数据。
 * @return 返回处理后的字符串结果。
 */
QString extractUploadToken(const protocol::Envelope &envelope) {
  return envelope.data.value("upload_token").toString().trimmed();
}

/**
 * @brief 提取上传令牌类型信息。
 * @param envelope 协议封装数据。
 * @return 返回处理后的字符串结果。
 */
QString extractUploadTokenType(const protocol::Envelope &envelope) {
  return envelope.data.value("upload_token_type").toString().trimmed();
}

/**
 * @brief 提取上传令牌expiresat信息。
 * @param envelope 协议封装数据。
 * @return 返回处理后的字符串结果。
 */
QString extractUploadTokenExpiresAt(const protocol::Envelope &envelope) {
  return envelope.data.value("upload_token_expires_at").toString().trimmed();
}
}

/**
 * @brief 构造并初始化LoginWindow实例。
 * @param parent 父级对象指针，用于管理当前对象的生命周期。
 * @return 无返回值。
 */
LoginWindow::LoginWindow(QWidget *parent)
    : QWidget(parent), ui(new Ui::LoginWindow), m_isDragging(false) {
  ui->setupUi(this);

  /**
   * @brief 设置窗口标题和大小
   * @param arg1 输入参数 `arg1`。
   * @return 无返回值。
   */
  setWindowTitle("登录 - IM聊天软件");
  setFixedSize(400, 500);

  /**
   * @brief 设置无边框窗口
   * @param arg1 数值参数 `arg1`。
   * @return 无返回值。
   */
  setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
  setAttribute(Qt::WA_TranslucentBackground);

  /**
   * @brief 连接信号槽
   * @param arg1 输入参数 `arg1`。
   * @param arg2 对象参数 `arg2`。
   * @param arg3 输入参数 `arg3`。
   * @param arg4 对象参数 `arg4`。
   * @return 无返回值。
   */
  connect(ui->loginButton, &QPushButton::clicked, this,
          &LoginWindow::onLoginClicked);
  connect(ui->usernameEdit, &QLineEdit::returnPressed, ui->loginButton,
          &QPushButton::click);
  connect(ui->passwordEdit, &QLineEdit::returnPressed, ui->loginButton,
          &QPushButton::click);
  connect(ui->registerButton, &QPushButton::clicked, this,
          &LoginWindow::onRegisterClicked);
  connect(ui->closeButton, &QPushButton::clicked, this,
          &LoginWindow::onCloseClicked);

  auto ws = websocketclient::instance();
  connect(ws, &websocketclient::connected, this,
          &LoginWindow::onWebSocketConnected);
  connect(ws, &websocketclient::textMessageReceived, this,
          &LoginWindow::onWebSocketTextMessage);
  connect(ws, &websocketclient::errorOccurred, this,
          &LoginWindow::onWebSocketError);

  /**
   * @brief 设置密码框为密码模式
   * @param arg1 输入参数 `arg1`。
   * @return 返回 ui->passwordEdit-> 结果。
   */
  ui->passwordEdit->setEchoMode(QLineEdit::Password);
  restoreSavedCredentials();
}

/**
 * @brief 析构 LoginWindow 实例并释放相关资源。
 * @return 无返回值。
 */
LoginWindow::~LoginWindow() { delete ui; }

/**
 * @brief 执行resetLoginForm的核心逻辑。
 * @return 无返回值。
 */
void LoginWindow::resetLoginForm() {
  m_isLoginPending = false;
  m_pendingUsername.clear();
  m_pendingPassword.clear();
  m_pendingLoginRequestId.clear();
  ui->loginButton->setEnabled(true);
  ui->loginButton->setText("登录");
  restoreSavedCredentials();
}

/**
 * @brief 执行paintEvent的核心逻辑。
 * @param event 数值参数 `event`。
 * @return 无返回值。
 */
void LoginWindow::paintEvent(QPaintEvent *event) {
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing);

  /**
   * @brief 绘制圆角矩形背景
   * @param arg1 输入参数 `arg1`。
   * @param arg2 输入参数 `arg2`。
   * @param arg3 输入参数 `arg3`。
   * @return 返回计算得到的数值结果。
   */
  QPainterPath path;
  path.addRoundedRect(rect(), 20, 20);

  painter.fillPath(path, QColor("#f5f5f5"));

  /**
   * @brief 绘制边框（可选）
   * @param arg1 输入参数 `arg1`。
   * @return 返回计算得到的数值结果。
   */
  painter.setPen(QPen(QColor("#d9d9d9"), 1));
  painter.drawPath(path);
}

/**
 * @brief 响应登录点击事件。
 * @return 无返回值。
 */
void LoginWindow::onLoginClicked() {
  QString username = ui->usernameEdit->text().trimmed();
  const QString password = ui->passwordEdit->text();

  if (username.isEmpty()) {
    QMessageBox::warning(this, "输入错误", "用户名不能为空");
    ui->usernameEdit->setFocus();
    return;
  }

  if (password.isEmpty()) {
    QMessageBox::warning(this, "输入错误", "密码不能为空");
    ui->passwordEdit->setFocus();
    return;
  }

  m_pendingUsername = username;
  m_pendingPassword = password;
  m_pendingLoginRequestId.clear();
  m_isLoginPending = true;
  ui->loginButton->setEnabled(false);
  ui->loginButton->setText("连接中...");

  auto ws = websocketclient::instance();
  UserSession::instance().clear();
  qInfo() << "Start login request for user:" << m_pendingUsername;
  if (!ws->isConnected()) {
    const QUrl wsUrl = resolveWebSocketUrl();
    qInfo() << "Open websocket for login, url=" << wsUrl.toString();
    ws->open(wsUrl);
  } else {
    onWebSocketConnected();
  }
}

/**
 * @brief 响应注册点击事件。
 * @return 无返回值。
 */
void LoginWindow::onRegisterClicked() {
  if (!m_registerWindow) {
    m_registerWindow = new RegisterWindow(nullptr);
    connect(m_registerWindow, &RegisterWindow::registerSuccess, this,
            [this](const QString &username) {
              ui->usernameEdit->setText(username.trimmed());
              ui->passwordEdit->clear();
              this->show();
              this->raise();
              this->activateWindow();
            });
    connect(m_registerWindow, &QObject::destroyed, this, [this]() {
      m_registerWindow = nullptr;
      this->show();
      this->raise();
      this->activateWindow();
    });
  }

  this->hide();
  m_registerWindow->show();
  m_registerWindow->raise();
  m_registerWindow->activateWindow();
}

/**
 * @brief 关闭close资源或连接。
 * @return 无返回值。
 */
void LoginWindow::onCloseClicked() { close(); }

/**
 * @brief 响应WebSocket已连接事件。
 * @return 无返回值。
 */
void LoginWindow::onWebSocketConnected() {
  if (!m_isLoginPending)
    return;

  QJsonObject data;
  data.insert("username", m_pendingUsername);
  data.insert("password", m_pendingPassword);
  m_pendingLoginRequestId =
      QUuid::createUuid().toString(QUuid::WithoutBraces);
  const QString payload =
      protocol::createRequest("AUTH", "LOGIN", data, m_pendingLoginRequestId);
  websocketclient::instance()->sendTextMessage(payload);
  qInfo() << "AUTH LOGIN sent, request_id:" << m_pendingLoginRequestId;
  ui->loginButton->setText("登录中...");
}

/**
 * @brief 响应WebSocket文本消息事件。
 * @param message 消息文本或提示信息。
 * @return 无返回值。
 */
void LoginWindow::onWebSocketTextMessage(const QString &message) {
  if (!m_isLoginPending || m_pendingLoginRequestId.isEmpty())
    return;

  protocol::Envelope envelope;
  QString parseError;
  if (!protocol::parseEnvelope(message, &envelope, &parseError)) {
    m_isLoginPending = false;
    m_pendingLoginRequestId.clear();
    m_pendingPassword.clear();
    ui->loginButton->setEnabled(true);
    ui->loginButton->setText("登录");
    QMessageBox::warning(this, "登录失败",
                         QStringLiteral("响应解析失败: %1")
                             .arg(parseError.isEmpty()
                                      ? QStringLiteral("未知协议错误")
                                      : parseError));
    return;
  }

  if (!AuthApiClient::isCurrentLoginResponse(envelope, m_pendingLoginRequestId))
    return;

  const QString responseMessage =
      AuthApiClient::extractAuthErrorMessage(envelope, QStringLiteral("LOGIN"));
  if (envelope.type != "AUTH" || envelope.action != "LOGIN") {
    m_isLoginPending = false;
    m_pendingLoginRequestId.clear();
    m_pendingPassword.clear();
    ui->loginButton->setEnabled(true);
    ui->loginButton->setText("登录");
    QMessageBox::warning(this, "登录失败", responseMessage);
    return;
  }

  m_isLoginPending = false;
  m_pendingLoginRequestId.clear();
  ui->loginButton->setEnabled(true);
  ui->loginButton->setText("登录");

  if (AuthApiClient::isLoginSuccessEnvelope(envelope)) {
    LoginResult loginResult;
    QString loginParseError;
    if (!AuthApiClient::parseLoginResult(envelope, &loginResult, &loginParseError)) {
      m_pendingPassword.clear();
      QMessageBox::warning(this, "登录失败",
                           QStringLiteral("响应解析失败: %1").arg(loginParseError));
      return;
    }

    const QString loginUsername = loginResult.user.username.trimmed().isEmpty()
                                      ? m_pendingUsername
                                      : loginResult.user.username.trimmed();
    const QString userId = loginResult.user.userId;
    const QString numericId = loginResult.user.numericId;
    const QString uploadToken = loginResult.uploadToken;
    const QString uploadTokenType = loginResult.uploadTokenType;
    const QString uploadTokenExpiresAt = loginResult.uploadTokenExpiresAtUtc;

    UserSession::instance().setLoginContext(
        userId, loginUsername, numericId, uploadToken, uploadTokenType,
        uploadTokenExpiresAt, loginResult.presence.isOnline,
        loginResult.presence.lastSeenAtUtc);

    qInfo() << "Login success for user:" << loginUsername << "user_id:" << userId;
    if (userId.isEmpty()) {
      qWarning() << "Login response does not include valid numeric user_id";
    }
    if (uploadToken.isEmpty() || uploadTokenType.isEmpty() ||
        uploadTokenExpiresAt.isEmpty()) {
      qWarning() << "Login response missing upload token fields, user_id:" << userId
                 << "token_type:" << uploadTokenType
                 << "expires_at:" << uploadTokenExpiresAt;
      QMessageBox::warning(this, "登录提示",
                           "登录成功，但上传凭证缺失或不完整，头像上传将不可用。");
    } else if (UserSession::instance().isUploadTokenExpired()) {
      qWarning() << "Upload token already expired or invalid timestamp, user_id:"
                 << userId << "expires_at:" << uploadTokenExpiresAt;
      QMessageBox::warning(this, "登录提示",
                           "登录成功，但上传凭证已过期或时间格式无效，请重新登录。");
    } else {
      qInfo() << "Upload token received for user_id:" << userId
              << "token_type:" << uploadTokenType
              << "expires_at:" << uploadTokenExpiresAt;
    }
    qInfo() << "Presence cached for user_id:" << userId
            << "is_online:" << UserSession::instance().isOnline()
            << "last_seen_at:" << UserSession::instance().lastSeenAtUtc();
    persistSavedCredentials(m_pendingUsername, m_pendingPassword,
                            ui->rememberCheckBox->isChecked());
    m_pendingPassword.clear();
    emit loginSuccess(loginUsername, userId);
    return;
  }

  qWarning() << "Login failed, reason:" << responseMessage;
  m_pendingPassword.clear();
  QMessageBox::warning(this, "登录失败", responseMessage);
}

/**
 * @brief 响应WebSocket错误事件。
 * @param arg1 输入参数 `arg1`。
 * @param message 消息文本或提示信息。
 * @return 无返回值。
 */
void LoginWindow::onWebSocketError(QAbstractSocket::SocketError,
                                   const QString &message) {
  qWarning() << "WebSocket error during login:" << message;
  m_isLoginPending = false;
  m_pendingLoginRequestId.clear();
  m_pendingPassword.clear();
  ui->loginButton->setEnabled(true);
  ui->loginButton->setText("登录");
  QMessageBox::warning(this, "连接失败", message);
}

/**
 * @brief 执行mousePressEvent的核心逻辑。
 * @param event 对象参数 `event`。
 * @return 无返回值。
 */
void LoginWindow::mousePressEvent(QMouseEvent *event) {
  if (event->button() == Qt::LeftButton) {
    m_isDragging = true;
    m_dragPosition =
        event->globalPosition().toPoint() - frameGeometry().topLeft();
    event->accept();
  }
}

/**
 * @brief 执行mouseMoveEvent的核心逻辑。
 * @param event 对象参数 `event`。
 * @return 无返回值。
 */
void LoginWindow::mouseMoveEvent(QMouseEvent *event) {
  if (m_isDragging && (event->buttons() & Qt::LeftButton)) {
    move(event->globalPosition().toPoint() - m_dragPosition);
    event->accept();
  }
}

/**
 * @brief 执行mouseReleaseEvent的核心逻辑。
 * @param event 对象参数 `event`。
 * @return 无返回值。
 */
void LoginWindow::mouseReleaseEvent(QMouseEvent *event) {
  if (event->button() == Qt::LeftButton) {
    m_isDragging = false;
    event->accept();
  }
}

void LoginWindow::restoreSavedCredentials() {
  ui->usernameEdit->clear();
  ui->passwordEdit->clear();
  ui->rememberCheckBox->setChecked(false);

  const QString settingsPath = loginSettingsPath();
  if (!QFileInfo::exists(settingsPath)) {
    ui->usernameEdit->setFocus();
    return;
  }

  QSettings settings(settingsPath, QSettings::IniFormat);
  const QString savedUsername =
      settings.value(QString::fromUtf8(kLoginUsernameKey)).toString().trimmed();
  const bool rememberPassword =
      settings.value(QString::fromUtf8(kLoginRememberPasswordKey), false)
          .toBool();
  const QString savedPassword =
      rememberPassword
          ? settings.value(QString::fromUtf8(kLoginPasswordKey)).toString()
          : QString();

  ui->usernameEdit->setText(savedUsername);
  ui->passwordEdit->setText(savedPassword);
  ui->rememberCheckBox->setChecked(rememberPassword && !savedPassword.isEmpty());

  if (savedUsername.isEmpty()) {
    ui->usernameEdit->setFocus();
  } else if (savedPassword.isEmpty()) {
    ui->passwordEdit->setFocus();
  } else {
    ui->loginButton->setFocus();
  }
}

void LoginWindow::persistSavedCredentials(const QString &username,
                                          const QString &password,
                                          bool rememberPassword) {
  if (!ensureLoginSettingsDirectory()) {
    qWarning() << "Failed to create login settings directory:"
               << QFileInfo(loginSettingsPath()).dir().absolutePath();
    return;
  }

  QSettings settings(loginSettingsPath(), QSettings::IniFormat);
  settings.setValue(QString::fromUtf8(kLoginUsernameKey), username.trimmed());
  settings.setValue(QString::fromUtf8(kLoginRememberPasswordKey),
                    rememberPassword);
  if (rememberPassword) {
    settings.setValue(QString::fromUtf8(kLoginPasswordKey), password);
  } else {
    settings.remove(QString::fromUtf8(kLoginPasswordKey));
  }
  settings.sync();
  if (settings.status() != QSettings::NoError) {
    qWarning() << "Failed to persist login credentials, settings_path="
               << loginSettingsPath() << "status=" << settings.status();
  }
}


