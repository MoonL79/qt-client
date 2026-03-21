#include "widget.h"

#include "addfrienddialog.h"
#include "creategroupdialog.h"
#include "deletefrienddialog.h"
#include "protocol.h"
#include "settingswindow.h"
#include "sessionwindow.h"
#include "ui_widget.h"
#include "usersession.h"
#include "websocketclient.h"

#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMenu>
#include <QMessageBox>
#include <QNetworkRequest>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QRegularExpression>
#include <QSet>
#include <QStyle>
#include <QStandardPaths>
#include <QToolButton>
#include <QTabBar>
#include <QtGlobal>

namespace {
constexpr int kDefaultStaticPort = 18080;
constexpr int kConversationListRefreshIntervalMs = 10 * 1000;
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
    return QStringLiteral("账号正常");
  case 0:
    return QStringLiteral("账号停用");
  default:
    return QStringLiteral("账号状态:%1").arg(status);
  }
}

QString friendOnlineText(bool isOnline) {
  return isOnline ? QStringLiteral("在线") : QStringLiteral("离线");
}

QString friendPresenceText(bool isOnline, const QString &lastSeenAtUtc) {
  if (isOnline) {
    return QStringLiteral("在线");
  }
  const QString trimmed = lastSeenAtUtc.trimmed();
  if (trimmed.isEmpty()) {
    return QStringLiteral("离线");
  }
  return QStringLiteral("离线 · 最近在线 %1").arg(trimmed);
}

constexpr int kRoleSessionId = Qt::UserRole;
constexpr int kRoleSessionType = Qt::UserRole + 1;
constexpr int kRoleUserId = Qt::UserRole + 2;
constexpr int kRoleNumericId = Qt::UserRole + 3;
constexpr int kRoleUserStatus = Qt::UserRole + 4;
constexpr int kRoleIsOnline = Qt::UserRole + 5;
constexpr int kRoleLastSeenAtUtc = Qt::UserRole + 6;
constexpr int kRoleConversationId = Qt::UserRole + 7;
constexpr int kRoleLastPreview = Qt::UserRole + 8;
constexpr int kRoleUnreadCount = Qt::UserRole + 9;
constexpr int kRoleAvatarUrl = Qt::UserRole + 10;
constexpr int kRoleDisplayName = Qt::UserRole + 11;
}

Widget::Widget(QWidget *parent)
    : QWidget(parent), ui(new Ui::Widget), m_isDragging(false) {
  initUI();
  initAvatarHttpClient();

  m_conversationListRefreshTimer = new QTimer(this);
  m_conversationListRefreshTimer->setInterval(kConversationListRefreshIntervalMs);
  connect(m_conversationListRefreshTimer, &QTimer::timeout, this,
          [this]() { requestConversationList(false); });
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

  QHBoxLayout *quickActionLayout = new QHBoxLayout();
  quickActionLayout->setContentsMargins(0, 0, 5, 5);
  quickActionLayout->setSpacing(8);
  quickActionLayout->addStretch();

  auto *quickActionBtn = new QToolButton(m_topPanel);
  quickActionBtn->setText("+");
  quickActionBtn->setFixedSize(28, 28);
  quickActionBtn->setPopupMode(QToolButton::InstantPopup);
  quickActionBtn->setCursor(Qt::ArrowCursor);
  quickActionBtn->setStyleSheet(
      "QToolButton { border: 1px solid #d0d0d0; border-radius: 14px; "
      "background-color: #ffffff; color: #333333; font-size: 18px; "
      "font-weight: bold; }"
      "QToolButton:hover { background-color: #f5f5f5; }"
      "QToolButton::menu-indicator { image: none; width: 0px; }");

  auto *quickActionMenu = new QMenu(quickActionBtn);
  quickActionMenu->setStyleSheet(
      "QMenu { background: #ffffff; border: 1px solid #d9d9d9; padding: 6px 0; }"
      "QMenu::item { padding: 8px 18px; color: #222222; }"
      "QMenu::item:selected { background: #f0f0f0; }");
  QAction *addFriendAction = quickActionMenu->addAction(QStringLiteral("添加好友"));
  QAction *deleteFriendAction =
      quickActionMenu->addAction(QStringLiteral("删除好友"));
  quickActionMenu->addSeparator();
  QAction *createGroupAction =
      quickActionMenu->addAction(QStringLiteral("创建群聊"));
  QAction *joinGroupAction = quickActionMenu->addAction(QStringLiteral("加入群聊"));
  quickActionBtn->setMenu(quickActionMenu);

  connect(addFriendAction, &QAction::triggered, this, &Widget::onOpenAddFriend);
  connect(deleteFriendAction, &QAction::triggered, this,
          &Widget::onOpenDeleteFriend);
  connect(createGroupAction, &QAction::triggered, this, [this]() {
    onOpenCreateGroup();
  });
  connect(joinGroupAction, &QAction::triggered, this, [this]() {
    QMessageBox::information(this, QStringLiteral("加入群聊"),
                             QStringLiteral("加入群聊功能暂未实现。"));
  });

  quickActionLayout->addWidget(quickActionBtn);
  rightBtnLayout->addLayout(quickActionLayout);
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
  connect(minBtn, &QPushButton::clicked, this, &QWidget::showMinimized);
  connect(closeBtn, &QPushButton::clicked, this, &QWidget::close);

  // --- 中下部：标签页 ---
  m_tabWidget = new QTabWidget(container);
  m_tabWidget->setDocumentMode(true);
  m_tabWidget->tabBar()->setExpanding(true);
  m_tabWidget->tabBar()->setUsesScrollButtons(false);
  m_tabWidget->setStyleSheet(
      "QTabWidget::pane { border: none; background: transparent; }"
      "QTabBar::tab { background: #e9ecef; color: #333333; padding: 8px 0; "
      "margin: 10px 0 0 0; border-top-left-radius: 6px; "
      "border-top-right-radius: 6px; }"
      "QTabBar::tab:selected { background: #ffffff; font-weight: bold; }"
      "QTabBar::tab:hover { background: #f5f5f5; }");

  const QString listWidgetStyle =
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
      "background: none; }";

  m_sessionList = new QListWidget(m_tabWidget);
  m_sessionList->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
  m_sessionList->setFrameShape(QFrame::NoFrame);
  m_sessionList->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
  m_sessionList->setStyleSheet(listWidgetStyle);

  m_contactList = new QListWidget(m_tabWidget);
  m_contactList->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
  m_contactList->setFrameShape(QFrame::NoFrame);
  m_contactList->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
  m_contactList->setStyleSheet(listWidgetStyle);

  m_groupList = new QListWidget(m_tabWidget);
  m_groupList->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
  m_groupList->setFrameShape(QFrame::NoFrame);
  m_groupList->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
  m_groupList->setStyleSheet(listWidgetStyle);

  m_tabWidget->addTab(m_sessionList, QStringLiteral("会话"));
  m_tabWidget->addTab(m_contactList, QStringLiteral("联系人"));
  m_tabWidget->addTab(m_groupList, QStringLiteral("群聊"));

  // 添加到容器布局
  containerLayout->addWidget(m_topPanel);
  containerLayout->addWidget(m_tabWidget);

  connect(m_sessionList, &QListWidget::itemDoubleClicked, this,
          &Widget::onSessionDoubleClicked);
  connect(m_groupList, &QListWidget::itemDoubleClicked, this,
          &Widget::onSessionDoubleClicked);

  connect(websocketclient::instance(), &websocketclient::textMessageReceived,
          this, [this](const QString &message) {
            handleIncomingRealtimePayload(message, QStringLiteral("文本"));
          });
  connect(websocketclient::instance(), &websocketclient::binaryMessageReceived,
          this, [this](const QByteArray &data) {
            handleIncomingRealtimePayload(QString::fromUtf8(data),
                                          QStringLiteral("二进制"));
          });
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
  if (m_conversationListRefreshTimer) {
    if (m_currentUserNumericId.isEmpty()) {
      m_conversationListRefreshTimer->stop();
    } else if (!m_conversationListRefreshTimer->isActive()) {
      m_conversationListRefreshTimer->start();
    }
  }
  requestConversationList();
  requestFriendListForContacts();
}

void Widget::setProfileApiClient(ProfileApiClient *profileApiClient) {
  m_profileApiClient = profileApiClient;
  if (!m_profileApiClient) {
    return;
  }
  connect(m_profileApiClient, &ProfileApiClient::conversationListPayloadReceived,
          this, &Widget::onConversationListPayloadReceived,
          Qt::UniqueConnection);
  connect(m_profileApiClient, &ProfileApiClient::conversationListFailed, this,
          &Widget::onConversationListFailed, Qt::UniqueConnection);
  connect(m_profileApiClient, &ProfileApiClient::friendListPayloadReceived, this,
          &Widget::onFriendListPayloadReceived, Qt::UniqueConnection);
  connect(m_profileApiClient, &ProfileApiClient::friendListFailed, this,
          &Widget::onFriendListFailed, Qt::UniqueConnection);
  if (m_conversationListRefreshTimer &&
      !m_currentUserNumericId.trimmed().isEmpty() &&
      !m_conversationListRefreshTimer->isActive()) {
    m_conversationListRefreshTimer->start();
  }
  requestConversationList();
  requestFriendListForContacts();
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
  const QString sessionId = item->data(kRoleSessionId).toString();
  const Session session = m_sessionsById.value(sessionId);
  if (!session.isValid())
    return;

  const QString peerUserId = item->data(kRoleUserId).toString().trimmed();
  const QString peerNumericId = item->data(kRoleNumericId).toString().trimmed();
  const QString conversationId =
      item->data(kRoleConversationId).toString().trimmed();
  SessionWindow *sessionWindow = nullptr;
  if (!conversationId.isEmpty()) {
    sessionWindow = m_sessionWindowsByConversationId.value(conversationId);
  }
  if (!sessionWindow && !peerUserId.isEmpty()) {
    sessionWindow = m_sessionWindowsByUserId.value(peerUserId);
  }
  if (!sessionWindow && !peerNumericId.isEmpty()) {
    sessionWindow = m_sessionWindowsByNumericId.value(peerNumericId);
  }

  if (sessionWindow) {
    if (sessionWindow->isMinimized()) {
      sessionWindow->showNormal();
    } else {
      sessionWindow->show();
    }
    sessionWindow->raise();
    sessionWindow->activateWindow();
    qInfo().noquote() << "[MainWidget] reuse session window peer_user_id="
                      << peerUserId << "peer_numeric_id=" << peerNumericId;
    return;
  }

  sessionWindow = new SessionWindow(session);
  sessionWindow->setPeerIdentity(peerUserId, peerNumericId);
  sessionWindow->updatePeerPresence(item->data(kRoleIsOnline).toBool(),
                                    item->data(kRoleLastSeenAtUtc).toString());
  if (!peerUserId.isEmpty()) {
    m_sessionWindowsByUserId.insert(peerUserId, sessionWindow);
  }
  if (!peerNumericId.isEmpty()) {
    m_sessionWindowsByNumericId.insert(peerNumericId, sessionWindow);
  }
  if (!conversationId.isEmpty()) {
    m_sessionWindowsByConversationId.insert(conversationId, sessionWindow);
  }
  connect(sessionWindow, &SessionWindow::outgoingMessageSubmitted, this,
          [this](const QString &conversationId, const QString &previewText) {
            if (conversationId.isEmpty()) {
              return;
            }
            ConversationListState state =
                m_conversationStatesByConversationId.value(conversationId);
            state.conversationId = conversationId;
            state.lastMessagePreview = previewText.trimmed();
            state.unreadCount = 0;
            m_conversationStatesByConversationId.insert(conversationId, state);
            if (QListWidgetItem *listItem =
                    findConversationItemByConversationId(conversationId)) {
              applyConversationStateToItem(listItem, state, nullptr);
            }
          });
  connect(sessionWindow, &QObject::destroyed, this,
          [this, peerUserId, peerNumericId, conversationId]() {
            if (!peerUserId.isEmpty()) {
              m_sessionWindowsByUserId.remove(peerUserId);
            }
            if (!peerNumericId.isEmpty()) {
              m_sessionWindowsByNumericId.remove(peerNumericId);
            }
            if (!conversationId.isEmpty()) {
              m_sessionWindowsByConversationId.remove(conversationId);
            }
          });
  resetConversationUnread(conversationId);
  sessionWindow->show();
}

void Widget::addSessionItem(const Session &session) {
  if (!session.isValid() || !m_sessionList)
    return;

  m_sessionsById.insert(session.id(), session);
  QListWidgetItem *item = new QListWidgetItem(session.displayName());
  item->setData(kRoleSessionId, session.id());
  item->setData(kRoleSessionType,
                session.type() == Session::Type::Group ? "group" : "direct");
  item->setData(kRoleConversationId, session.conversationId());
  item->setIcon(conversationIcon(session.type() == Session::Type::Group ? 2 : 1));
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
    if (m_createGroupDialog) {
      m_createGroupDialog->close();
    }
    m_currentUserId.clear();
    m_currentUserNumericId.clear();
    m_currentDisplayName.clear();
    m_currentSignature.clear();
    m_currentAvatarUrl.clear();
    m_pendingConversationListRequestId.clear();
    m_pendingFriendListRequestId.clear();
    m_pendingOpenConversationId.clear();
    if (m_conversationListRefreshTimer) {
      m_conversationListRefreshTimer->stop();
    }
    m_conversationListManager.clear();
    m_friendListManager.clear();
    m_sessionWindowsByUserId.clear();
    m_sessionWindowsByNumericId.clear();
    m_sessionWindowsByConversationId.clear();
    m_conversationStatesByConversationId.clear();
    refreshConversationListUi();
    refreshGroupListUi();
    refreshContactListUi();
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
          [this](const AddFriendResult &) {
            requestConversationList(true);
            requestFriendListForContacts(true);
          });
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
          [this](const DeleteFriendResult &) {
            requestConversationList(true);
            requestFriendListForContacts(true);
          });
  connect(m_deleteFriendDialog, &QObject::destroyed, this,
          [this]() { m_deleteFriendDialog = nullptr; });
  requestFriendListForContacts(true);
  m_deleteFriendDialog->show();
  m_deleteFriendDialog->raise();
  m_deleteFriendDialog->activateWindow();
}

void Widget::onOpenCreateGroup() {
  if (!m_profileApiClient) {
    QMessageBox::warning(this, QStringLiteral("无法创建群聊"),
                         QStringLiteral("Profile 服务未初始化。"));
    return;
  }

  if (m_createGroupDialog) {
    m_createGroupDialog->setFriends(m_friendListManager.friends());
    if (m_createGroupDialog->isMinimized()) {
      m_createGroupDialog->showNormal();
    } else {
      m_createGroupDialog->show();
    }
    m_createGroupDialog->raise();
    m_createGroupDialog->activateWindow();
    return;
  }

  m_createGroupDialog = new CreateGroupDialog(m_friendListManager.friends(),
                                              m_profileApiClient, this);
  connect(m_createGroupDialog, &CreateGroupDialog::groupCreated, this,
          [this](const CreateGroupResult &result) {
            m_pendingOpenConversationId = result.conversationId.trimmed();
            requestConversationList(true);
          });
  connect(m_createGroupDialog, &QObject::destroyed, this,
          [this]() { m_createGroupDialog = nullptr; });
  requestFriendListForContacts(true);
  m_createGroupDialog->show();
  m_createGroupDialog->raise();
  m_createGroupDialog->activateWindow();
}

void Widget::requestConversationList(bool force) {
  static const QRegularExpression kUnsignedIntRe(QStringLiteral("^\\d+$"));
  QString numericId = m_currentUserNumericId.trimmed();
  if (!kUnsignedIntRe.match(numericId).hasMatch()) {
    const QString sessionNumericId = UserSession::instance().numericId().trimmed();
    if (kUnsignedIntRe.match(sessionNumericId).hasMatch()) {
      m_currentUserNumericId = sessionNumericId;
      numericId = sessionNumericId;
    }
  }

  if (!force && !m_pendingConversationListRequestId.isEmpty()) {
    return;
  }
  if (!m_profileApiClient || numericId.isEmpty() ||
      !kUnsignedIntRe.match(numericId).hasMatch()) {
    return;
  }
  m_pendingConversationListRequestId =
      m_profileApiClient->fetchConversationList(numericId);
}

void Widget::requestFriendListForContacts(bool force) {
  static const QRegularExpression kUnsignedIntRe(QStringLiteral("^\\d+$"));
  QString numericId = m_currentUserNumericId.trimmed();
  if (!kUnsignedIntRe.match(numericId).hasMatch()) {
    const QString sessionNumericId = UserSession::instance().numericId().trimmed();
    if (kUnsignedIntRe.match(sessionNumericId).hasMatch()) {
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

void Widget::refreshConversationListUi() {
  if (!m_sessionList) {
    qWarning() << "[MainWidget] refresh conversation list skipped: session list is null";
    return;
  }

  m_sessionList->clear();
  m_sessionsById.clear();
  QSet<QString> activeConversationIds;

  const QList<conversationlist::ConversationItem> &conversations =
      m_conversationListManager.conversations();
  bool hasConversation = false;
  for (const conversationlist::ConversationItem &conversationItem : conversations) {
    if (!conversationItem.conversationId.trimmed().isEmpty()) {
      activeConversationIds.insert(conversationItem.conversationId.trimmed());
    }
    hasConversation = true;
  }

  for (auto it = m_conversationStatesByConversationId.begin();
       it != m_conversationStatesByConversationId.end();) {
    if (!activeConversationIds.contains(it.key())) {
      it = m_conversationStatesByConversationId.erase(it);
      continue;
    }
    ++it;
  }

  if (!hasConversation) {
    auto *emptyItem = new QListWidgetItem(QStringLiteral("暂无会话"));
    emptyItem->setFlags(emptyItem->flags() & ~Qt::ItemIsSelectable &
                        ~Qt::ItemIsEnabled);
    m_sessionList->addItem(emptyItem);
  }

  for (const conversationlist::ConversationItem &conversationItem : conversations) {
    ConversationListState state =
        m_conversationStatesByConversationId.value(conversationItem.conversationId);
    state.conversationId = conversationItem.conversationId.trimmed();
    state.conversationUuid = conversationItem.conversationUuid.trimmed();
    state.conversationType = conversationItem.conversationType;
    state.displayName = conversationItem.name;
    state.avatarUrl = conversationItem.avatarUrl;
    state.memberCount = conversationItem.memberCount;
    state.peerUserId = conversationItem.peerUserId;
    state.peerNumericId = conversationItem.peerNumericId;
    state.peerUsername = conversationItem.peerUsername;
    state.peerNickname = conversationItem.peerNickname;
    state.peerAvatarUrl = conversationItem.peerAvatarUrl;
    state.peerBio = conversationItem.peerBio;
    state.peerStatus = conversationItem.peerStatus;
    state.peerIsOnline = conversationItem.peerIsOnline;
    state.peerLastSeenAt = conversationItem.peerLastSeenAt;
    state.placeholder = false;
    if (!state.conversationId.isEmpty()) {
      m_conversationStatesByConversationId.insert(state.conversationId, state);
    }
    upsertConversationListItemToList(m_sessionList, state, &conversationItem);
  }
}

void Widget::refreshGroupListUi() {
  if (!m_groupList) {
    qWarning() << "[MainWidget] refresh group list skipped: group list is null";
    return;
  }

  m_groupList->clear();

  const QList<conversationlist::ConversationItem> &conversations =
      m_conversationListManager.conversations();
  bool hasGroupConversation = false;
  for (const conversationlist::ConversationItem &conversationItem : conversations) {
    if (conversationItem.conversationType == 2) {
      hasGroupConversation = true;
      break;
    }
  }
  if (!hasGroupConversation) {
    auto *emptyItem = new QListWidgetItem(QStringLiteral("暂无群聊"));
    emptyItem->setFlags(emptyItem->flags() & ~Qt::ItemIsSelectable &
                        ~Qt::ItemIsEnabled);
    m_groupList->addItem(emptyItem);
  }

  for (const conversationlist::ConversationItem &conversationItem : conversations) {
    if (conversationItem.conversationType != 2) {
      continue;
    }
    ConversationListState state =
        m_conversationStatesByConversationId.value(conversationItem.conversationId);
    state.conversationId = conversationItem.conversationId.trimmed();
    state.conversationUuid = conversationItem.conversationUuid.trimmed();
    state.conversationType = conversationItem.conversationType;
    state.displayName = conversationItem.name;
    state.avatarUrl = conversationItem.avatarUrl;
    state.memberCount = conversationItem.memberCount;
    state.peerUserId = conversationItem.peerUserId;
    state.peerNumericId = conversationItem.peerNumericId;
    state.peerUsername = conversationItem.peerUsername;
    state.peerNickname = conversationItem.peerNickname;
    state.peerAvatarUrl = conversationItem.peerAvatarUrl;
    state.peerBio = conversationItem.peerBio;
    state.peerStatus = conversationItem.peerStatus;
    state.peerIsOnline = conversationItem.peerIsOnline;
    state.peerLastSeenAt = conversationItem.peerLastSeenAt;
    state.placeholder = false;
    if (!state.conversationId.isEmpty()) {
      m_conversationStatesByConversationId.insert(state.conversationId, state);
    }
    upsertConversationListItem(state, &conversationItem);
  }
}

void Widget::refreshContactListUi() {
  if (!m_contactList) {
    qWarning() << "[MainWidget] refresh contact list skipped: contact list is null";
    return;
  }
  // Contacts still depend on LIST_FRIENDS until dedicated contact models are split out.
  friendlist::FriendListManager::refreshListWidget(m_contactList,
                                                   m_friendListManager.friends());
}

void Widget::updateConversationListItem(
    const conversationlist::ConversationItem &conversationItem) {
  if (!m_sessionList && !m_groupList) {
    return;
  }
  QListWidgetItem *item =
      findConversationItemByConversationId(conversationItem.conversationId);
  if (!item) {
    return;
  }

  ConversationListState state =
      m_conversationStatesByConversationId.value(conversationItem.conversationId);
  state.conversationId = conversationItem.conversationId.trimmed();
  state.conversationUuid = conversationItem.conversationUuid.trimmed();
  state.conversationType = conversationItem.conversationType;
  state.displayName = conversationItem.name;
  state.avatarUrl = conversationItem.avatarUrl;
  state.memberCount = conversationItem.memberCount;
  state.peerUserId = conversationItem.peerUserId;
  state.peerNumericId = conversationItem.peerNumericId;
  state.peerUsername = conversationItem.peerUsername;
  state.peerNickname = conversationItem.peerNickname;
  state.peerAvatarUrl = conversationItem.peerAvatarUrl;
  state.peerBio = conversationItem.peerBio;
  state.peerStatus = conversationItem.peerStatus;
  state.peerIsOnline = conversationItem.peerIsOnline;
  state.peerLastSeenAt = conversationItem.peerLastSeenAt;
  state.placeholder = false;
  if (!state.conversationId.isEmpty()) {
    m_conversationStatesByConversationId.insert(state.conversationId, state);
  }
  applyConversationStateToItem(item, state, &conversationItem);
  qInfo().noquote() << "[MainWidget] refreshed conversation list item peer_user_id="
                    << conversationItem.peerUserId << "peer_numeric_id="
                    << conversationItem.peerNumericId << "presence="
                    << friendPresenceText(conversationItem.peerIsOnline,
                                          conversationItem.peerLastSeenAt);
}

void Widget::syncFriendListToDeleteDialog() {
  if (m_deleteFriendDialog) {
    m_deleteFriendDialog->setFriends(m_friendListManager.friends());
  }
  if (m_createGroupDialog) {
    m_createGroupDialog->setFriends(m_friendListManager.friends());
  }
}

void Widget::handleIncomingRealtimePayload(const QString &payload,
                                           const QString &sourceTag) {
  protocol::Envelope envelope;
  QString parseError;
  if (!protocol::parseEnvelope(payload, &envelope, &parseError)) {
    return;
  }

  if (envelope.type == QStringLiteral("MESSAGE") &&
      envelope.action == QStringLiteral("SEND")) {
    handleMessageEnvelope(envelope);
    return;
  }

  if (envelope.type == QStringLiteral("MESSAGE") &&
      envelope.action == QStringLiteral("PRESENCE")) {
    qInfo().noquote() << "[MainWidget] received presence broadcast source="
                      << sourceTag << "payload="
                      << QString::fromUtf8(
                             QJsonDocument(envelope.data)
                                 .toJson(QJsonDocument::Compact));
    handlePresenceEnvelope(envelope.data);
  }
}

void Widget::handleMessageEnvelope(const protocol::Envelope &envelope) {
  if (!envelope.requestId.trimmed().isEmpty()) {
    return;
  }

  const QString conversationId =
      envelope.data.value(QStringLiteral("conversation_id")).toString().trimmed();
  const QString content =
      envelope.data.value(QStringLiteral("content")).toString().trimmed();
  if (conversationId.isEmpty() || content.isEmpty()) {
    qWarning() << "[MainWidget] ignore MESSAGE/SEND push with invalid data"
               << QString::fromUtf8(
                      QJsonDocument(envelope.data).toJson(QJsonDocument::Compact));
    return;
  }

  ConversationListState state =
      m_conversationStatesByConversationId.value(conversationId);
  state.conversationId = conversationId;
  if (state.displayName.isEmpty()) {
    state.displayName =
        envelope.data.value(QStringLiteral("from_username")).toString().trimmed();
    if (state.displayName.isEmpty()) {
      state.displayName = conversationId;
    }
  }
  if (state.peerUserId.isEmpty()) {
    state.peerUserId =
        envelope.data.value(QStringLiteral("from_user_id")).toString().trimmed();
  }
  if (state.peerNumericId.isEmpty()) {
    state.peerNumericId =
        envelope.data.value(QStringLiteral("from_numeric_id")).toString().trimmed();
  }
  state.lastMessagePreview = content;
  state.placeholder = false;

  SessionWindow *openWindow = m_sessionWindowsByConversationId.value(conversationId);
  if (!openWindow) {
    state.unreadCount += 1;
  } else {
    state.unreadCount = 0;
  }
  m_conversationStatesByConversationId.insert(conversationId, state);

  if (QListWidgetItem *item = findConversationItemByConversationId(conversationId)) {
    applyConversationStateToItem(item, state, nullptr);
  } else {
    upsertConversationListItem(state, nullptr);
  }

  qInfo() << "[MainWidget] routed incoming MESSAGE/SEND conversation_id="
          << conversationId << "open_window=" << (openWindow != nullptr)
          << "unread=" << state.unreadCount;
}

void Widget::handlePresenceEnvelope(const QJsonObject &data) {
  const QString userId = data.value(QStringLiteral("user_id")).toString().trimmed();
  const QString numericId =
      data.value(QStringLiteral("numeric_id")).toString().trimmed();
  const QString presenceEvent =
      data.value(QStringLiteral("presence_event")).toString().trimmed().toLower();

  bool isOnline = data.value(QStringLiteral("is_online")).toBool(false);
  if (presenceEvent == QStringLiteral("online")) {
    isOnline = true;
  } else if (presenceEvent == QStringLiteral("offline")) {
    isOnline = false;
  }
  const QString lastSeenAtUtc =
      data.value(QStringLiteral("last_seen_at")).toString().trimmed();

  qInfo().noquote()
      << "[MainWidget] apply presence event user_id=" << userId
      << "numeric_id=" << numericId << "presence_event=" << presenceEvent
      << "is_online=" << isOnline << "last_seen_at=" << lastSeenAtUtc;

  conversationlist::ConversationItem updatedConversation;
  if (!m_conversationListManager.applyPeerPresenceUpdate(
          userId, numericId, isOnline, lastSeenAtUtc, &updatedConversation)) {
    qInfo().noquote()
        << "[MainWidget] ignore presence update: conversation peer not found user_id="
        << userId << "numeric_id=" << numericId;
    return;
  }

  updateConversationListItem(updatedConversation);

  SessionWindow *sessionWindow = nullptr;
  if (!updatedConversation.peerUserId.isEmpty()) {
    sessionWindow =
        m_sessionWindowsByUserId.value(updatedConversation.peerUserId);
  }
  if (!sessionWindow && !updatedConversation.peerNumericId.isEmpty()) {
    sessionWindow =
        m_sessionWindowsByNumericId.value(updatedConversation.peerNumericId);
  }
  if (sessionWindow) {
    sessionWindow->updatePeerPresence(updatedConversation.peerIsOnline,
                                      updatedConversation.peerLastSeenAt);
    qInfo().noquote()
        << "[MainWidget] refreshed open session window user_id="
        << updatedConversation.peerUserId << "numeric_id="
        << updatedConversation.peerNumericId;
  }
}

void Widget::onConversationListPayloadReceived(const QString &requestId,
                                               const QJsonObject &data) {
  if (!m_pendingConversationListRequestId.isEmpty() &&
      requestId != m_pendingConversationListRequestId) {
    return;
  }
  m_pendingConversationListRequestId.clear();
  if (!m_conversationListManager.updateFromResponse(data)) {
    qWarning() << "[MainWidget] failed to parse conversation list payload";
    return;
  }
  refreshConversationListUi();
  refreshGroupListUi();
  if (!m_pendingOpenConversationId.isEmpty()) {
    if (QListWidgetItem *item =
            findConversationItemByConversationId(m_pendingOpenConversationId)) {
      const QString createdConversationId = m_pendingOpenConversationId;
      m_pendingOpenConversationId.clear();
      onSessionDoubleClicked(item);
      qInfo() << "[MainWidget] opened created group conversation_id="
              << createdConversationId;
    }
  }
}

void Widget::onConversationListFailed(const QString &requestId, int code,
                                      const QString &message) {
  if (!m_pendingConversationListRequestId.isEmpty() &&
      requestId != m_pendingConversationListRequestId) {
    return;
  }
  m_pendingConversationListRequestId.clear();
  qWarning() << "[MainWidget] conversation list request failed, code=" << code
             << "message=" << message;
  m_conversationListManager.clear();
  refreshConversationListUi();
  refreshGroupListUi();
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
  refreshContactListUi();
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
  refreshContactListUi();
  syncFriendListToDeleteDialog();
}

QListWidget *Widget::listWidgetForConversationType(int conversationType) const {
  return conversationType == 2 ? m_groupList : m_sessionList;
}

QListWidgetItem *Widget::findConversationItemInList(
    QListWidget *listWidget, const QString &conversationId) const {
  if (!listWidget || conversationId.trimmed().isEmpty()) {
    return nullptr;
  }

  for (int i = 0; i < listWidget->count(); ++i) {
    QListWidgetItem *item = listWidget->item(i);
    if (!item) {
      continue;
    }
    if (item->data(kRoleConversationId).toString().trimmed() ==
        conversationId.trimmed()) {
      return item;
    }
  }
  return nullptr;
}

QListWidgetItem *Widget::findConversationItemByConversationId(
    const QString &conversationId) const {
  if (conversationId.trimmed().isEmpty()) {
    return nullptr;
  }

  const QList<QListWidget *> listWidgets = {m_sessionList, m_groupList};
  for (QListWidget *listWidget : listWidgets) {
    if (QListWidgetItem *item =
            findConversationItemInList(listWidget, conversationId)) {
      return item;
    }
  }
  return nullptr;
}

QListWidgetItem *Widget::upsertConversationListItem(
    const ConversationListState &state,
    const conversationlist::ConversationItem *conversationItem) {
  QListWidget *targetList = listWidgetForConversationType(state.conversationType);
  return upsertConversationListItemToList(targetList, state, conversationItem);
}

QListWidgetItem *Widget::upsertConversationListItemToList(
    QListWidget *targetList, const ConversationListState &state,
    const conversationlist::ConversationItem *conversationItem) {
  if (!targetList) {
    return nullptr;
  }

  if (!state.conversationId.isEmpty()) {
    if (QListWidgetItem *existing =
            findConversationItemInList(targetList, state.conversationId)) {
      applyConversationStateToItem(existing, state, conversationItem);
      return existing;
    }
  }

  const QString displayName =
      state.displayName.isEmpty() ? QStringLiteral("未知会话") : state.displayName;
  const Session::Type sessionType =
      state.conversationType == 2 ? Session::Type::Group : Session::Type::Direct;
  const Session session =
      Session::create(displayName, sessionType, state.conversationId);
  m_sessionsById.insert(session.id(), session);

  auto *item = new QListWidgetItem();
  item->setData(kRoleSessionId, session.id());
  item->setData(kRoleSessionType,
                sessionType == Session::Type::Group ? "group" : "direct");
  item->setIcon(conversationIcon(state.conversationType));
  applyConversationStateToItem(item, state, conversationItem);
  targetList->addItem(item);
  return item;
}

QIcon Widget::conversationIcon(int conversationType) const {
  if (!style()) {
    return QIcon();
  }
  return conversationType == 2
             ? style()->standardIcon(QStyle::SP_DirIcon)
             : style()->standardIcon(QStyle::SP_FileDialogContentsView);
}

void Widget::applyConversationStateToItem(QListWidgetItem *item,
                                          const ConversationListState &state,
                                          const conversationlist::ConversationItem *conversationItem) {
  if (!item) {
    return;
  }

  const QString displayName =
      state.displayName.isEmpty()
          ? item->data(kRoleDisplayName).toString().trimmed()
          : state.displayName;
  const QString numericId = !state.peerNumericId.isEmpty()
                                ? state.peerNumericId
                                : item->data(kRoleNumericId).toString().trimmed();

  bool isOnline = state.peerIsOnline;
  int userStatus = state.peerStatus;
  QString lastSeenAtUtc = item->data(kRoleLastSeenAtUtc).toString();
  QString userId = state.peerUserId;

  if (conversationItem) {
    isOnline = conversationItem->peerIsOnline;
    userStatus = conversationItem->peerStatus;
    lastSeenAtUtc = conversationItem->peerLastSeenAt;
    if (userId.isEmpty()) {
      userId = conversationItem->peerUserId;
    }
  } else if (lastSeenAtUtc.isEmpty()) {
    lastSeenAtUtc = state.peerLastSeenAt;
  }

  QString sessionId = item->data(kRoleSessionId).toString().trimmed();
  if (sessionId.isEmpty() || !m_sessionsById.contains(sessionId)) {
    const Session::Type sessionType =
        state.conversationType == 2 ? Session::Type::Group : Session::Type::Direct;
    const Session session =
        Session::create(displayName.isEmpty() ? QStringLiteral("未知会话")
                                              : displayName,
                        sessionType, state.conversationId);
    sessionId = session.id();
    m_sessionsById.insert(sessionId, session);
    item->setData(kRoleSessionId, sessionId);
    item->setData(kRoleSessionType,
                  sessionType == Session::Type::Group ? "group" : "direct");
  }

  item->setText(buildSessionItemText(state.conversationType, displayName, numericId,
                                     isOnline, userStatus,
                                     state.lastMessagePreview, state.memberCount,
                                     state.unreadCount));
  item->setData(kRoleDisplayName, displayName);
  item->setData(kRoleUserId, userId);
  item->setData(kRoleNumericId, numericId);
  item->setData(kRoleUserStatus, userStatus);
  item->setData(kRoleIsOnline, isOnline);
  item->setData(kRoleLastSeenAtUtc, lastSeenAtUtc);
  item->setData(kRoleConversationId, state.conversationId);
  item->setData(kRoleLastPreview, state.lastMessagePreview);
  item->setData(kRoleUnreadCount, state.unreadCount);
  item->setData(kRoleAvatarUrl, state.avatarUrl);
  item->setIcon(conversationIcon(state.conversationType));
  if (state.conversationType == 2) {
    item->setToolTip(state.memberCount > 0
                         ? QStringLiteral("群聊成员数: %1").arg(state.memberCount)
                         : QStringLiteral("群聊"));
  } else {
    item->setToolTip(friendPresenceText(isOnline, lastSeenAtUtc));
  }
}

void Widget::resetConversationUnread(const QString &conversationId) {
  const QString trimmedConversationId = conversationId.trimmed();
  if (trimmedConversationId.isEmpty()) {
    return;
  }

  auto it = m_conversationStatesByConversationId.find(trimmedConversationId);
  if (it == m_conversationStatesByConversationId.end()) {
    return;
  }

  it->unreadCount = 0;
  if (QListWidgetItem *item =
          findConversationItemByConversationId(trimmedConversationId)) {
    applyConversationStateToItem(item, it.value(), nullptr);
  }
}

QString Widget::buildSessionItemText(int conversationType,
                                     const QString &displayName,
                                     const QString &numericId, bool isOnline,
                                     int userStatus, const QString &preview,
                                     int memberCount,
                                     int unreadCount) const {
  QString firstLine;
  QString previewText;
  if (conversationType == 2) {
    firstLine = displayName;
    previewText = preview.trimmed().isEmpty()
                      ? (memberCount > 0 ? QStringLiteral("%1 人").arg(memberCount)
                                         : QStringLiteral("群聊"))
                      : elidePreview(preview);
  } else {
    firstLine = QStringLiteral("%1 (%2) [%3|%4]")
                    .arg(displayName,
                         numericId.isEmpty() ? QStringLiteral("-") : numericId,
                         friendOnlineText(isOnline), friendStatusText(userStatus));
    previewText = preview.trimmed().isEmpty() ? QStringLiteral("暂无消息")
                                              : elidePreview(preview);
  }
  if (unreadCount > 0) {
    firstLine += QStringLiteral("  未读:%1").arg(unreadCount);
  }
  return firstLine + QLatin1Char('\n') + previewText;
}

QString Widget::elidePreview(const QString &preview) const {
  QString singleLine = preview;
  singleLine.replace(QLatin1Char('\n'), QLatin1Char(' '));
  singleLine = singleLine.trimmed();
  if (singleLine.size() > 36) {
    return singleLine.left(36) + QStringLiteral("...");
  }
  return singleLine;
}

