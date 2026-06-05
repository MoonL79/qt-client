#include "sessionwindow.h"
#include "protocol.h"
#include <QAbstractSocket>
#include <QColor>
#include <QDateTime>
#include <QDebug>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QScrollBar>
#include <QSizePolicy>
#include <QTimer>
#include <QtGlobal>
#include <QUrl>
#include <QUuid>

namespace {
constexpr int kDefaultSessionWindowWidth = 860;
constexpr int kDefaultSessionWindowHeight = 780;
constexpr int kMinimumSessionWindowWidth = 780;
constexpr int kMinimumSessionWindowHeight = 620;
constexpr int kMessageContentMaximumWidth = 520;
constexpr int kFileCardMinimumWidth = 320;
constexpr int kMessageAvatarSide = 36;
constexpr int kDefaultStaticPort = 18080;
constexpr const char *kStaticPortEnv = "QT_SERVER_STATIC_PORT";
constexpr const char *kStaticHostEnv = "QT_SERVER_STATIC_HOST";
constexpr const char *kWebSocketHostEnv = "QT_SERVER_WS_HOST";
constexpr const char *kDefaultServerHost = "192.168.14.133";

/**
 * @brief 判断loopbackhost条件是否满足。
 * @param host 主机地址字符串。
 * @return 返回条件判断结果，`true` 表示满足，`false` 表示不满足。
 */
bool isLoopbackHost(const QString &host) {
  const QString lower = host.trimmed().toLower();
  return lower == "127.0.0.1" || lower == "localhost" || lower == "::1";
}

/**
 * @brief 解析并确定serverhost结果。
 * @return 返回处理后的字符串结果。
 */
QString resolveServerHost() {
  QString host = qEnvironmentVariable(kStaticHostEnv).trimmed();
  if (host.isEmpty()) {
    const QUrl wsUrl = websocketclient::instance()->url();
    if (wsUrl.isValid() && !wsUrl.host().trimmed().isEmpty()) {
      host = wsUrl.host().trimmed();
    }
  }
  if (host.isEmpty()) {
    host = qEnvironmentVariable(kWebSocketHostEnv).trimmed();
  }
  if (host.isEmpty()) {
    host = QString::fromLatin1(kDefaultServerHost);
  }
  return host;
}

/**
 * @brief 解析并确定头像url结果。
 * @param avatarSource 头像地址或头像来源。
 * @return 返回解析得到的 URL 对象。
 */
QUrl resolveAvatarUrl(const QString &avatarSource) {
  const QString trimmed = avatarSource.trimmed();
  if (trimmed.isEmpty()) {
    return QUrl();
  }

  if (trimmed.startsWith(QStringLiteral("http://")) ||
      trimmed.startsWith(QStringLiteral("https://"))) {
    QUrl absolute(trimmed);
    if (!absolute.isValid()) {
      return QUrl();
    }
    if (isLoopbackHost(absolute.host())) {
      absolute.setHost(resolveServerHost());
    }
    return absolute;
  }

  QString staticPath = trimmed;
  if (staticPath.startsWith(QStringLiteral("/static/"))) {
    /**
     * @brief Use as-is.
     * @param arg1 字符串参数 `arg1`。
     * @return 返回 } else 结果。
     */
  } else if (staticPath.startsWith(QStringLiteral("static/"))) {
    staticPath.prepend(QLatin1Char('/'));
  } else {
    return QUrl();
  }

  bool ok = false;
  int staticPort = qEnvironmentVariableIntValue(kStaticPortEnv, &ok);
  if (!ok || staticPort <= 0 || staticPort > 65535) {
    staticPort = kDefaultStaticPort;
  }

  QUrl url;
  url.setScheme(QStringLiteral("http"));
  url.setHost(resolveServerHost());
  url.setPort(staticPort);
  url.setPath(staticPath);
  return url;
}

/**
 * @brief 生成在线状态显示文本。
 * @param isOnline 在线状态标记。
 * @param lastSeenAtUtc 最近在线时间字符串。
 * @return 返回处理后的字符串结果。
 */
QString presenceText(bool isOnline, const QString &lastSeenAtUtc) {
  if (isOnline) {
    return QStringLiteral("在线");
  }
  const QString trimmed = lastSeenAtUtc.trimmed();
  if (trimmed.isEmpty()) {
    return QStringLiteral("离线");
  }
  return QStringLiteral("离线 · 最近在线 %1").arg(trimmed);
}

/**
 * @brief 执行jsonStringValue的核心逻辑。
 * @param obj 输入的对象数据。
 * @param key 对象参数 `key`。
 * @return 返回处理后的字符串结果。
 */
QString jsonStringValue(const QJsonObject &obj, const char *key) {
  const QJsonValue value = obj.value(QLatin1String(key));
  if (value.isString()) {
    return value.toString().trimmed();
  }
  if (value.isDouble()) {
    return QString::number(static_cast<qint64>(value.toDouble()));
  }
  return QString();
}

qint64 jsonIntegerValue(const QJsonObject &obj, const char *key,
                        qint64 defaultValue = 0) {
  const QJsonValue value = obj.value(QLatin1String(key));
  if (value.isDouble()) {
    return static_cast<qint64>(value.toDouble(defaultValue));
  }
  if (value.isString()) {
    bool ok = false;
    const qint64 parsed = value.toString().trimmed().toLongLong(&ok);
    return ok ? parsed : defaultValue;
  }
  return defaultValue;
}

/**
 * @brief 执行formatMessageTime的核心逻辑。
 * @param utcIsoTime 时间相关参数。
 * @return 返回处理后的字符串结果。
 */
QString formatMessageTime(const QString &utcIsoTime) {
  const QString trimmed = utcIsoTime.trimmed();
  if (trimmed.isEmpty()) {
    return QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));
  }

  const QDateTime parsed = QDateTime::fromString(trimmed, Qt::ISODate);
  if (!parsed.isValid()) {
    return QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));
  }
  return parsed.toLocalTime().toString(QStringLiteral("HH:mm:ss"));
}

/**
 * @brief 生成消息状态显示文本。
 * @param status 状态值。
 * @return 返回处理后的字符串结果。
 */
QString messageStatusText(SessionWindow::MessageStatus status) {
  switch (status) {
  case SessionWindow::MessageStatus::Pending:
    return QStringLiteral("发送中");
  case SessionWindow::MessageStatus::Sent:
    return QStringLiteral("已发送");
  case SessionWindow::MessageStatus::Failed:
    return QStringLiteral("发送失败");
  case SessionWindow::MessageStatus::Received:
    return QStringLiteral("已接收");
  }
  return QStringLiteral("未知状态");
}

/**
 * @brief 生成消息错误显示文本。
 * @param code 数值参数 `code`。
 * @param fallback 字符串参数 `fallback`。
 * @return 返回处理后的字符串结果。
 */
QString messageErrorText(int code, const QString &fallback) {
  switch (code) {
  case 2001:
    return QStringLiteral("发送失败：未登录");
  case 2005:
    return QStringLiteral("发送失败：不是会话成员或已被禁言");
  case 4001:
    return QStringLiteral("发送失败：消息参数非法");
  case 4002:
    return QStringLiteral("发送失败：消息过大");
  case 4005:
    return QStringLiteral("发送失败：会话不存在");
  case 1099:
    return QStringLiteral("发送失败：服务端内部错误");
  default:
    break;
  }

  const QString trimmed = fallback.trimmed();
  if (!trimmed.isEmpty()) {
    return QStringLiteral("发送失败：%1").arg(trimmed);
  }
  return QStringLiteral("发送失败：未知错误(%1)").arg(code);
}

/**
 * @brief 执行humanReadableFileSize的核心逻辑。
 * @param sizeBytes 数值参数 `sizeBytes`。
 * @return 返回处理后的字符串结果。
 */
QString humanReadableFileSize(qint64 sizeBytes) {
  if (sizeBytes < 0) {
    return QStringLiteral("未知大小");
  }

  static const char *kUnits[] = {"B", "KB", "MB", "GB"};
  double size = static_cast<double>(sizeBytes);
  int unitIndex = 0;
  while (size >= 1024.0 && unitIndex < 3) {
    size /= 1024.0;
    ++unitIndex;
  }

  const int precision = unitIndex == 0 ? 0 : (size < 10.0 ? 1 : 0);
  return QStringLiteral("%1 %2")
      .arg(QString::number(size, 'f', precision),
           QString::fromLatin1(kUnits[unitIndex]));
}

QString avatarInitial(const QString &displayName,
                      const QString &fallback = QStringLiteral("?")) {
  const QString trimmed = displayName.trimmed();
  if (trimmed.isEmpty()) {
    return fallback;
  }

  const QString initial = trimmed.left(1);
  const QChar firstChar = initial.front();
  if (firstChar.isLetter() && firstChar.unicode() < 128) {
    return initial.toUpper();
  }
  return initial;
}

/**
 * @brief 执行fallbackAvatarColor的核心逻辑。
 * @param seed 字符串参数 `seed`。
 * @param outgoing 布尔参数 `outgoing`。
 * @return 返回颜色对象。
 */
QColor fallbackAvatarColor(const QString &seed, bool outgoing) {
  if (outgoing) {
    return QColor(QStringLiteral("#4a90e2"));
  }

  static const QColor kPalette[] = {
      QColor(QStringLiteral("#7f8ea3")),
      QColor(QStringLiteral("#5f8b7e")),
      QColor(QStringLiteral("#8e7aa8")),
      QColor(QStringLiteral("#9a7b5f")),
      QColor(QStringLiteral("#607d8b"))};
  const uint hash = qHash(seed.trimmed());
  return kPalette[hash % (sizeof(kPalette) / sizeof(kPalette[0]))];
}

/**
 * @brief 执行circularAvatarPixmap的核心逻辑。
 * @param pixmap 位图对象。
 * @param side 数值参数 `side`。
 * @return 返回处理后的位图对象。
 */
QPixmap circularAvatarPixmap(const QPixmap &pixmap, int side) {
  if (pixmap.isNull() || side <= 0) {
    return QPixmap();
  }

  const QPixmap scaled =
      pixmap.scaled(side, side, Qt::KeepAspectRatioByExpanding,
                    Qt::SmoothTransformation);
  QPixmap circular(side, side);
  circular.fill(Qt::transparent);

  QPainter painter(&circular);
  painter.setRenderHint(QPainter::Antialiasing, true);
  QPainterPath path;
  path.addEllipse(0, 0, side, side);
  painter.setClipPath(path);
  painter.drawPixmap(0, 0, scaled);
  return circular;
}

/**
 * @brief 加载头像pixmap数据。
 * @param avatarSource 头像地址或头像来源。
 * @param side 数值参数 `side`。
 * @return 返回处理后的位图对象。
 */
QPixmap loadAvatarPixmap(const QString &avatarSource, int side) {
  if (side <= 0) {
    return QPixmap();
  }

  const QString trimmed = avatarSource.trimmed();
  if (trimmed.isEmpty()) {
    return QPixmap();
  }

  QString localPath = trimmed;
  if (!trimmed.startsWith(QLatin1Char(':'))) {
    const QUrl avatarUrl(trimmed);
    if (avatarUrl.isValid() && avatarUrl.isLocalFile()) {
      localPath = avatarUrl.toLocalFile();
    }
  }

  if (!localPath.startsWith(QLatin1Char(':')) && !QFileInfo::exists(localPath)) {
    return QPixmap();
  }

  const QPixmap pixmap(localPath);
  return circularAvatarPixmap(pixmap, side);
}
} // namespace

/**
 * @brief 构造并初始化SessionWindow实例。
 * @param session 会话对象。
 * @param parent 父级对象指针，用于管理当前对象的生命周期。
 * @return 无返回值。
 */
SessionWindow::SessionWindow(const Session &session, QWidget *parent)
    : FramelessWindowBase(parent), m_session(session), m_chatScroll(nullptr),
      m_chatContainer(nullptr), m_chatLayout(nullptr), m_inputLine(nullptr),
      m_sendBtn(nullptr), m_presenceLabel(nullptr),
      m_websocket(websocketclient::instance()) {
  setAttribute(Qt::WA_DeleteOnClose);
  setAttribute(Qt::WA_TranslucentBackground);
  setStandardTitleBarVisible(false);
  setResizeBorderWidth(8);
  m_avatarNetworkManager = new QNetworkAccessManager(this);
  connect(m_avatarNetworkManager, &QNetworkAccessManager::finished, this,
          [this](QNetworkReply *reply) {
            if (!reply) {
              return;
            }

            const QString avatarSource =
                reply->property("avatar_source").toString().trimmed();
            if (!avatarSource.isEmpty()) {
              m_pendingAvatarSources.remove(avatarSource);
            }

            const auto finalizeFailure = [this, &avatarSource]() {
              if (!avatarSource.isEmpty()) {
                m_failedAvatarSources.insert(avatarSource);
              }
            };

            if (reply->error() != QNetworkReply::NoError) {
              finalizeFailure();
              reply->deleteLater();
              return;
            }

            QPixmap pixmap;
            const QByteArray body = reply->readAll();
            if (body.isEmpty() || !pixmap.loadFromData(body)) {
              finalizeFailure();
              reply->deleteLater();
              return;
            }

            const QPixmap circular = circularAvatarPixmap(pixmap, kMessageAvatarSide);
            if (circular.isNull()) {
              finalizeFailure();
              reply->deleteLater();
              return;
            }

            if (!avatarSource.isEmpty()) {
              m_failedAvatarSources.remove(avatarSource);
              m_avatarPixmapsBySource.insert(avatarSource, circular);
              refreshMessagesForAvatarSource(avatarSource);
            }
            reply->deleteLater();
          });
  initUI();
}

/**
 * @brief 加载历史数据。
 * @param messages 消息对象。
 * @return 无返回值。
 */
void SessionWindow::loadHistory(const QVector<ChatMessage> &messages) {
  for (const ChatMessage &message : messages) {
    appendPersistedMessage(message);
  }
}

/**
 * @brief 设置当前资料值。
 * @param displayName 字符串参数 `displayName`。
 * @param avatarSource 头像地址或头像来源。
 * @return 无返回值。
 */
void SessionWindow::setCurrentProfile(const QString &displayName,
                                      const QString &avatarSource) {
  const QString nextDisplayName = displayName.trimmed();
  const QString nextAvatarSource = avatarSource.trimmed();
  const bool changed =
      m_currentDisplayName != nextDisplayName ||
      m_currentAvatarSource != nextAvatarSource;
  m_currentDisplayName = nextDisplayName;
  m_currentAvatarSource = nextAvatarSource;
  if (!changed) {
    return;
  }
  for (int index = 0; index < m_messages.size(); ++index) {
    updateMessageBubble(index);
  }
}

/**
 * @brief 设置peer资料值。
 * @param displayName 字符串参数 `displayName`。
 * @param avatarSource 头像地址或头像来源。
 * @return 无返回值。
 */
void SessionWindow::setPeerProfile(const QString &displayName,
                                   const QString &avatarSource) {
  const QString nextDisplayName = displayName.trimmed();
  const QString nextAvatarSource = avatarSource.trimmed();
  const bool changed =
      m_peerDisplayName != nextDisplayName ||
      m_peerAvatarSource != nextAvatarSource;
  m_peerDisplayName = nextDisplayName;
  m_peerAvatarSource = nextAvatarSource;
  if (!changed) {
    return;
  }
  for (int index = 0; index < m_messages.size(); ++index) {
    updateMessageBubble(index);
  }
}

/**
 * @brief 设置peeridentity值。
 * @param userId 用户 ID。
 * @param numericId 数字编号。
 * @return 无返回值。
 */
void SessionWindow::setPeerIdentity(const QString &userId,
                                    const QString &numericId) {
  m_peerUserId = userId.trimmed();
  m_peerNumericId = numericId.trimmed();
}

/**
 * @brief 更新peer在线状态状态。
 * @param isOnline 在线状态标记。
 * @param lastSeenAtUtc 最近在线时间字符串。
 * @return 无返回值。
 */
void SessionWindow::updatePeerPresence(bool isOnline,
                                       const QString &lastSeenAtUtc) {
  m_peerIsOnline = isOnline;
  m_peerLastSeenAtUtc = lastSeenAtUtc.trimmed();
  refreshPresenceLabel();
  qInfo().noquote() << "[SessionWindow] updated peer presence user_id="
                    << m_peerUserId << "numeric_id=" << m_peerNumericId
                    << "is_online=" << m_peerIsOnline << "last_seen_at="
                    << m_peerLastSeenAtUtc;
}

/**
 * @brief 初始化界面依赖与状态。
 * @return 无返回值。
 */
void SessionWindow::initUI() {
  setWindowTitle(m_session.displayName());
  resize(kDefaultSessionWindowWidth, kDefaultSessionWindowHeight);
  setMinimumSize(kMinimumSessionWindowWidth, kMinimumSessionWindowHeight);

  QWidget *root = contentWidget();
  root->setAttribute(Qt::WA_TranslucentBackground);
  root->setStyleSheet("background: transparent;");
  QVBoxLayout *mainLayout = new QVBoxLayout(root);
  mainLayout->setContentsMargins(0, 0, 0, 0);
  mainLayout->setSpacing(0);

  QWidget *container = new QWidget(root);
  container->setObjectName("SessionContainer");
  container->setStyleSheet("#SessionContainer { background-color: #ffffff; "
                           "border: 1px solid #dcdcdc; border-radius: 4px; }");
  mainLayout->addWidget(container);

  QVBoxLayout *containerLayout = new QVBoxLayout(container);
  containerLayout->setContentsMargins(0, 0, 0, 0);
  containerLayout->setSpacing(0);

  /**
   * @brief 3. 自定义顶部标题栏
   * @param arg1 输入参数 `arg1`。
   * @return 返回处理得到的对象指针。
   */
  QWidget *header = new QWidget(container);
  header->setFixedHeight(50);
  header->setStyleSheet(
      "background-color: #ffffff; border-bottom: 1px solid #e0e0e0; "
      "border-top-left-radius: 4px; border-top-right-radius: 4px;");

  QHBoxLayout *headerLayout = new QHBoxLayout(header);
  headerLayout->setContentsMargins(15, 6, 10, 6);

  /**
   * @brief 标题文本 (居中)
   * @param arg1 输入参数 `arg1`。
   * @param arg2 输入参数 `arg2`。
   * @return 返回处理得到的对象指针。
   */
  QLabel *titleLabel = new QLabel(m_session.displayName(), header);
  titleLabel->setStyleSheet("font-size: 16px; font-weight: bold; color: #333;");
  titleLabel->setAlignment(Qt::AlignCenter);
  m_presenceLabel = new QLabel(QStringLiteral("离线"), header);
  m_presenceLabel->setStyleSheet("font-size: 12px; color: #7a7a7a;");
  m_presenceLabel->setAlignment(Qt::AlignCenter);

  QVBoxLayout *titleLayout = new QVBoxLayout();
  titleLayout->setContentsMargins(0, 0, 0, 0);
  titleLayout->setSpacing(2);
  titleLayout->addWidget(titleLabel, 0, Qt::AlignCenter);
  titleLayout->addWidget(m_presenceLabel, 0, Qt::AlignCenter);

  /**
   * @brief 关闭按钮
   * @param arg1 输入参数 `arg1`。
   * @param arg2 输入参数 `arg2`。
   * @return 返回 QPushButton closeBtn = new 结果。
   */
  QPushButton *closeBtn = new QPushButton("×", header);
  closeBtn->setFixedSize(30, 30);
  closeBtn->setStyleSheet(
      "QPushButton { border: none; font-weight: bold; color: #555; font-size: "
      "20px; background: transparent; }"
      "QPushButton:hover { background-color: #ff4d4d; color: white; "
      "border-radius: 4px; }");
  connect(closeBtn, &QPushButton::clicked, this, &QWidget::close);

  // 布局组装：使用弹簧将标题挤到中间（这里简单处理，左侧加弹簧，右侧加弹簧和按钮）
  // 为了严格居中，通常需要更复杂的布局，这里使用简单方式：
  // 左侧 Spacer - Title - Right Spacer - CloseBtn
  // 注意：如果有 CloseBtn 存在，绝对居中需要补偿右侧宽度。
  // 这里简化为：Title 占据主要空间并居中显示

  headerLayout->addStretch();
  headerLayout->addLayout(titleLayout);
  headerLayout->addStretch();
  headerLayout->addWidget(closeBtn);

  containerLayout->addWidget(header);
  addDragRegion(header);

  /**
   * @brief 内容区域
   * @param arg1 输入参数 `arg1`。
   * @return 返回处理得到的对象指针。
   */
  QWidget *contentArea = new QWidget(container);
  contentArea->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

  QVBoxLayout *contentLayout = new QVBoxLayout(contentArea);
  contentLayout->setContentsMargins(12, 12, 12, 12);
  contentLayout->setSpacing(12);

  m_chatScroll = new QScrollArea(contentArea);
  m_chatScroll->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  m_chatScroll->setWidgetResizable(true);
  m_chatScroll->setFrameShape(QFrame::NoFrame);
  m_chatScroll->setStyleSheet("QScrollArea { background-color: #ffffff; border: "
                              "1px solid #dcdcdc; border-radius: 8px; }");
  m_chatScroll->viewport()->setStyleSheet("background-color: #ffffff;");

  m_chatContainer = new QWidget(m_chatScroll);
  m_chatContainer->setStyleSheet("background-color: #ffffff;");
  m_chatLayout = new QVBoxLayout(m_chatContainer);
  m_chatLayout->setContentsMargins(0, 10, 10, 10);
  m_chatLayout->setSpacing(8);
  m_chatLayout->setAlignment(Qt::AlignTop);

  m_chatScroll->setWidget(m_chatContainer);
  contentLayout->addWidget(m_chatScroll, 3);

  QWidget *attachmentBar = new QWidget(contentArea);
  attachmentBar->setObjectName("SessionAttachmentBar");
  attachmentBar->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  attachmentBar->setStyleSheet(
      "#SessionAttachmentBar { background-color: #fbfcfe; border: 1px solid "
      "#e6ebf2; border-radius: 10px; }");

  QHBoxLayout *attachmentLayout = new QHBoxLayout(attachmentBar);
  attachmentLayout->setContentsMargins(10, 7, 10, 7);
  attachmentLayout->setSpacing(8);

  auto attachmentButtonStyle = QStringLiteral(
      "QPushButton { min-width: 58px; min-height: 30px; background-color: "
      "#f3f7fd; color: #25476a; border: 1px solid #d7e3f3; border-radius: "
      "8px; padding: 0 12px; }"
      "QPushButton:hover { background-color: #e7f0fb; border-color: #a9c7ef; }");

  QPushButton *fileButton = new QPushButton(QStringLiteral("文件"), attachmentBar);
  fileButton->setCursor(Qt::PointingHandCursor);
  fileButton->setStyleSheet(attachmentButtonStyle);
  attachmentLayout->addWidget(fileButton, 0, Qt::AlignLeft);
  attachmentLayout->addStretch();

  connect(fileButton, &QPushButton::clicked, this,
          [this]() { requestFileAttachment(); });

  contentLayout->addWidget(attachmentBar);

  QWidget *inputWrapper = new QWidget(contentArea);
  inputWrapper->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

  QGridLayout *inputLayout = new QGridLayout(inputWrapper);
  inputLayout->setContentsMargins(0, 0, 0, 0);
  inputLayout->setHorizontalSpacing(0);
  inputLayout->setVerticalSpacing(0);
  inputLayout->setRowStretch(0, 1);

  m_inputLine = new QTextEdit(inputWrapper);
  m_inputLine->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  m_inputLine->setPlaceholderText(QStringLiteral("输入消息"));
  m_inputLine->setAcceptRichText(false);
  m_inputLine->setMinimumHeight(150);
  m_inputLine->setStyleSheet(
      "QTextEdit { border: 1px solid #dcdcdc; border-radius: 8px; "
      "padding: 8px 88px 40px 8px; background: #ffffff; color: #111111; }");
  inputLayout->addWidget(m_inputLine, 0, 0);

  m_sendBtn = new QPushButton("发送", inputWrapper);
  m_sendBtn->setCursor(Qt::PointingHandCursor);
  m_sendBtn->setFixedHeight(32);
  m_sendBtn->setMinimumWidth(68);
  m_sendBtn->setStyleSheet(
      "QPushButton { background-color: #4a90e2; color: white; border: none; "
      "border-radius: 6px; padding: 6px 16px; }"
      "QPushButton:hover { background-color: #3a78d6; }");
  inputLayout->addWidget(m_sendBtn, 0, 0, Qt::AlignRight | Qt::AlignBottom);

  connect(m_sendBtn, &QPushButton::clicked, this,
          &SessionWindow::onSendClicked);
  connect(m_sendBtn, &QPushButton::clicked, this,
          &SessionWindow::sendPendingMessage);
  connect(m_websocket, &websocketclient::textMessageReceived, this,
          [this](const QString &message) {
            handleIncomingPayload(message, QStringLiteral("文本"));
            qDebug() << "Received text payload: " << message << Qt::endl;
          });
  connect(m_websocket, &websocketclient::binaryMessageReceived, this,
          [this](const QByteArray &data) {
            const QString payload = QString::fromUtf8(data);
            handleIncomingPayload(payload, QStringLiteral("二进制"));
            qDebug() << "Received binary payload: " << data << Qt::endl;
          });
  connect(m_websocket, &websocketclient::errorOccurred, this,
          [this](QAbstractSocket::SocketError, const QString &message) {
            appendStatusLine("连接错误: " + message);
          });

  contentLayout->addWidget(inputWrapper, 1);

  containerLayout->addWidget(contentArea);
  refreshPresenceLabel();
}

/**
 * @brief 发送待处理消息数据。
 * @return 无返回值。
 */
void SessionWindow::sendPendingMessage() {
  if (!m_inputLine || !m_chatLayout)
    return;

  QString message = m_pendingMessage;
  if (message.isEmpty())
    message = m_inputLine->toPlainText().trimmed();
  if (message.isEmpty())
    return;

  m_pendingMessage.clear();
  const QString conversationId = m_session.conversationId().trimmed();
  if (conversationId.isEmpty()) {
    appendStatusLine(QStringLiteral("缺少 conversation_id，无法发送消息"));
    qWarning() << "[SessionWindow] missing conversation_id for display_name="
               << m_session.displayName() << "peer_user_id=" << m_peerUserId
               << "peer_numeric_id=" << m_peerNumericId;
    return;
  }

  ChatMessage localMessage;
  localMessage.localId = QUuid::createUuid().toString(QUuid::WithoutBraces);
  localMessage.requestId = QUuid::createUuid().toString(QUuid::WithoutBraces);
  localMessage.conversationId = conversationId;
  localMessage.kind = ChatMessageKind::Text;
  localMessage.text = message;
  localMessage.sentAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
  localMessage.senderUserId = UserSession::instance().userId().trimmed();
  localMessage.senderNumericId = UserSession::instance().numericId().trimmed();
  localMessage.senderUsername = UserSession::instance().username().trimmed();
  const int messageIndex = appendMessage(localMessage, MessageStatus::Pending);
  m_pendingMessageIndexesByRequestId.insert(localMessage.requestId, messageIndex);
  emit messageReadyForPersistence(localMessage);

  QJsonObject data;
  data.insert("conversation_id", conversationId);
  data.insert("content", message);

  const QString payload =
      protocol::createRequest("MESSAGE", "SEND", data, localMessage.requestId);
  qInfo() << "[SessionWindow] MESSAGE SEND request_id=" << localMessage.requestId
          << "conversation_id=" << conversationId;
  if (!m_websocket || !m_websocket->isConnected()) {
    markPendingMessageFailed(messageIndex, QStringLiteral("连接未建立"));
    appendStatusLine(QStringLiteral("发送失败：WebSocket 未连接"));
    return;
  }
  m_websocket->sendTextMessage(payload);
  m_inputLine->clear();
  emit outgoingMessageSubmitted(conversationId, message);
}

/**
 * @brief 响应发送点击事件。
 * @return 无返回值。
 */
void SessionWindow::onSendClicked() {
  if (!m_inputLine)
    return;
  m_pendingMessage = m_inputLine->toPlainText().trimmed();
}

/**
 * @brief 发起文件attachment请求。
 * @return 无返回值。
 */
void SessionWindow::requestFileAttachment() {
  const QString conversationId = m_session.conversationId().trimmed();
  if (conversationId.isEmpty()) {
    appendStatusLine(QStringLiteral("缺少 conversation_id，无法发送文件"));
    qWarning() << "[SessionWindow] missing conversation_id for attachment"
               << "display_name=" << m_session.displayName()
               << "kind=file";
    return;
  }

  const QString conversationName = m_session.displayName().trimmed();
  emit fileAttachmentRequested(conversationId, conversationName);
}

/**
 * @brief 追加状态line内容。
 * @param message 消息文本或提示信息。
 * @return 无返回值。
 */
void SessionWindow::appendStatusLine(const QString &message) {
  const QString line =
      QDateTime::currentDateTime().toString("HH:mm:ss ") + message;
  appendChatBubble(line, false, true);
  qInfo() << "Session status:" << message;
}

/**
 * @brief 追加聊天bubble内容。
 * @param message 消息文本或提示信息。
 * @param outgoing 布尔参数 `outgoing`。
 * @param status 状态值。
 * @return 返回处理得到的对象指针。
 */
QLabel *SessionWindow::appendChatBubble(const QString &message, bool outgoing,
                                        bool status) {
  if (!m_chatLayout || !m_chatContainer || !m_chatScroll)
    return nullptr;

  QWidget *row = new QWidget(m_chatContainer);
  QHBoxLayout *rowLayout = new QHBoxLayout(row);
  rowLayout->setContentsMargins(0, 0, 0, 0);

  QLabel *bubble = new QLabel(message, row);
  bubble->setWordWrap(true);
  bubble->setTextInteractionFlags(Qt::TextSelectableByMouse);
  bubble->setMaximumWidth(kMessageContentMaximumWidth);

  if (status) {
    bubble->setStyleSheet("QLabel { background: #f1f3f5; color: #4f5b66; "
                          "border-radius: 10px; padding: 8px 12px; }");
    rowLayout->addWidget(bubble);
    rowLayout->addStretch();
  } else if (outgoing) {
    bubble->setStyleSheet("QLabel { background: #e2f0ff; color: #1f3552; "
                          "border-radius: 12px; padding: 8px 12px; }");
    rowLayout->addStretch();
    rowLayout->addWidget(bubble);
  } else {
    bubble->setStyleSheet("QLabel { background: #f7f7f8; color: #2f2f2f; "
                          "border-radius: 12px; padding: 8px 12px; }");
    rowLayout->addWidget(bubble);
    rowLayout->addStretch();
  }

  m_chatLayout->addWidget(row);
  scrollChatToBottom();
  return bubble;
}

/**
 * @brief 刷新在线状态label显示或缓存。
 * @return 无返回值。
 */
void SessionWindow::refreshPresenceLabel() {
  if (!m_presenceLabel) {
    return;
  }
  if (m_session.type() == Session::Type::Group) {
    m_presenceLabel->setText(QStringLiteral("群聊"));
    return;
  }
  m_presenceLabel->setText(presenceText(m_peerIsOnline, m_peerLastSeenAtUtc));
}

/**
 * @brief 处理incomingpayload流程。
 * @param payload 原始载荷字符串。
 * @param sourceTag 来源标识或来源数据。
 * @return 无返回值。
 */
void SessionWindow::handleIncomingPayload(const QString &payload,
                                          const QString &sourceTag) {
  if (!m_chatLayout)
    return;

  protocol::Envelope envelope;
  QString parseError;
  if (protocol::parseEnvelope(payload, &envelope, &parseError)) {
    if (envelope.type == QStringLiteral("MESSAGE") &&
        envelope.action == QStringLiteral("SEND")) {
      if (!envelope.requestId.trimmed().isEmpty()) {
        handleMessageSendResponse(envelope);
      }
    }
    return;
  }

  qWarning() << "Protocol parse failed, source:" << sourceTag << "error:"
             << parseError << "payload:" << payload;
}

/**
 * @brief 追加persisted消息内容。
 * @param message 消息对象或消息内容。
 * @return 无返回值。
 */
void SessionWindow::appendPersistedMessage(const ChatMessage &message) {
  if (!message.isValid()) {
    return;
  }
  const int existingIndex = findExistingMessageIndex(message);
  if (existingIndex >= 0) {
    DisplayMessage &displayMessage = m_messages[existingIndex];
    displayMessage.message = message;
    displayMessage.status = isOutgoingMessage(message) ? MessageStatus::Sent
                                                       : MessageStatus::Received;
    updateMessageBubble(existingIndex);
    return;
  }
  appendMessage(message, isOutgoingMessage(message) ? MessageStatus::Sent
                                                    : MessageStatus::Received);
}

/**
 * @brief 追加消息内容。
 * @param message 消息对象或消息内容。
 * @param status 状态值。
 * @return 返回计算得到的数值结果。
 */
int SessionWindow::appendMessage(const ChatMessage &message, MessageStatus status) {
  DisplayMessage displayMessage;
  displayMessage.message = message;
  displayMessage.status = status;
  m_messages.push_back(displayMessage);
  const int index = m_messages.size() - 1;
  DisplayMessage &storedMessage = m_messages[index];
  storedMessage.rowWidget = new QWidget(m_chatContainer);
  storedMessage.rowLayout = new QHBoxLayout(storedMessage.rowWidget);
  storedMessage.rowLayout->setContentsMargins(0, 0, 0, 0);
  storedMessage.rowLayout->setSpacing(10);
  m_chatLayout->addWidget(storedMessage.rowWidget);
  updateMessageBubble(index);
  scrollChatToBottom();
  return index;
}

/**
 * @brief 更新消息bubble状态。
 * @param index 数值参数 `index`。
 * @return 无返回值。
 */
void SessionWindow::updateMessageBubble(int index) {
  if (index < 0 || index >= m_messages.size()) {
    return;
  }

  DisplayMessage &displayMessage = m_messages[index];
  if (!displayMessage.rowLayout || !displayMessage.rowWidget) {
    return;
  }

  while (QLayoutItem *item = displayMessage.rowLayout->takeAt(0)) {
    if (QWidget *widget = item->widget()) {
      widget->deleteLater();
    }
    delete item;
  }

  displayMessage.bubbleLabel = nullptr;
  const bool outgoing =
      isOutgoingMessage(displayMessage.message) ||
      displayMessage.status == MessageStatus::Pending ||
      displayMessage.status == MessageStatus::Sent ||
      displayMessage.status == MessageStatus::Failed;
  QWidget *avatarWidget = createMessageAvatarWidget(index, outgoing);
  displayMessage.contentWidget = createMessageContentWidget(index);
  if (!avatarWidget || !displayMessage.contentWidget) {
    return;
  }
  if (outgoing) {
    displayMessage.rowLayout->addStretch();
    displayMessage.rowLayout->addWidget(displayMessage.contentWidget, 0,
                                        Qt::AlignTop);
    displayMessage.rowLayout->addWidget(avatarWidget, 0, Qt::AlignTop);
  } else {
    displayMessage.rowLayout->addWidget(avatarWidget, 0, Qt::AlignTop);
    displayMessage.rowLayout->addWidget(displayMessage.contentWidget, 0,
                                        Qt::AlignTop);
    displayMessage.rowLayout->addStretch();
  }
}

/**
 * @brief 创建消息头像widget对象或数据。
 * @param index 数值参数 `index`。
 * @param outgoing 布尔参数 `outgoing`。
 * @return 返回处理得到的对象指针。
 */
QWidget *SessionWindow::createMessageAvatarWidget(int index, bool outgoing) {
  if (index < 0 || index >= m_messages.size()) {
    return nullptr;
  }

  const DisplayMessage &displayMessage = m_messages[index];
  const ChatMessage &message = displayMessage.message;
  QString displayName;
  QString avatarSource;

  if (outgoing) {
    displayName = m_currentDisplayName.trimmed();
    if (displayName.isEmpty()) {
      displayName = UserSession::instance().username().trimmed();
    }
    avatarSource = m_currentAvatarSource.trimmed();
  } else if (m_session.type() == Session::Type::Group) {
    displayName = message.senderUsername.trimmed();
    if (displayName.isEmpty()) {
      displayName = QStringLiteral("Group");
    }
  } else {
    displayName = m_peerDisplayName.trimmed();
    if (displayName.isEmpty()) {
      displayName = message.senderUsername.trimmed();
    }
    if (displayName.isEmpty()) {
      displayName = QStringLiteral("Peer");
    }
    avatarSource = m_peerAvatarSource.trimmed();
  }

  auto *avatarLabel = new QLabel(displayMessage.rowWidget);
  avatarLabel->setFixedSize(kMessageAvatarSide, kMessageAvatarSide);
  avatarLabel->setAlignment(Qt::AlignCenter);
  avatarLabel->setTextInteractionFlags(Qt::NoTextInteraction);

  const QString trimmedAvatarSource = avatarSource.trimmed();
  QPixmap avatarPixmap;
  if (!trimmedAvatarSource.isEmpty()) {
    const auto cachedIt = m_avatarPixmapsBySource.constFind(trimmedAvatarSource);
    if (cachedIt != m_avatarPixmapsBySource.cend()) {
      avatarPixmap = cachedIt.value();
    } else {
      avatarPixmap =
          loadAvatarPixmap(trimmedAvatarSource, avatarLabel->width());
      if (!avatarPixmap.isNull()) {
        m_avatarPixmapsBySource.insert(trimmedAvatarSource, avatarPixmap);
      } else {
        requestAvatarIfNeeded(trimmedAvatarSource);
      }
    }
  }

  if (!avatarPixmap.isNull()) {
    avatarLabel->setPixmap(avatarPixmap);
    avatarLabel->setStyleSheet(
        QStringLiteral("QLabel { background: transparent; }"));
    return avatarLabel;
  }

  const QString initial =
      avatarInitial(displayName, outgoing ? QStringLiteral("我")
                                          : QStringLiteral("对"));
  const QColor avatarColor = fallbackAvatarColor(displayName, outgoing);
  avatarLabel->setStyleSheet(
      QStringLiteral(
          "QLabel { background-color: %1; color: #ffffff; border-radius: 18px; "
          "font-size: 14px; font-weight: 600; }")
          .arg(avatarColor.name(QColor::HexRgb)));
  avatarLabel->setText(
      initial.trimmed().isEmpty() ? (outgoing ? QStringLiteral("M")
                                              : QStringLiteral("P"))
                                  : initial);
  return avatarLabel;
}

/**
 * @brief 创建消息contentwidget对象或数据。
 * @param index 数值参数 `index`。
 * @return 返回处理得到的对象指针。
 */
QWidget *SessionWindow::createMessageContentWidget(int index) {
  if (index < 0 || index >= m_messages.size()) {
    return nullptr;
  }

  DisplayMessage &displayMessage = m_messages[index];
  const bool outgoing =
      isOutgoingMessage(displayMessage.message) ||
      displayMessage.status == MessageStatus::Pending ||
      displayMessage.status == MessageStatus::Sent ||
      displayMessage.status == MessageStatus::Failed;
  const MessageStatus visualStatus =
      outgoing && displayMessage.status == MessageStatus::Received
          ? MessageStatus::Sent
          : displayMessage.status;
  if (displayMessage.message.kind == ChatMessageKind::File) {
    return createFileCardWidget(index, outgoing);
  }

  const QString timeText = formatMessageTime(displayMessage.message.sentAt);
  QString bubbleText;
  if (!outgoing) {
    const QString sender =
        displayMessage.message.senderUsername.trimmed().isEmpty()
            ? QStringLiteral("对方")
            : displayMessage.message.senderUsername.trimmed();
    bubbleText = QStringLiteral("%1 %2: %3")
                     .arg(timeText, sender,
                          renderMessageBody(displayMessage.message));
  } else {
    bubbleText = QStringLiteral("%1 我: %2 [%3]")
                     .arg(timeText, renderMessageBody(displayMessage.message),
                          messageStatusText(visualStatus));
    if (visualStatus == MessageStatus::Sent &&
        displayMessage.message.seq > 0) {
      bubbleText += QStringLiteral(" (#%1)").arg(displayMessage.message.seq);
    }
  }
  auto *bubble = new QLabel(bubbleText, displayMessage.rowWidget);
  bubble->setWordWrap(true);
  bubble->setTextInteractionFlags(Qt::TextSelectableByMouse);
  bubble->setMaximumWidth(kMessageContentMaximumWidth);
  bubble->setStyleSheet(
      outgoing
          ? QStringLiteral("QLabel { background: #e2f0ff; color: #1f3552; "
                           "border-radius: 12px; padding: 8px 12px; }")
          : QStringLiteral("QLabel { background: #f7f7f8; color: #2f2f2f; "
                           "border-radius: 12px; padding: 8px 12px; }"));
  displayMessage.bubbleLabel = bubble;
  return bubble;
}

/**
 * @brief 创建文件cardwidget对象或数据。
 * @param index 数值参数 `index`。
 * @param outgoing 布尔参数 `outgoing`。
 * @return 返回处理得到的对象指针。
 */
QWidget *SessionWindow::createFileCardWidget(int index, bool outgoing) {
  if (index < 0 || index >= m_messages.size()) {
    return nullptr;
  }

  const DisplayMessage &displayMessage = m_messages[index];
  const ChatMessage &message = displayMessage.message;
  const QString fileName =
      message.file.originalName.trimmed().isEmpty()
          ? QStringLiteral("未命名文件")
          : message.file.originalName.trimmed();
  const QString contentType =
      message.file.contentType.trimmed().isEmpty()
          ? QStringLiteral("未知类型")
          : message.file.contentType.trimmed();
  const QString metaText = QStringLiteral("%1  |  %2")
                               .arg(humanReadableFileSize(message.file.sizeBytes),
                                    contentType);
  const MessageStatus visualStatus =
      outgoing && displayMessage.status == MessageStatus::Received
          ? MessageStatus::Sent
          : displayMessage.status;
  const QString ownerText =
      outgoing
          ? QStringLiteral("%1 我 [%2]")
                .arg(formatMessageTime(message.sentAt),
                     messageStatusText(visualStatus))
          : QStringLiteral("%1 %2")
                .arg(formatMessageTime(message.sentAt),
                     message.senderUsername.trimmed().isEmpty()
                         ? QStringLiteral("对方")
                         : message.senderUsername.trimmed());

  auto *card = new QFrame(displayMessage.rowWidget);
  card->setObjectName(QStringLiteral("ChatFileCard"));
  card->setMaximumWidth(kMessageContentMaximumWidth);
  card->setMinimumWidth(kFileCardMinimumWidth);
  card->setStyleSheet(
      outgoing
          ? QStringLiteral(
                "QFrame#ChatFileCard { background-color: #eef6ff; border: 1px solid "
                "#c6dbf7; border-radius: 14px; }")
          : QStringLiteral(
                "QFrame#ChatFileCard { background-color: #f8f9fb; border: 1px solid "
                "#dde3ea; border-radius: 14px; }"));

  auto *cardLayout = new QVBoxLayout(card);
  cardLayout->setContentsMargins(14, 12, 14, 12);
  cardLayout->setSpacing(10);

  auto *headerLayout = new QHBoxLayout();
  headerLayout->setContentsMargins(0, 0, 0, 0);
  headerLayout->setSpacing(8);

  auto *badge = new QLabel(QStringLiteral("文件"), card);
  badge->setStyleSheet(
      outgoing
          ? QStringLiteral("QLabel { background: #cfe4ff; color: #1d4f91; "
                           "border-radius: 8px; padding: 3px 8px; font-weight: 600; }")
          : QStringLiteral("QLabel { background: #e8edf3; color: #49586b; "
                           "border-radius: 8px; padding: 3px 8px; font-weight: 600; }"));
  headerLayout->addWidget(badge, 0, Qt::AlignTop);

  auto *titleLayout = new QVBoxLayout();
  titleLayout->setContentsMargins(0, 0, 0, 0);
  titleLayout->setSpacing(4);

  auto *titleLabel = new QLabel(fileName, card);
  titleLabel->setWordWrap(true);
  titleLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
  titleLabel->setStyleSheet(
      "QLabel { color: #16202a; font-size: 14px; font-weight: 600; }");
  titleLayout->addWidget(titleLabel);

  auto *metaLabel = new QLabel(metaText, card);
  metaLabel->setWordWrap(true);
  metaLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
  metaLabel->setStyleSheet("QLabel { color: #66727f; font-size: 12px; }");
  titleLayout->addWidget(metaLabel);

  auto *ownerLabel = new QLabel(ownerText, card);
  ownerLabel->setWordWrap(true);
  ownerLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
  ownerLabel->setStyleSheet("QLabel { color: #51606f; font-size: 12px; }");
  titleLayout->addWidget(ownerLabel);

  headerLayout->addLayout(titleLayout, 1);
  cardLayout->addLayout(headerLayout);

  auto *buttonLayout = new QHBoxLayout();
  buttonLayout->setContentsMargins(0, 0, 0, 0);
  buttonLayout->setSpacing(8);

  auto *downloadButton = new QPushButton(QStringLiteral("下载"), card);
  downloadButton->setCursor(Qt::PointingHandCursor);
  downloadButton->setStyleSheet(
      "QPushButton { background-color: #4a90e2; color: #ffffff; border: none; "
      "border-radius: 8px; padding: 6px 14px; font-weight: 600; }"
      "QPushButton:hover { background-color: #3c7fce; }");
  connect(downloadButton, &QPushButton::clicked, this,
          [this, index]() {
            if (index < 0 || index >= m_messages.size()) {
              return;
            }
            emit fileDownloadRequested(m_messages[index].message, false);
          });
  buttonLayout->addWidget(downloadButton);

  auto *saveAsButton = new QPushButton(QStringLiteral("另存为"), card);
  saveAsButton->setCursor(Qt::PointingHandCursor);
  saveAsButton->setStyleSheet(
      "QPushButton { background-color: transparent; color: #35506d; "
      "border: 1px solid #bfd0e3; border-radius: 8px; padding: 6px 14px; "
      "font-weight: 600; }"
      "QPushButton:hover { background-color: #edf3fa; }");
  connect(saveAsButton, &QPushButton::clicked, this,
          [this, index]() {
            if (index < 0 || index >= m_messages.size()) {
              return;
            }
            emit fileDownloadRequested(m_messages[index].message, true);
          });
  buttonLayout->addWidget(saveAsButton);
  buttonLayout->addStretch();

  cardLayout->addLayout(buttonLayout);
  return card;
}

/**
 * @brief 查找Existing消息Index。
 * @param message 消息对象或消息内容。
 * @return 返回计算得到的数值结果。
 */
int SessionWindow::findExistingMessageIndex(const ChatMessage &message) const {
  for (int index = 0; index < m_messages.size(); ++index) {
    const ChatMessage &existing = m_messages[index].message;
    if (existing.conversationId.trimmed() != message.conversationId.trimmed()) {
      continue;
    }
    if (message.seq > 0 && existing.seq > 0 && existing.seq == message.seq) {
      return index;
    }
    if (!message.messageId.trimmed().isEmpty() &&
        existing.messageId.trimmed() == message.messageId.trimmed()) {
      return index;
    }
    if (!message.requestId.trimmed().isEmpty() &&
        existing.requestId.trimmed() == message.requestId.trimmed()) {
      return index;
    }
  }
  return -1;
}

/**
 * @brief 处理消息发送响应流程。
 * @param envelope 协议封装数据。
 * @return 无返回值。
 */
void SessionWindow::handleMessageSendResponse(const protocol::Envelope &envelope) {
  const QString requestId = envelope.requestId.trimmed();
  if (requestId.isEmpty()) {
    return;
  }

  const auto it = m_pendingMessageIndexesByRequestId.constFind(requestId);
  if (it == m_pendingMessageIndexesByRequestId.cend()) {
    return;
  }

  const int index = it.value();
  if (index < 0 || index >= m_messages.size()) {
    m_pendingMessageIndexesByRequestId.remove(requestId);
    return;
  }

  DisplayMessage &message = m_messages[index];
  const QString responseConversationId =
      jsonStringValue(envelope.data, "conversation_id");
  if (!responseConversationId.isEmpty() &&
      responseConversationId != m_session.conversationId().trimmed()) {
    qWarning() << "[SessionWindow] ignore MESSAGE/SEND response with mismatched "
                  "conversation_id request_id="
               << requestId << "response_conversation_id="
               << responseConversationId << "session_conversation_id="
               << m_session.conversationId();
    return;
  }

  const bool ok = envelope.code == 0 &&
                  (envelope.hasOk ? envelope.ok
                                  : envelope.data.value("ok").toBool(true));
  if (!ok) {
    const QString errorMessage =
        envelope.message.trimmed().isEmpty()
            ? jsonStringValue(envelope.data, "message")
            : envelope.message.trimmed();
    const QString errorText = messageErrorText(envelope.code, errorMessage);
    markPendingMessageFailed(index, errorText);
    appendStatusLine(errorText);
    m_pendingMessageIndexesByRequestId.remove(requestId);
    qWarning() << "[SessionWindow] MESSAGE/SEND failed request_id=" << requestId
               << "code=" << envelope.code << "data="
               << QString::fromUtf8(
                      QJsonDocument(envelope.data).toJson(QJsonDocument::Compact));
    return;
  }

  message.message.conversationId =
      responseConversationId.isEmpty() ? message.message.conversationId
                                       : responseConversationId;
  message.message.messageId = jsonStringValue(envelope.data, "message_id");
  message.message.seq = jsonIntegerValue(envelope.data, "seq");
  const QString sentAt = jsonStringValue(envelope.data, "sent_at");
  if (!sentAt.isEmpty()) {
    message.message.sentAt = sentAt;
  }
  const QString content = jsonStringValue(envelope.data, "content");
  if (!content.isEmpty()) {
    message.message.text = content;
  }
  message.status = MessageStatus::Sent;
  updateMessageBubble(index);
  m_pendingMessageIndexesByRequestId.remove(requestId);
  emit messageReadyForPersistence(message.message);

  qInfo() << "[SessionWindow] MESSAGE/SEND ack request_id=" << requestId
          << "message_id=" << message.message.messageId
          << "seq=" << message.message.seq << "sent_at=" << message.message.sentAt;
}

/**
 * @brief 处理incoming消息push流程。
 * @param envelope 协议封装数据。
 * @return 无返回值。
 */
void SessionWindow::handleIncomingMessagePush(const protocol::Envelope &envelope) {
  const QString conversationId = jsonStringValue(envelope.data, "conversation_id");
  if (conversationId.isEmpty() ||
      conversationId != m_session.conversationId().trimmed()) {
    return;
  }

  ChatMessage incoming;
  incoming.localId = QUuid::createUuid().toString(QUuid::WithoutBraces);
  incoming.conversationId = conversationId;
  incoming.messageId = jsonStringValue(envelope.data, "message_id");
  incoming.seq = jsonIntegerValue(envelope.data, "seq");
  incoming.kind = ChatMessageKind::Text;
  incoming.text = jsonStringValue(envelope.data, "content");
  incoming.sentAt = jsonStringValue(envelope.data, "sent_at");
  incoming.senderUserId = jsonStringValue(envelope.data, "from_user_id");
  incoming.senderUsername = jsonStringValue(envelope.data, "from_username");

  if (incoming.text.isEmpty()) {
    qWarning() << "[SessionWindow] ignore incoming MESSAGE/SEND without content "
                  "conversation_id="
               << conversationId;
    return;
  }

  appendMessage(incoming, MessageStatus::Received);
  emit messageReadyForPersistence(incoming);
  qInfo() << "[SessionWindow] received incoming MESSAGE/SEND conversation_id="
          << conversationId << "message_id=" << incoming.messageId
          << "from_user_id=" << incoming.senderUserId;
}

/**
 * @brief 标记待处理消息失败状态。
 * @param index 数值参数 `index`。
 * @param reason 字符串参数 `reason`。
 * @return 无返回值。
 */
void SessionWindow::markPendingMessageFailed(int index, const QString &reason) {
  if (index < 0 || index >= m_messages.size()) {
    return;
  }

  DisplayMessage &message = m_messages[index];
  message.status = MessageStatus::Failed;
  updateMessageBubble(index);
  qWarning() << "[SessionWindow] pending message failed request_id="
             << message.message.requestId << "reason=" << reason;
}

/**
 * @brief 判断outgoing消息条件是否满足。
 * @param message 消息对象或消息内容。
 * @return 返回条件判断结果，`true` 表示满足，`false` 表示不满足。
 */
bool SessionWindow::isOutgoingMessage(const ChatMessage &message) const {
  const UserSession &session = UserSession::instance();

  const QString currentUserId = session.userId().trimmed();
  if (!currentUserId.isEmpty() &&
      message.senderUserId.trimmed() == currentUserId) {
    return true;
  }

  const QString currentNumericId = session.numericId().trimmed();
  if (!currentNumericId.isEmpty() &&
      message.senderNumericId.trimmed() == currentNumericId) {
    return true;
  }

  const QString currentUsername = session.username().trimmed();
  return !currentUsername.isEmpty() &&
         message.senderUsername.trimmed().compare(currentUsername,
                                                  Qt::CaseInsensitive) == 0;
}

/**
 * @brief 执行renderMessageBody的核心逻辑。
 * @param message 消息对象或消息内容。
 * @return 返回处理后的字符串结果。
 */
QString SessionWindow::renderMessageBody(const ChatMessage &message) const {
  if (message.kind == ChatMessageKind::File) {
    const QString fileName = message.file.originalName.trimmed();
    return fileName.isEmpty() ? QStringLiteral("[文件]")
                              : QStringLiteral("[文件] %1").arg(fileName);
  }
  return message.text;
}

/**
 * @brief 发起头像ifneeded请求。
 * @param avatarSource 头像地址或头像来源。
 * @return 无返回值。
 */
void SessionWindow::requestAvatarIfNeeded(const QString &avatarSource) {
  const QString trimmedSource = avatarSource.trimmed();
  if (trimmedSource.isEmpty() || !m_avatarNetworkManager ||
      m_avatarPixmapsBySource.contains(trimmedSource) ||
      m_pendingAvatarSources.contains(trimmedSource) ||
      m_failedAvatarSources.contains(trimmedSource)) {
    return;
  }

  QString localPath = trimmedSource;
  if (!trimmedSource.startsWith(QLatin1Char(':'))) {
    const QUrl avatarUrl(trimmedSource);
    if (avatarUrl.isValid() && avatarUrl.isLocalFile()) {
      localPath = avatarUrl.toLocalFile();
    }
  }
  if (trimmedSource.startsWith(QLatin1Char(':')) || QFileInfo::exists(localPath)) {
    return;
  }

  const QUrl url = resolveAvatarUrl(trimmedSource);
  if (!url.isValid()) {
    m_failedAvatarSources.insert(trimmedSource);
    return;
  }

  QNetworkRequest request(url);
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                       QNetworkRequest::NoLessSafeRedirectPolicy);
  request.setTransferTimeout(8000);
  QNetworkReply *reply = m_avatarNetworkManager->get(request);
  reply->setProperty("avatar_source", trimmedSource);
  m_pendingAvatarSources.insert(trimmedSource);
}

/**
 * @brief 刷新消息for头像source显示或缓存。
 * @param avatarSource 头像地址或头像来源。
 * @return 无返回值。
 */
void SessionWindow::refreshMessagesForAvatarSource(const QString &avatarSource) {
  const QString trimmedSource = avatarSource.trimmed();
  if (trimmedSource.isEmpty()) {
    return;
  }

  const bool refreshOutgoing =
      m_currentAvatarSource.trimmed() == trimmedSource;
  const bool refreshIncomingDirect =
      m_peerAvatarSource.trimmed() == trimmedSource &&
      m_session.type() != Session::Type::Group;
  if (!refreshOutgoing && !refreshIncomingDirect) {
    return;
  }

  for (int index = 0; index < m_messages.size(); ++index) {
    const ChatMessage &message = m_messages[index].message;
    const bool outgoing = isOutgoingMessage(message) ||
                          m_messages[index].status == MessageStatus::Pending ||
                          m_messages[index].status == MessageStatus::Sent ||
                          m_messages[index].status == MessageStatus::Failed;
    if ((refreshOutgoing && outgoing) ||
        (refreshIncomingDirect && !outgoing)) {
      updateMessageBubble(index);
    }
  }
}

/**
 * @brief 执行scrollChatToBottom的核心逻辑。
 * @return 无返回值。
 */
void SessionWindow::scrollChatToBottom() {
  QTimer::singleShot(0, this, [this]() {
    if (m_chatScroll && m_chatScroll->verticalScrollBar()) {
      m_chatScroll->verticalScrollBar()->setValue(
          m_chatScroll->verticalScrollBar()->maximum());
    }
  });
}




