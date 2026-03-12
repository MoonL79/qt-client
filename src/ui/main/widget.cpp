#include "widget.h"

#include "addfrienddialog.h"
#include "deletefrienddialog.h"
#include "settingswindow.h"
#include "sessionwindow.h"
#include "ui_widget.h"
#include "usersession.h"
#include "websocketclient.h"

#include <QDir>
#include <QMessageBox>
#include <QNetworkRequest>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QtGlobal>

namespace {
constexpr int kDefaultStaticPort = 18080;
constexpr int kFriendListRefreshIntervalMs = 10 * 1000;
constexpr const char *kStaticPortEnv = "QT_SERVER_STATIC_PORT";
constexpr const char *kStaticHostEnv = "QT_SERVER_STATIC_HOST";
constexpr const char *kWebSocketHostEnv = "QT_SERVER_WS_HOST";
constexpr const char *kDefaultServerHost = "192.168.14.133";

bool isLoopbackHost(const QString &host) {
  const QString lower = host.trimmed().toLower();
  return lower == "127.0.0.1" || lower == "localhost" || lower == "::1";
}

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

QString friendStatusText(int status) {
  switch (status) {
  case 1:
    return QStringLiteral("在线");
  case 0:
    return QStringLiteral("离线");
  default:
    return QStringLiteral("状态:%1").arg(status);
  }
}
}

Widget::Widget(QWidget *parent)
    : QWidget(parent), ui(new Ui::Widget), m_isDragging(false) {
  initUI();
  initAvatarHttpClient();

  m_friendListRefreshTimer = new QTimer(this);
  m_friendListRefreshTimer->setInterval(kFriendListRefreshIntervalMs);
  connect(m_friendListRefreshTimer, &QTimer::timeout, this,
          [this]() { requestFriendList(false); });
}

Widget::~Widget() { delete ui; }

void Widget::initUI() {
  this->resize(300, 700);
  this->setMinimumSize(280, 500);  // 设置最小尺寸，保证内容不被过度压缩
  this->setMaximumSize(500, 1000); // 设置最大尺寸上限
  this->setWindowTitle("Chat");

  // 去除系统标题栏 (无边框窗口)
  this->setWindowFlags(
      Qt::FramelessWindowHint |
      Qt::WindowMinMaxButtonsHint); // 移除 WindowSystemMenuHint 可能会更彻底
  this->setAttribute(
      Qt::WA_TranslucentBackground); // 可选：背景透明支持（如果需要圆角）

  // 主布局
  QVBoxLayout *mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(0, 0, 0, 0); // 确保没有 margin
  mainLayout->setSpacing(0);

  // 整体背景容器 (因为 WA_TranslucentBackground 可能会导致全透明，需要一个底板)
  QWidget *container = new QWidget(this);
  container->setObjectName("MainContainer");
  // 1. 容器底色设为浅灰 (#f0f2f5)
  container->setStyleSheet("#MainContainer { background-color: #f0f2f5; "
                           "border: 1px solid #dcdcdc; }");
  mainLayout->addWidget(container);

  QVBoxLayout *containerLayout = new QVBoxLayout(container);
  containerLayout->setContentsMargins(0, 0, 0, 0);
  containerLayout->setSpacing(0);

  // --- 上部：用户个人信息展示 ---
  m_topPanel = new QWidget(container);
  m_topPanel->setFixedHeight(120);
  // 2. 顶部面板保持白色，以便与灰色的底板区分
  m_topPanel->setStyleSheet(
      "background-color: #ffffff; border-bottom: 1px solid #dcdcdc;");

  QHBoxLayout *mainTopLayout = new QHBoxLayout(m_topPanel);
  mainTopLayout->setContentsMargins(0, 0, 0, 0);

  QHBoxLayout *leftContentLayout = new QHBoxLayout();
  leftContentLayout->setContentsMargins(20, 20, 20, 20);

  // 头像 (简单模拟)
  m_avatarLabel = new QLabel(m_topPanel);
  m_avatarLabel->setFixedSize(60, 60);
  m_avatarLabel->setStyleSheet(
      "background-color: #4a90e2; border-radius: 30px; color: white; "
      "font-weight: bold; qproperty-alignment: AlignCenter; border: none;");
  m_avatarLabel->setText("User"); // 默认文字

  // 用户名
  m_nameLabel = new QLabel("Username", m_topPanel);
  m_nameLabel->setStyleSheet(
      "font-size: 18px; font-weight: bold; color: #333; border: none;");
  m_nameLabel->setWordWrap(true);
  m_nameLabel->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
  m_nameLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
  m_nameLabel->setMinimumWidth(140);

  m_signatureLabel = new QLabel("暂无签名", m_topPanel);
  m_signatureLabel->setStyleSheet(
      "font-size: 12px; color: #8a8a8a; border: none;");
  m_signatureLabel->setWordWrap(true);
  m_signatureLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);
  m_signatureLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
  m_signatureLabel->setMinimumWidth(140);

  QVBoxLayout *nameLayout = new QVBoxLayout();
  nameLayout->setContentsMargins(10, 0, 0, 0);
  nameLayout->setSpacing(4);
  nameLayout->addWidget(m_nameLabel);
  nameLayout->addWidget(m_signatureLabel);

  leftContentLayout->addWidget(m_avatarLabel);
  leftContentLayout->addLayout(nameLayout, 1);

  mainTopLayout->addLayout(leftContentLayout);

  QVBoxLayout *rightBtnLayout = new QVBoxLayout();
  rightBtnLayout->setContentsMargins(0, 0, 0, 0);
  rightBtnLayout->setSpacing(0);

  QHBoxLayout *btnRowLayout = new QHBoxLayout();
  btnRowLayout->setContentsMargins(0, 0, 0, 0);
  btnRowLayout->setSpacing(0);

  // 设置、最小化和关闭按钮
  QPushButton *settingsBtn = new QPushButton("设", m_topPanel);
  QPushButton *minBtn = new QPushButton("-", m_topPanel);
  QPushButton *closeBtn = new QPushButton("x", m_topPanel);

  btnRowLayout->addWidget(settingsBtn);
  btnRowLayout->addWidget(minBtn);
  btnRowLayout->addWidget(closeBtn);

  rightBtnLayout->addLayout(btnRowLayout);
  rightBtnLayout->addStretch();

  QHBoxLayout *friendBtnLayout = new QHBoxLayout();
  friendBtnLayout->setContentsMargins(0, 0, 0, 0);
  friendBtnLayout->setSpacing(8);
  friendBtnLayout->addStretch();

  QPushButton *addFriendBtn = new QPushButton("添加好友", m_topPanel);
  QPushButton *removeFriendBtn = new QPushButton("删除好友", m_topPanel);
  addFriendBtn->setFixedHeight(28);
  removeFriendBtn->setFixedHeight(28);
  addFriendBtn->setCursor(Qt::ArrowCursor);
  removeFriendBtn->setCursor(Qt::ArrowCursor);
  addFriendBtn->setStyleSheet(
      "QPushButton { border: 1px solid #d0d0d0; border-radius: 4px; "
      "background-color: #ffffff; color: #333333; padding: 0 10px; }"
      "QPushButton:hover { background-color: #f5f5f5; }");
  removeFriendBtn->setStyleSheet(
      "QPushButton { border: 1px solid #d0d0d0; border-radius: 4px; "
      "background-color: #ffffff; color: #333333; padding: 0 10px; }"
      "QPushButton:hover { background-color: #f5f5f5; }");
  friendBtnLayout->addWidget(addFriendBtn);
  friendBtnLayout->addWidget(removeFriendBtn);
  m_addFriendBtn = addFriendBtn;
  rightBtnLayout->addLayout(friendBtnLayout);
  mainTopLayout->addLayout(rightBtnLayout);

  // 样式：悬浮时背景变灰/红
  QString baseBtnStyle =
      "QPushButton { border: none; font-weight: bold; color: #555; font-size: "
      "16px; background: transparent; }";

  settingsBtn->setFixedSize(40, 30);
  settingsBtn->setStyleSheet(
      baseBtnStyle +
      "QPushButton:hover { background-color: #e0e0e0; color: #000; }");
  settingsBtn->setCursor(Qt::ArrowCursor);

  minBtn->setFixedSize(40, 30); // 稍微宽一点
  minBtn->setStyleSheet(
      baseBtnStyle +
      "QPushButton:hover { background-color: #e0e0e0; color: #000; }");
  minBtn->setCursor(Qt::ArrowCursor); // 标题栏按钮通常是箭头光标

  closeBtn->setFixedSize(40, 30);
  closeBtn->setStyleSheet(baseBtnStyle +
                          "QPushButton:hover { background-color: #ff4d4d; "
                          "color: white; }"); // 关闭按钮悬浮通常变红
  closeBtn->setCursor(Qt::ArrowCursor);

  connect(settingsBtn, &QPushButton::clicked, this, &Widget::onOpenSettings);
  connect(addFriendBtn, &QPushButton::clicked, this, &Widget::onOpenAddFriend);
  connect(removeFriendBtn, &QPushButton::clicked, this,
          &Widget::onOpenDeleteFriend);
  connect(minBtn, &QPushButton::clicked, this, &QWidget::showMinimized);
  connect(closeBtn, &QPushButton::clicked, this, &QWidget::close);

  // --- 中下部：会话列表 ---
  m_sessionList = new QListWidget(container);
  m_sessionList->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
  m_sessionList->setFrameShape(QFrame::NoFrame); // 无边框
  m_sessionList->setVerticalScrollBarPolicy(
      Qt::ScrollBarAlwaysOn); // 滚动条常驻
  m_sessionList->setStyleSheet(
      "QListWidget { background-color: #ffffff; color: #000000; border: "
      "none; "
      "margin: 10px; border-radius: 1px; outline: none; }"
      "QListWidget::item { height: 70px; border-bottom: 1px solid #e0e0e0; "
      "padding: 10px; color: #000000; outline: none; }"
      "QListWidget::item:selected { background-color: #d0d0d0; color: "
      "#000000; "
      "}"
      "QListWidget::item:hover { background-color: #f0f0f0; color: "
      "#000000; "
      "}"
      "QScrollBar:vertical { border: none; background: #f7f7f7; width: "
      "12px; "
      "margin: 0px; border-radius: 6px; }"
      "QScrollBar::handle:vertical { background: #c1c1c1; border-radius: "
      "6px; "
      "min-height: 20px; }"
      "QScrollBar::handle:vertical:hover { background: #a8a8a8; }"
      "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { "
      "height: "
      "0px; }"
      "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { "
      "background: none; }");

  // 添加到容器布局
  containerLayout->addWidget(m_topPanel);
  containerLayout->addWidget(m_sessionList);

  connect(m_sessionList, &QListWidget::itemDoubleClicked, this,
          &Widget::onSessionDoubleClicked);
}

void Widget::initAvatarHttpClient() {
  if (m_avatarNetworkManager) {
    return;
  }

  m_avatarNetworkManager = new QNetworkAccessManager(this);
  m_avatarDiskCache = new QNetworkDiskCache(this);
  const QString appDataPath =
      QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  const QString cachePath = appDataPath + "/http_cache/avatar";
  QDir().mkpath(cachePath);
  m_avatarDiskCache->setCacheDirectory(cachePath);
  m_avatarDiskCache->setMaximumCacheSize(50 * 1024 * 1024); // 50MB
  m_avatarNetworkManager->setCache(m_avatarDiskCache);

  connect(m_avatarNetworkManager, &QNetworkAccessManager::finished, this,
          &Widget::onAvatarReplyFinished);
}

QUrl Widget::resolveAvatarUrl(const QString &avatarUrl) const {
  const QString trimmed = avatarUrl.trimmed();
  if (trimmed.isEmpty()) {
    return QUrl();
  }

  if (trimmed.startsWith("http://") || trimmed.startsWith("https://")) {
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
  if (staticPath.startsWith("/static/")) {
    // Use as-is.
  } else if (staticPath.startsWith("static/")) {
    staticPath.prepend('/');
  } else {
    return QUrl();
  }

  bool ok = false;
  int staticPort = qEnvironmentVariableIntValue(kStaticPortEnv, &ok);
  if (!ok || staticPort <= 0 || staticPort > 65535) {
    staticPort = kDefaultStaticPort;
  }

  const QString host = resolveServerHost();

  QUrl url;
  url.setScheme("http");
  url.setHost(host);
  url.setPort(staticPort);
  url.setPath(staticPath);
  return url;
}

void Widget::requestAvatarImage(const QString &avatarUrl) {
  if (!m_avatarNetworkManager) {
    applyDefaultAvatar();
    return;
  }

  const QUrl url = resolveAvatarUrl(avatarUrl);
  if (!url.isValid()) {
    qWarning() << "Avatar URL invalid, fallback to default avatar:" << avatarUrl;
    applyDefaultAvatar();
    return;
  }

  QNetworkRequest request(url);
  request.setAttribute(QNetworkRequest::CacheLoadControlAttribute,
                       QNetworkRequest::AlwaysNetwork);
  request.setAttribute(QNetworkRequest::CacheSaveControlAttribute, false);
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                       QNetworkRequest::NoLessSafeRedirectPolicy);
  request.setTransferTimeout(8000);

  QNetworkReply *reply = m_avatarNetworkManager->get(request);
  reply->setProperty("requested_avatar_url", avatarUrl.trimmed());
}

void Widget::applyAvatarPixmap(const QPixmap &pixmap) {
  if (pixmap.isNull() || !m_avatarLabel) {
    applyDefaultAvatar();
    return;
  }

  const QSize targetSize = m_avatarLabel->size();
  const int side = qMin(targetSize.width(), targetSize.height());
  const QPixmap scaled =
      pixmap.scaled(side, side, Qt::KeepAspectRatioByExpanding,
                    Qt::SmoothTransformation);

  QPixmap circular(side, side);
  circular.fill(Qt::transparent);
  QPainter painter(&circular);
  painter.setRenderHint(QPainter::Antialiasing, true);
  QPainterPath clipPath;
  clipPath.addEllipse(0, 0, side, side);
  painter.setClipPath(clipPath);
  painter.drawPixmap(0, 0, scaled);
  painter.end();

  m_avatarLabel->setPixmap(circular);
  m_avatarLabel->setText(QString());
}

void Widget::applyDefaultAvatar() {
  if (!m_avatarLabel) {
    return;
  }
  m_avatarLabel->setPixmap(QPixmap());
  if (!m_currentDisplayName.isEmpty()) {
    m_avatarLabel->setText(m_currentDisplayName.left(1).toUpper());
  } else {
    m_avatarLabel->setText("U");
  }
}

void Widget::setUserInfo(const QString &username, const QString &avatarPath,
                         const QString &signature) {
  m_currentDisplayName = username;
  m_currentSignature = signature.trimmed();
  m_currentAvatarUrl = avatarPath.trimmed();
  m_nameLabel->setText(username);
  m_signatureLabel->setText(m_currentSignature.isEmpty() ? "暂无签名"
                                                        : m_currentSignature);
  if (m_currentAvatarUrl.isEmpty()) {
    applyDefaultAvatar();
    return;
  }
  requestAvatarImage(m_currentAvatarUrl);
}

void Widget::setCurrentUserId(const QString &userId) { m_currentUserId = userId; }

void Widget::setCurrentUserNumericId(const QString &numericId) {
  m_currentUserNumericId = numericId.trimmed();
  if (m_friendListRefreshTimer) {
    if (m_currentUserNumericId.isEmpty()) {
      m_friendListRefreshTimer->stop();
    } else if (!m_friendListRefreshTimer->isActive()) {
      m_friendListRefreshTimer->start();
    }
  }
  requestFriendList();
}

void Widget::setProfileApiClient(ProfileApiClient *profileApiClient) {
  m_profileApiClient = profileApiClient;
  if (!m_profileApiClient) {
    return;
  }
  connect(m_profileApiClient, &ProfileApiClient::friendListPayloadReceived, this,
          &Widget::onFriendListPayloadReceived, Qt::UniqueConnection);
  connect(m_profileApiClient, &ProfileApiClient::friendListFailed, this,
          &Widget::onFriendListFailed, Qt::UniqueConnection);
  if (m_friendListRefreshTimer && !m_currentUserNumericId.trimmed().isEmpty() &&
      !m_friendListRefreshTimer->isActive()) {
    m_friendListRefreshTimer->start();
  }
  requestFriendList();
}

// --- 拖拽窗口支持 ---
void Widget::mousePressEvent(QMouseEvent *event) {
  if (event->button() == Qt::LeftButton) {
    m_isDragging = true;
    m_dragPosition =
        event->globalPosition().toPoint() - frameGeometry().topLeft();
    event->accept();
  }
}

void Widget::mouseMoveEvent(QMouseEvent *event) {
  if (m_isDragging && (event->buttons() & Qt::LeftButton)) {
    move(event->globalPosition().toPoint() - m_dragPosition);
    event->accept();
  }
}

void Widget::mouseReleaseEvent(QMouseEvent *event) {
  if (event->button() == Qt::LeftButton) {
    m_isDragging = false;
    event->accept();
  }
}

void Widget::onSessionDoubleClicked(QListWidgetItem *item) {
  if (!item)
    return;
  const QString sessionId = item->data(Qt::UserRole).toString();
  const Session session = m_sessionsById.value(sessionId);
  if (!session.isValid())
    return;

  SessionWindow *sessionWindow = new SessionWindow(session);
  sessionWindow->show();
}

void Widget::addSessionItem(const Session &session) {
  if (!session.isValid() || !m_sessionList)
    return;

  m_sessionsById.insert(session.id(), session);
  QListWidgetItem *item = new QListWidgetItem(session.displayName());
  item->setData(Qt::UserRole, session.id());
  item->setData(Qt::UserRole + 1,
                session.type() == Session::Type::Group ? "group" : "direct");
  item->setIcon(QIcon(":/resources/chat_icon.png"));
  m_sessionList->addItem(item);
}

void Widget::onOpenSettings() {
  static const QRegularExpression kUnsignedIntRe(QStringLiteral("^\\d+$"));
  if (!kUnsignedIntRe.match(m_currentUserId.trimmed()).hasMatch()) {
    QMessageBox::warning(this, "无法打开设置",
                         "当前用户ID无效，无法加载个人资料设置。");
    return;
  }
  if (!m_profileApiClient) {
    QMessageBox::warning(this, "无法打开设置", "Profile 服务未初始化。");
    return;
  }

  if (m_settingsWindow) {
    if (m_settingsWindow->isMinimized()) {
      m_settingsWindow->showNormal();
    } else {
      m_settingsWindow->show();
    }
    m_settingsWindow->raise();
    m_settingsWindow->activateWindow();
    return;
  }

  m_settingsWindow =
      new SettingsWindow(m_currentUserId.trimmed(), m_profileApiClient, nullptr);
  connect(m_settingsWindow, &SettingsWindow::profileApplied, this,
          [this](const QString &displayName, const QString &avatarUrl,
                 const QString &signature) {
            qInfo() << "[MainWidget] apply profile from settings, display_name="
                    << displayName << "avatar_url=" << avatarUrl;
            setUserInfo(displayName, avatarUrl, signature);
          });
  connect(m_settingsWindow, &SettingsWindow::logoutRequested, this, [this]() {
    if (m_addFriendDialog) {
      m_addFriendDialog->close();
    }
    if (m_deleteFriendDialog) {
      m_deleteFriendDialog->close();
    }
    m_currentUserId.clear();
    m_currentUserNumericId.clear();
    m_currentDisplayName.clear();
    m_currentSignature.clear();
    m_currentAvatarUrl.clear();
    m_pendingFriendListRequestId.clear();
    if (m_friendListRefreshTimer) {
      m_friendListRefreshTimer->stop();
    }
    m_friendListManager.clear();
    refreshFriendListUi();
    emit logoutRequested();
    close();
  });
  connect(m_settingsWindow, &QObject::destroyed, this,
          [this]() { m_settingsWindow = nullptr; });
  m_settingsWindow->show();
  m_settingsWindow->raise();
  m_settingsWindow->activateWindow();
}

void Widget::onAvatarReplyFinished(QNetworkReply *reply) {
  if (!reply) {
    return;
  }

  const QString requestedAvatarUrl =
      reply->property("requested_avatar_url").toString().trimmed();
  const bool isLatestRequest = (requestedAvatarUrl == m_currentAvatarUrl);
  if (!isLatestRequest) {
    reply->deleteLater();
    return;
  }

  const QVariant statusCode =
      reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
  const int httpCode = statusCode.isValid() ? statusCode.toInt() : 0;

  if (reply->error() != QNetworkReply::NoError || httpCode != 200) {
    qWarning() << "Avatar download failed, url=" << reply->url().toString()
               << "http_code=" << httpCode << "error=" << reply->errorString();
    applyDefaultAvatar();
    reply->deleteLater();
    return;
  }

  QPixmap pixmap;
  if (!pixmap.loadFromData(reply->readAll())) {
    qWarning() << "Avatar decode failed, url=" << reply->url().toString();
    applyDefaultAvatar();
    reply->deleteLater();
    return;
  }

  applyAvatarPixmap(pixmap);
  reply->deleteLater();
}

void Widget::onOpenAddFriend() {
  if (!m_profileApiClient) {
    QMessageBox::warning(this, "无法添加好友", "Profile 服务未初始化。");
    return;
  }

  if (m_addFriendDialog) {
    if (m_addFriendDialog->isMinimized()) {
      m_addFriendDialog->showNormal();
    } else {
      m_addFriendDialog->show();
    }
    m_addFriendDialog->raise();
    m_addFriendDialog->activateWindow();
    return;
  }

  QString currentNumericId = m_currentUserNumericId.trimmed();
  if (currentNumericId.isEmpty()) {
    currentNumericId = UserSession::instance().numericId().trimmed();
  }
  m_addFriendDialog = new AddFriendDialog(m_currentUserId.trimmed(), currentNumericId,
                                          m_profileApiClient, this);
  connect(m_addFriendDialog, &AddFriendDialog::friendAdded, this,
          [this](const AddFriendResult &) { requestFriendList(true); });
  connect(m_addFriendDialog, &QObject::destroyed, this,
          [this]() { m_addFriendDialog = nullptr; });
  m_addFriendDialog->show();
  m_addFriendDialog->raise();
  m_addFriendDialog->activateWindow();
}

void Widget::onOpenDeleteFriend() {
  if (!m_profileApiClient) {
    QMessageBox::warning(this, "无法删除好友", "Profile 服务未初始化。");
    return;
  }

  QString currentNumericId = m_currentUserNumericId.trimmed();
  if (currentNumericId.isEmpty()) {
    currentNumericId = UserSession::instance().numericId().trimmed();
  }

  static const QRegularExpression kUnsignedIntRe(QStringLiteral("^\\d+$"));
  if (!kUnsignedIntRe.match(currentNumericId).hasMatch()) {
    QMessageBox::warning(this, "无法删除好友", "当前用户编号无效，请重新登录。");
    return;
  }

  if (m_deleteFriendDialog) {
    if (m_deleteFriendDialog->isMinimized()) {
      m_deleteFriendDialog->showNormal();
    } else {
      m_deleteFriendDialog->show();
    }
    m_deleteFriendDialog->raise();
    m_deleteFriendDialog->activateWindow();
    return;
  }

  m_deleteFriendDialog = new DeleteFriendDialog(
      currentNumericId, m_friendListManager.friends(), m_profileApiClient, this);
  connect(m_deleteFriendDialog, &DeleteFriendDialog::friendDeleted, this,
          [this](const DeleteFriendResult &) { requestFriendList(true); });
  connect(m_deleteFriendDialog, &QObject::destroyed, this,
          [this]() { m_deleteFriendDialog = nullptr; });
  m_deleteFriendDialog->show();
  m_deleteFriendDialog->raise();
  m_deleteFriendDialog->activateWindow();
}

void Widget::requestFriendList(bool force) {
  static const QRegularExpression kUnsignedIntRe(QStringLiteral("^\\d+$"));
  QString numericId = m_currentUserNumericId.trimmed();
  if (!kUnsignedIntRe.match(numericId).hasMatch()) {
    const QString sessionNumericId = UserSession::instance().numericId().trimmed();
    if (kUnsignedIntRe.match(sessionNumericId).hasMatch()) {
      m_currentUserNumericId = sessionNumericId;
      numericId = sessionNumericId;
    }
  }

  if (!force && !m_pendingFriendListRequestId.isEmpty()) {
    return;
  }
  if (!m_profileApiClient || numericId.isEmpty() ||
      !kUnsignedIntRe.match(numericId).hasMatch()) {
    return;
  }
  m_pendingFriendListRequestId = m_profileApiClient->fetchFriendList(numericId);
}

void Widget::refreshFriendListUi() {
  if (!m_sessionList) {
    qWarning() << "[MainWidget] refresh friend list skipped: session list is null";
    return;
  }

  m_sessionList->clear();
  m_sessionsById.clear();

  const QList<friendlist::FriendItem> &friends = m_friendListManager.friends();
  if (friends.isEmpty()) {
    auto *emptyItem = new QListWidgetItem(QStringLiteral("暂无好友"));
    emptyItem->setFlags(emptyItem->flags() & ~Qt::ItemIsSelectable &
                        ~Qt::ItemIsEnabled);
    m_sessionList->addItem(emptyItem);
    return;
  }

  for (const friendlist::FriendItem &friendItem : friends) {
    const Session session =
        Session::create(friendItem.displayName, Session::Type::Direct);
    m_sessionsById.insert(session.id(), session);

    const QString itemText =
        QStringLiteral("%1 (%2) [%3]")
            .arg(friendItem.displayName, friendItem.numericId,
                 friendStatusText(friendItem.status));
    auto *item = new QListWidgetItem(itemText);
    item->setData(Qt::UserRole, session.id());
    item->setData(Qt::UserRole + 1, QStringLiteral("direct"));
    item->setData(Qt::UserRole + 2, friendItem.userId);
    item->setData(Qt::UserRole + 3, friendItem.numericId);
    item->setData(Qt::UserRole + 4, friendItem.status);
    item->setIcon(QIcon(":/resources/chat_icon.png"));
    m_sessionList->addItem(item);
  }
}

void Widget::syncFriendListToDeleteDialog() {
  if (m_deleteFriendDialog) {
    m_deleteFriendDialog->setFriends(m_friendListManager.friends());
  }
}

void Widget::onFriendListPayloadReceived(const QString &requestId,
                                         const QJsonObject &data) {
  if (!m_pendingFriendListRequestId.isEmpty() &&
      requestId != m_pendingFriendListRequestId) {
    return;
  }
  m_pendingFriendListRequestId.clear();
  if (!m_friendListManager.updateFromResponse(data)) {
    qWarning() << "[MainWidget] failed to parse friend list payload";
    return;
  }
  refreshFriendListUi();
  syncFriendListToDeleteDialog();
}

void Widget::onFriendListFailed(const QString &requestId, int code,
                                const QString &message) {
  if (!m_pendingFriendListRequestId.isEmpty() &&
      requestId != m_pendingFriendListRequestId) {
    return;
  }
  m_pendingFriendListRequestId.clear();
  qWarning() << "[MainWidget] friend list request failed, code=" << code
             << "message=" << message;
  m_friendListManager.clear();
  refreshFriendListUi();
  syncFriendListToDeleteDialog();
}
