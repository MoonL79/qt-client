#include "widget.h"

#include "addfrienddialog.h"
#include "chatfileservice.h"
#include "creategroupdialog.h"
#include "deletefrienddialog.h"
#include "dismissgroupdialog.h"
#include "leavegroupdialog.h"
#include "localchatstore.h"
#include "messagesyncclient.h"
#include "protocol.h"
#include "searchgroupdialog.h"
#include "settingswindow.h"
#include "sessionwindow.h"
#include "ui_widget.h"
#include "usersession.h"
#include "websocketclient.h"

#include <QAbstractButton>
#include <QDateTime>
#include <QDir>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
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
constexpr qint64 kMaxTransferFileSizeBytes = 20 * 1024 * 1024;
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
 * @brief 生成好友状态显示文本。
 * @param status 状态值。
 * @return 返回处理后的字符串结果。
 */
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

/**
 * @brief 生成好友在线显示文本。
 * @param isOnline 在线状态标记。
 * @return 返回处理后的字符串结果。
 */
QString friendOnlineText(bool isOnline) {
  return isOnline ? QStringLiteral("在线") : QStringLiteral("离线");
}

/**
 * @brief 生成好友在线状态显示文本。
 * @param isOnline 在线状态标记。
 * @param lastSeenAtUtc 最近在线时间字符串。
 * @return 返回处理后的字符串结果。
 */
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
constexpr int kTransferRoleConversationId = Qt::UserRole + 100;
constexpr int kTransferRoleConversationName = Qt::UserRole + 101;
constexpr int kTransferRoleConversationType = Qt::UserRole + 102;

/**
 * @brief 生成文件预览显示文本。
 * @param originalName 字符串参数 `originalName`。
 * @return 返回处理后的字符串结果。
 */
QString filePreviewText(const QString &originalName) {
  const QString trimmed = originalName.trimmed();
  return trimmed.isEmpty() ? QStringLiteral("[文件]") :
                             QStringLiteral("[文件] %1").arg(trimmed);
}

/**
 * @brief 执行uniqueDownloadSavePath的核心逻辑。
 * @param preferredPath 路径相关参数。
 * @return 返回处理后的字符串结果。
 */
QString uniqueDownloadSavePath(const QString &preferredPath) {
  QFileInfo preferredInfo(preferredPath);
  const QString directoryPath =
      preferredInfo.dir().absolutePath().trimmed().isEmpty()
          ? QDir::homePath()
          : preferredInfo.dir().absolutePath().trimmed();
  const QString completeBaseName =
      preferredInfo.completeBaseName().trimmed().isEmpty()
          ? QStringLiteral("download")
          : preferredInfo.completeBaseName().trimmed();
  const QString suffix = preferredInfo.suffix().trimmed();

  QString candidate = QDir(directoryPath).filePath(preferredInfo.fileName());
  if (!QFileInfo::exists(candidate)) {
    return candidate;
  }

  for (int index = 1; index < 1000; ++index) {
    const QString fileName =
        suffix.isEmpty()
            ? QStringLiteral("%1 (%2)").arg(completeBaseName, QString::number(index))
            : QStringLiteral("%1 (%2).%3")
                  .arg(completeBaseName, QString::number(index), suffix);
    candidate = QDir(directoryPath).filePath(fileName);
    if (!QFileInfo::exists(candidate)) {
      return candidate;
    }
  }

  return QDir(directoryPath)
      .filePath(QStringLiteral("%1-%2%3")
                    .arg(completeBaseName,
                         QString::number(QDateTime::currentMSecsSinceEpoch()),
                         suffix.isEmpty() ? QString() : QStringLiteral(".%1").arg(suffix)));
}

/**
 * @brief 执行topPanelStyleSheetForColor的核心逻辑。
 * @param color 颜色值。
 * @return 返回处理后的字符串结果。
 */
QString topPanelStyleSheetForColor(const QColor &color) {
  const QColor resolved = color.isValid() ? color : QColor(QStringLiteral("#ffffff"));
  const QColor border = resolved.darker(112);
  return QStringLiteral(
             "background-color: %1; border-bottom: 1px solid %2; "
             "border-top-left-radius: 8px; border-top-right-radius: 8px;")
      .arg(resolved.name(QColor::HexRgb), border.name(QColor::HexRgb));
}

/**
 * @brief 执行listWidgetStyleSheetForColor的核心逻辑。
 * @param color 颜色值。
 * @return 返回处理后的字符串结果。
 */
QString listWidgetStyleSheetForColor(const QColor &color) {
  const QColor accent = color.isValid() ? color : QColor(QStringLiteral("#4a90e2"));
  const QColor selected = accent.lighter(145);
  const QColor hover = accent.lighter(180);
  return QStringLiteral(
             "QListWidget { background-color: #ffffff; color: #000000; border: none; "
             "margin: 10px; border-radius: 1px; outline: none; }"
             "QListWidget::item { height: 70px; border-bottom: 1px solid #e0e0e0; "
             "padding: 10px; color: #000000; outline: none; }"
             "QListWidget::item:selected { background-color: %1; color: #111827; }"
             "QListWidget::item:hover { background-color: %2; color: #111827; }"
             "QScrollBar:vertical { border: none; background: #f7f7f7; width: 12px; "
             "margin: 0px; border-radius: 6px; }"
             "QScrollBar::handle:vertical { background: #c1c1c1; border-radius: 6px; "
             "min-height: 20px; }"
             "QScrollBar::handle:vertical:hover { background: #a8a8a8; }"
             "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; }"
             "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: none; }")
      .arg(selected.name(QColor::HexRgb), hover.name(QColor::HexRgb));
}

/**
 * @brief 执行mainTabsStyleSheetForColor的核心逻辑。
 * @param color 颜色值。
 * @return 返回处理后的字符串结果。
 */
QString mainTabsStyleSheetForColor(const QColor &color) {
  const QColor accent = color.isValid() ? color : QColor(QStringLiteral("#4a90e2"));
  const QColor selected = accent.lighter(150);
  const QColor hover = accent.lighter(185);
  return QStringLiteral(
             "QTabWidget::pane { border: none; background: transparent; }"
             "QTabBar::tab { background: #e9ecef; color: #333333; padding: 8px 0; "
             "margin: 10px 0 0 0; border-top-left-radius: 6px; border-top-right-radius: 6px; }"
             "QTabBar::tab:selected { background: %1; color: #111827; font-weight: bold; }"
             "QTabBar::tab:hover { background: %2; }")
      .arg(selected.name(QColor::HexRgb), hover.name(QColor::HexRgb));
}

QString topButtonStyleSheetForColor(const QColor &color, bool closeButton = false) {
  const QColor accent = color.isValid() ? color : QColor(QStringLiteral("#4a90e2"));
  const QColor hover = closeButton ? QColor(QStringLiteral("#ff4d4d")) : accent.lighter(150);
  const QColor text = closeButton ? QColor(QStringLiteral("#555555")) : accent.darker(185);
  return QStringLiteral(
             "QPushButton { border: none; font-weight: bold; color: %1; font-size: 16px; "
             "background: transparent; }"
             "QPushButton:hover { background-color: %2; color: %3; %4 }")
      .arg(text.name(QColor::HexRgb), hover.name(QColor::HexRgb),
           closeButton ? QStringLiteral("#ffffff") : QStringLiteral("#111827"),
           closeButton ? QStringLiteral("border-radius: 6px;")
                       : QStringLiteral(""));
}

/**
 * @brief 执行minButtonStyleSheet的核心逻辑。
 * @return 返回处理后的字符串结果。
 */
QString minButtonStyleSheet() {
  return QStringLiteral(
      "QPushButton { border: none; font-weight: bold; color: #374151; font-size: 16px; "
      "background: transparent; }"
      "QPushButton:hover { background-color: #eef2f7; color: #111827; border-radius: 6px; }");
}

/**
 * @brief 执行quickActionButtonStyleSheetForColor的核心逻辑。
 * @param color 颜色值。
 * @return 返回处理后的字符串结果。
 */
QString quickActionButtonStyleSheetForColor(const QColor &color) {
  const QColor accent = color.isValid() ? color : QColor(QStringLiteral("#4a90e2"));
  const QColor border = accent.darker(110);
  const QColor hover = accent.lighter(165);
  return QStringLiteral(
             "QToolButton { border: 1px solid %1; border-radius: 14px; background-color: #ffffff; "
             "color: %2; font-size: 18px; font-weight: bold; }"
             "QToolButton:hover { background-color: %3; }"
             "QToolButton::menu-indicator { image: none; width: 0px; }")
      .arg(border.name(QColor::HexRgb), accent.darker(180).name(QColor::HexRgb),
           hover.name(QColor::HexRgb));
}

/**
 * @brief 执行quickActionMenuStyleSheetForColor的核心逻辑。
 * @param color 颜色值。
 * @return 返回处理后的字符串结果。
 */
QString quickActionMenuStyleSheetForColor(const QColor &color) {
  const QColor accent = color.isValid() ? color : QColor(QStringLiteral("#4a90e2"));
  return QStringLiteral(
             "QMenu { background: #ffffff; border: 1px solid #d9d9d9; padding: 6px 0; }"
             "QMenu::item { padding: 8px 18px; color: #222222; }"
             "QMenu::item:selected { background: %1; }")
      .arg(accent.lighter(175).name(QColor::HexRgb));
}
}

/**
 * @brief 构造并初始化Widget实例。
 * @param parent 父级对象指针，用于管理当前对象的生命周期。
 * @return 无返回值。
 */
Widget::Widget(QWidget *parent)
    : QWidget(parent), ui(new Ui::Widget), m_topPanelThemeColor(QStringLiteral("#ffffff")),
      m_isDragging(false) {
  initUI();
  initAvatarHttpClient();
  m_chatFileService = new ChatFileService(websocketclient::instance(), this);
  m_messageSyncClient = new MessageSyncClient(websocketclient::instance(), this);
  m_localChatStore = new LocalChatStore();

  connect(m_chatFileService, &ChatFileService::uploadFinished, this,
          [this](const QString &requestId, const ChatFileUploadResult &result) {
            if (requestId != m_pendingFileTransfer.uploadRequestId) {
              return;
            }
            m_pendingFileTransfer.uploadRequestId.clear();
            if (!websocketclient::instance()->isConnected()) {
              QMessageBox::warning(this, QStringLiteral("文件发送失败"),
                                   QStringLiteral("文件已上传，但 WebSocket 未连接，无法发送文件消息。"));
              m_pendingFileTransfer.clear();
              return;
            }
            const QString sendRequestId =
                m_chatFileService->sendFileMessage(result.conversationId, result);
            if (sendRequestId.isEmpty()) {
              m_pendingFileTransfer.clear();
              QMessageBox::warning(this, QStringLiteral("文件发送失败"),
                                   QStringLiteral("文件已上传，但文件消息发送失败。"));
              return;
            }
            m_pendingFileTransfer.sendRequestId = sendRequestId;
          });
  connect(m_chatFileService, &ChatFileService::fileMessageSendSucceeded, this,
          [this](const QString &requestId, const ChatMessage &message) {
            if (requestId != m_pendingFileTransfer.sendRequestId) {
              return;
            }
            storeAndRouteMessage(message, false);
            m_pendingFileTransfer.clear();
          });
  connect(m_messageSyncClient, &MessageSyncClient::pullSucceeded, this,
          [this](const QString &requestId, const MessagePullResult &result) {
            if (requestId != m_pendingMessagePullRequestId) {
              return;
            }
            m_pendingMessagePullRequestId.clear();
            if (!m_activeConversationSync.isActive()) {
              return;
            }

            qint64 highestSeq = m_activeConversationSync.localLastSeq;
            bool allStored = true;
            for (const ChatMessage &message : result.messages) {
              allStored = storeAndRouteMessage(message, true) && allStored;
              highestSeq = qMax(highestSeq, message.seq);
            }

            m_activeConversationSync.hasMore = result.hasMore;
            m_activeConversationSync.nextAfterSeq =
                qMax<qint64>(result.nextAfterSeq, highestSeq);
            m_activeConversationSync.serverLastSeq =
                qMax<qint64>(m_activeConversationSync.serverLastSeq,
                             result.serverLastSeq);
            const bool pullMadeProgress =
                highestSeq > m_activeConversationSync.localLastSeq ||
                m_activeConversationSync.nextAfterSeq >
                    m_activeConversationSync.localLastSeq;

            if (!allStored) {
              qWarning() << "[MainWidget] skip ACK because local persistence failed"
                         << "conversation_id=" << result.conversationId;
              finalizeActiveConversationSync();
              return;
            }

            if (m_activeConversationSync.hasMore && !pullMadeProgress) {
              qWarning()
                  << "[MainWidget] stop stalled pull pagination conversation_id="
                  << result.conversationId << "local_last_seq="
                  << m_activeConversationSync.localLastSeq << "next_after_seq="
                  << m_activeConversationSync.nextAfterSeq;
              finalizeActiveConversationSync();
              return;
            }

            if (highestSeq > m_activeConversationSync.localLastSeq) {
              m_activeConversationSync.ackUpToSeq = highestSeq;
              if (!websocketclient::instance()->isConnected()) {
                finalizeActiveConversationSync();
                return;
              }
              m_pendingMessageAckRequestId =
                  m_messageSyncClient->acknowledgeUpToSeq(
                      result.conversationId, highestSeq, true);
              return;
            }

            m_activeConversationSync.localLastSeq =
                qMax(m_activeConversationSync.localLastSeq,
                     m_activeConversationSync.nextAfterSeq);
            if (m_activeConversationSync.hasMore) {
              continueActiveConversationSync();
            } else {
              finalizeActiveConversationSync();
            }
          });
  connect(m_messageSyncClient, &MessageSyncClient::ackSucceeded, this,
          [this](const QString &requestId, const MessageAckResult &result) {
            if (requestId != m_pendingMessageAckRequestId) {
              return;
            }
            m_pendingMessageAckRequestId.clear();
            if (!m_activeConversationSync.isActive()) {
              return;
            }

            m_activeConversationSync.localLastSeq =
                qMax(m_activeConversationSync.localLastSeq,
                     qMax(result.ackedUpToSeq,
                          qMax(m_activeConversationSync.ackUpToSeq,
                               m_activeConversationSync.nextAfterSeq)));
            if (m_activeConversationSync.hasMore) {
              continueActiveConversationSync();
            } else {
              finalizeActiveConversationSync();
            }
          });
  connect(m_messageSyncClient, &MessageSyncClient::requestFailed, this,
          [this](const QString &requestId, const QString &action, int code,
                 const QString &error) {
            if (requestId != m_pendingMessagePullRequestId &&
                requestId != m_pendingMessageAckRequestId) {
              return;
            }
            qWarning() << "[MainWidget] message sync request failed action=" << action
                       << "request_id=" << requestId << "code=" << code
                       << "message=" << error;
            if (requestId == m_pendingMessagePullRequestId) {
              m_pendingMessagePullRequestId.clear();
            }
            if (requestId == m_pendingMessageAckRequestId) {
              m_pendingMessageAckRequestId.clear();
            }
            finalizeActiveConversationSync();
          });
  connect(m_chatFileService, &ChatFileService::downloadFinished, this,
          [this](const QString &requestId, const ChatFileDownloadTask &task) {
            auto it = m_pendingFileDownloads.find(requestId);
            const QString savePath =
                it == m_pendingFileDownloads.end() ? task.savePath : it->savePath;
            const QString originalName =
                it == m_pendingFileDownloads.end() ? QString() : it->originalName;
            if (it != m_pendingFileDownloads.end()) {
              m_pendingFileDownloads.erase(it);
            }

            QMessageBox::information(
                this, QStringLiteral("文件已保存"),
                QStringLiteral("%1已保存到：\n%2")
                    .arg(originalName.trimmed().isEmpty()
                             ? QString()
                             : QStringLiteral("文件“%1”").arg(originalName),
                         savePath));
          });
  connect(m_chatFileService, &ChatFileService::requestFailed, this,
          [this](const QString &requestId, const QString &action, int code,
                 const QString &error) {
            const bool matchesUpload =
                requestId == m_pendingFileTransfer.uploadRequestId;
            const bool matchesSend = requestId == m_pendingFileTransfer.sendRequestId;
            const auto downloadIt = m_pendingFileDownloads.find(requestId);
            const bool matchesDownload = downloadIt != m_pendingFileDownloads.end();
            if (!matchesUpload && !matchesSend && !matchesDownload) {
              return;
            }

            const QString stage =
                matchesUpload ? QStringLiteral("上传")
                              : (matchesSend ? QStringLiteral("发送消息")
                                             : QStringLiteral("下载"));
            const QString title =
                matchesDownload ? QStringLiteral("文件下载失败")
                                : QStringLiteral("文件传输失败");
            const QString detail =
                matchesDownload && downloadIt != m_pendingFileDownloads.end() &&
                        !downloadIt->savePath.trimmed().isEmpty()
                    ? QStringLiteral("%1失败: %2 (code=%3)\n保存路径：%4")
                          .arg(stage,
                               error.isEmpty() ? QStringLiteral("未知错误") : error,
                               QString::number(code), downloadIt->savePath)
                    : QStringLiteral("%1失败: %2 (code=%3)")
                          .arg(stage,
                               error.isEmpty() ? QStringLiteral("未知错误") : error,
                               QString::number(code));
            QMessageBox::warning(this, title, detail);
            qWarning() << "[MainWidget] file transfer failed action=" << action
                       << "request_id=" << requestId << "code=" << code
                       << "message=" << error;
            if (matchesDownload) {
              m_pendingFileDownloads.erase(downloadIt);
            } else {
              m_pendingFileTransfer.clear();
            }
          });

  m_conversationListRefreshTimer = new QTimer(this);
  m_conversationListRefreshTimer->setInterval(kConversationListRefreshIntervalMs);
  connect(m_conversationListRefreshTimer, &QTimer::timeout, this,
          [this]() { requestConversationList(false); });
  connect(websocketclient::instance(), &websocketclient::connected, this,
          [this]() {
            if (!UserSession::instance().isLoggedIn()) {
              return;
            }
            scheduleInitialConversationSync();
            requestConversationList(true, true);
            requestFriendListForContacts(true, true);
          });
}

/**
 * @brief 析构 Widget 实例并释放相关资源。
 * @return 无返回值。
 */
Widget::~Widget() {
  delete m_localChatStore;
  delete ui;
}

/**
 * @brief 初始化界面依赖与状态。
 * @return 无返回值。
 */
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

  /**
   * @brief 主布局
   * @param arg1 输入参数 `arg1`。
   * @return 返回 QVBoxLayout mainLayout = new 结果。
   */
  QVBoxLayout *mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(0, 0, 0, 0); // 确保没有 margin
  mainLayout->setSpacing(0);

  /**
   * @brief 整体背景容器 (因为 WA_TranslucentBackground 可能会导致全透明，需要一个底板)
   * @param arg1 输入参数 `arg1`。
   * @return 返回处理得到的对象指针。
   */
  QWidget *container = new QWidget(this);
  container->setObjectName("MainContainer");
  /**
   * @brief 1. 容器底色设为浅灰 (#f0f2f5)
   * @param arg1 输入参数 `arg1`。
   * @return 返回 container-> 结果。
   */
  container->setStyleSheet("#MainContainer { background-color: #f0f2f5; "
                           "border: 1px solid #dcdcdc; border-radius: 8px; }");
  mainLayout->addWidget(container);

  QVBoxLayout *containerLayout = new QVBoxLayout(container);
  containerLayout->setContentsMargins(0, 0, 0, 0);
  containerLayout->setSpacing(0);

  /**
   * @brief --- 上部：用户个人信息展示 ---
   * @param arg1 输入参数 `arg1`。
   * @return 返回 m_topPanel = new 结果。
   */
  m_topPanel = new QWidget(container);
  m_topPanel->setFixedHeight(120);
  /**
   * @brief 2. 顶部面板保持白色，以便与灰色的底板区分
   * @param arg1 输入参数 `arg1`。
   * @return 无返回值。
   */
  applyTopPanelThemeColor(m_topPanelThemeColor);

  QHBoxLayout *mainTopLayout = new QHBoxLayout(m_topPanel);
  mainTopLayout->setContentsMargins(0, 0, 0, 0);

  QHBoxLayout *leftContentLayout = new QHBoxLayout();
  leftContentLayout->setContentsMargins(20, 20, 20, 20);

  /**
   * @brief 头像 (简单模拟)
   * @param arg1 输入参数 `arg1`。
   * @return 返回 m_avatarLabel = new 结果。
   */
  m_avatarLabel = new QLabel(m_topPanel);
  m_avatarLabel->setFixedSize(60, 60);
  m_avatarLabel->setStyleSheet(
      "background-color: #4a90e2; border-radius: 30px; color: white; "
      "font-weight: bold; qproperty-alignment: AlignCenter; border: none;");
  m_avatarLabel->setText("User"); // 默认文字

  /**
   * @brief 用户名
   * @param arg1 输入参数 `arg1`。
   * @param arg2 输入参数 `arg2`。
   * @return 返回 m_nameLabel = new 结果。
   */
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

  /**
   * @brief 设置、最小化和关闭按钮
   * @param arg1 输入参数 `arg1`。
   * @param arg2 输入参数 `arg2`。
   * @return 返回 m_settingsButton = new 结果。
   */
  m_settingsButton = new QPushButton("设", m_topPanel);
  m_minButton = new QPushButton("-", m_topPanel);
  m_closeButton = new QPushButton("x", m_topPanel);

  btnRowLayout->addWidget(m_settingsButton);
  btnRowLayout->addWidget(m_minButton);
  btnRowLayout->addWidget(m_closeButton);

  rightBtnLayout->addLayout(btnRowLayout);
  rightBtnLayout->addStretch();

  QHBoxLayout *quickActionLayout = new QHBoxLayout();
  quickActionLayout->setContentsMargins(0, 0, 5, 5);
  quickActionLayout->setSpacing(8);
  quickActionLayout->addStretch();

  m_quickActionButton = new QToolButton(m_topPanel);
  m_quickActionButton->setText("+");
  m_quickActionButton->setFixedSize(28, 28);
  m_quickActionButton->setPopupMode(QToolButton::InstantPopup);
  m_quickActionButton->setCursor(Qt::ArrowCursor);

  m_quickActionMenu = new QMenu(m_quickActionButton);
  QAction *addFriendAction = m_quickActionMenu->addAction(QStringLiteral("添加好友"));
  QAction *deleteFriendAction =
      m_quickActionMenu->addAction(QStringLiteral("删除好友"));
  m_quickActionMenu->addSeparator();
  QAction *createGroupAction =
      m_quickActionMenu->addAction(QStringLiteral("创建群聊"));
  QAction *joinGroupAction = m_quickActionMenu->addAction(QStringLiteral("加入群聊"));
  QAction *leaveGroupAction =
      m_quickActionMenu->addAction(QStringLiteral("退出群聊"));
  QAction *dismissGroupAction =
      m_quickActionMenu->addAction(QStringLiteral("解散群聊"));
  m_quickActionMenu->addSeparator();
  QAction *fileTransferAction =
      m_quickActionMenu->addAction(QStringLiteral("文件传输"));
  m_quickActionButton->setMenu(m_quickActionMenu);

  connect(addFriendAction, &QAction::triggered, this, &Widget::onOpenAddFriend);
  connect(deleteFriendAction, &QAction::triggered, this,
          &Widget::onOpenDeleteFriend);
  connect(createGroupAction, &QAction::triggered, this, [this]() {
    onOpenCreateGroup();
  });
  connect(joinGroupAction, &QAction::triggered, this, [this]() {
    onOpenSearchGroup();
  });
  connect(leaveGroupAction, &QAction::triggered, this,
          &Widget::onOpenLeaveGroup);
  connect(dismissGroupAction, &QAction::triggered, this,
          &Widget::onOpenDismissGroup);
  connect(fileTransferAction, &QAction::triggered, this,
          &Widget::onOpenFileTransfer);

  quickActionLayout->addWidget(m_quickActionButton);
  rightBtnLayout->addLayout(quickActionLayout);
  mainTopLayout->addLayout(rightBtnLayout);

  /**
   * @brief 样式：悬浮时背景变灰/红
   * @param arg1 输入参数 `arg1`。
   * @param arg2 输入参数 `arg2`。
   * @return 返回 m_settingsButton-> 结果。
   */
  m_settingsButton->setFixedSize(40, 30);
  m_settingsButton->setCursor(Qt::ArrowCursor);
  m_minButton->setFixedSize(40, 30);
  m_minButton->setCursor(Qt::ArrowCursor);
  m_closeButton->setFixedSize(40, 30);
  m_closeButton->setCursor(Qt::ArrowCursor);

  connect(m_settingsButton, &QPushButton::clicked, this, &Widget::onOpenSettings);
  connect(m_minButton, &QPushButton::clicked, this, &QWidget::showMinimized);
  connect(m_closeButton, &QPushButton::clicked, this, &QWidget::close);

  /**
   * @brief --- 中下部：标签页 ---
   * @param arg1 输入参数 `arg1`。
   * @return 返回 m_tabWidget = new 结果。
   */
  m_tabWidget = new QTabWidget(container);
  m_tabWidget->setDocumentMode(true);
  m_tabWidget->tabBar()->setExpanding(true);
  m_tabWidget->tabBar()->setUsesScrollButtons(false);
  m_sessionList = new QListWidget(m_tabWidget);
  m_sessionList->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
  m_sessionList->setFrameShape(QFrame::NoFrame);
  m_sessionList->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);

  m_contactList = new QListWidget(m_tabWidget);
  m_contactList->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
  m_contactList->setFrameShape(QFrame::NoFrame);
  m_contactList->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);

  m_groupList = new QListWidget(m_tabWidget);
  m_groupList->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
  m_groupList->setFrameShape(QFrame::NoFrame);
  m_groupList->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);

  m_tabWidget->addTab(m_sessionList, QStringLiteral("会话"));
  m_tabWidget->addTab(m_contactList, QStringLiteral("联系人"));
  m_tabWidget->addTab(m_groupList, QStringLiteral("群聊"));

  /**
   * @brief 添加到容器布局
   * @param arg1 输入参数 `arg1`。
   * @return 返回 containerLayout-> 结果。
   */
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
  applyMainThemeColor(m_topPanelThemeColor);
}

/**
 * @brief 初始化头像HTTPclient依赖与状态。
 * @return 无返回值。
 */
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

/**
 * @brief 解析并确定头像url结果。
 * @param avatarUrl 头像地址或头像来源。
 * @return 返回解析得到的 URL 对象。
 */
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
    /**
     * @brief Use as-is.
     * @param arg1 输入参数 `arg1`。
     * @return 返回 } else 结果。
     */
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

/**
 * @brief 发起头像image请求。
 * @param avatarUrl 头像地址或头像来源。
 * @return 无返回值。
 */
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

/**
 * @brief 应用头像pixmap配置。
 * @param pixmap 位图对象。
 * @return 无返回值。
 */
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

/**
 * @brief 应用默认头像配置。
 * @return 无返回值。
 */
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

/**
 * @brief 设置user信息值。
 * @param username 用户名。
 * @param avatarPath 头像路径。
 * @param signature 个性签名内容。
 * @return 无返回值。
 */
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

/**
 * @brief 应用toppanel主题颜色配置。
 * @param color 颜色值。
 * @return 无返回值。
 */
void Widget::applyTopPanelThemeColor(const QColor &color) {
  m_topPanelThemeColor = color.isValid() ? color : QColor(QStringLiteral("#ffffff"));
  if (m_topPanel) {
    m_topPanel->setStyleSheet(topPanelStyleSheetForColor(m_topPanelThemeColor));
  }
}

/**
 * @brief 应用main主题颜色配置。
 * @param color 颜色值。
 * @return 无返回值。
 */
void Widget::applyMainThemeColor(const QColor &color) {
  applyTopPanelThemeColor(color);
  if (m_settingsButton) {
    m_settingsButton->setStyleSheet(topButtonStyleSheetForColor(m_topPanelThemeColor));
  }
  if (m_minButton) {
    m_minButton->setStyleSheet(minButtonStyleSheet());
  }
  if (m_closeButton) {
    m_closeButton->setStyleSheet(topButtonStyleSheetForColor(m_topPanelThemeColor, true));
  }
  if (m_quickActionButton) {
    m_quickActionButton->setStyleSheet(
        quickActionButtonStyleSheetForColor(m_topPanelThemeColor));
  }
  if (m_quickActionMenu) {
    m_quickActionMenu->setStyleSheet(quickActionMenuStyleSheetForColor(m_topPanelThemeColor));
  }
  if (m_tabWidget) {
    m_tabWidget->setStyleSheet(mainTabsStyleSheetForColor(m_topPanelThemeColor));
  }
  if (m_sessionList) {
    m_sessionList->setStyleSheet(listWidgetStyleSheetForColor(m_topPanelThemeColor));
  }
  if (m_contactList) {
    m_contactList->setStyleSheet(listWidgetStyleSheetForColor(m_topPanelThemeColor));
  }
  if (m_groupList) {
    m_groupList->setStyleSheet(listWidgetStyleSheetForColor(m_topPanelThemeColor));
  }
}

/**
 * @brief 设置当前user编号值。
 * @param userId 用户 ID。
 * @return 无返回值。
 */
void Widget::setCurrentUserId(const QString &userId) {
  const QString previousUserId = m_currentUserId;
  m_currentUserId = userId.trimmed();
  if (previousUserId != m_currentUserId) {
    m_conversationSyncQueue.clear();
    m_queuedConversationSyncIds.clear();
    m_activeConversationSync.clear();
    m_pendingMessagePullRequestId.clear();
    m_pendingMessageAckRequestId.clear();
  }
  if (!m_localChatStore) {
    return;
  }
  if (m_currentUserId.isEmpty()) {
    m_pendingInitialConversationSync = false;
    m_localChatStore->clearCurrentUser();
    return;
  }

  QString error;
  if (!m_localChatStore->setCurrentUser(m_currentUserId, &error)) {
    qWarning() << "[MainWidget] failed to initialize local chat store:" << error;
  }
}

/**
 * @brief 设置当前user数字编号值。
 * @param numericId 数字编号。
 * @return 无返回值。
 */
void Widget::setCurrentUserNumericId(const QString &numericId) {
  m_currentUserNumericId = numericId.trimmed();
  if (!m_currentUserNumericId.isEmpty()) {
    scheduleInitialConversationSync();
  }
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

/**
 * @brief 设置主界面主题颜色值。
 * @param colorHex 颜色十六进制字符串。
 * @return 无返回值。
 */
void Widget::setThemeColor(const QString &colorHex) {
  applyMainThemeColor(QColor(colorHex.trimmed()));
}

/**
 * @brief 设置资料apiclient值。
 * @param profileApiClient 文件相关数据。
 * @return 无返回值。
 */
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
  connect(m_profileApiClient, &ProfileApiClient::serverRequestReceived, this,
          &Widget::onProfileServerRequestReceived, Qt::UniqueConnection);
  connect(m_profileApiClient, &ProfileApiClient::leaveGroupFinished, this,
          &Widget::onLeaveGroupFinished, Qt::UniqueConnection);
  connect(m_profileApiClient, &ProfileApiClient::dismissGroupFinished, this,
          &Widget::onDismissGroupFinished, Qt::UniqueConnection);
  if (m_conversationListRefreshTimer &&
      !m_currentUserNumericId.trimmed().isEmpty() &&
      !m_conversationListRefreshTimer->isActive()) {
    m_conversationListRefreshTimer->start();
  }
  requestConversationList();
  requestFriendListForContacts();
}

/**
 * @brief 执行scheduleInitialConversationSync的核心逻辑。
 * @return 无返回值。
 */
void Widget::scheduleInitialConversationSync() {
  if (m_currentUserNumericId.trimmed().isEmpty() &&
      UserSession::instance().numericId().trimmed().isEmpty()) {
    return;
  }
  m_pendingInitialConversationSync = true;
}

/**
 * @brief 执行beginInitialConversationSyncIfNeeded的核心逻辑。
 * @return 无返回值。
 */
void Widget::beginInitialConversationSyncIfNeeded() {
  if (!m_pendingInitialConversationSync) {
    return;
  }
  if (!m_messageSyncClient || !m_localChatStore ||
      !m_pendingMessagePullRequestId.isEmpty() ||
      !m_pendingMessageAckRequestId.isEmpty()) {
    return;
  }

  m_pendingInitialConversationSync = false;
  m_conversationSyncQueue.clear();
  m_queuedConversationSyncIds.clear();
  for (const conversationlist::ConversationItem &conversation :
       m_conversationListManager.conversations()) {
    enqueueConversationSyncTask(conversation.conversationId,
                                conversation.lastMessageSeq, false, false);
  }
  startNextConversationSyncTask();
}

/**
 * @brief 执行enqueueConversationSyncTask的核心逻辑。
 * @param conversationId 会话 ID。
 * @param serverLastSeq 数值参数 `serverLastSeq`。
 * @param onDemand 布尔参数 `onDemand`。
 * @param prioritize 布尔参数 `prioritize`。
 * @return 无返回值。
 */
void Widget::enqueueConversationSyncTask(const QString &conversationId,
                                         qint64 serverLastSeq, bool onDemand,
                                         bool prioritize) {
  const QString trimmedConversationId = conversationId.trimmed();
  if (trimmedConversationId.isEmpty()) {
    return;
  }

  if (m_activeConversationSync.conversationId == trimmedConversationId) {
    m_activeConversationSync.serverLastSeq =
        qMax(m_activeConversationSync.serverLastSeq, serverLastSeq);
    m_activeConversationSync.onDemand =
        m_activeConversationSync.onDemand || onDemand;
    return;
  }

  for (ConversationSyncTask &task : m_conversationSyncQueue) {
    if (task.conversationId == trimmedConversationId) {
      task.serverLastSeq = qMax(task.serverLastSeq, serverLastSeq);
      task.onDemand = task.onDemand || onDemand;
      return;
    }
  }

  ConversationSyncTask task;
  task.conversationId = trimmedConversationId;
  task.serverLastSeq = serverLastSeq;
  task.onDemand = onDemand;
  if (prioritize) {
    m_conversationSyncQueue.push_front(task);
  } else {
    m_conversationSyncQueue.push_back(task);
  }
  m_queuedConversationSyncIds.insert(trimmedConversationId);
}

/**
 * @brief 启动next会话同步task流程。
 * @return 无返回值。
 */
void Widget::startNextConversationSyncTask() {
  if (!m_messageSyncClient || !m_localChatStore ||
      !m_pendingMessagePullRequestId.isEmpty() ||
      !m_pendingMessageAckRequestId.isEmpty()) {
    return;
  }

  while (!m_conversationSyncQueue.isEmpty()) {
    const ConversationSyncTask task = m_conversationSyncQueue.takeFirst();
    m_queuedConversationSyncIds.remove(task.conversationId);
    if (task.conversationId.trimmed().isEmpty()) {
      continue;
    }

    QString error;
    const qint64 localLastSeq =
        m_localChatStore->lastSeqForConversation(task.conversationId, &error);
    if (!error.isEmpty()) {
      qWarning() << "[MainWidget] failed to load local last_seq conversation_id="
                 << task.conversationId << "error=" << error;
    }
    if (task.serverLastSeq > 0 && task.serverLastSeq <= localLastSeq) {
      continue;
    }

    startConversationPull(task.conversationId, localLastSeq, task.serverLastSeq,
                          task.onDemand);
    return;
  }

  m_activeConversationSync.clear();
  beginInitialConversationSyncIfNeeded();
}

/**
 * @brief 启动会话拉取流程。
 * @param conversationId 会话 ID。
 * @param afterSeq 数值参数 `afterSeq`。
 * @param serverLastSeq 数值参数 `serverLastSeq`。
 * @param onDemand 布尔参数 `onDemand`。
 * @return 无返回值。
 */
void Widget::startConversationPull(const QString &conversationId, qint64 afterSeq,
                                   qint64 serverLastSeq, bool onDemand) {
  if (!m_messageSyncClient || !websocketclient::instance()->isConnected()) {
    m_activeConversationSync.clear();
    return;
  }
  m_activeConversationSync.clear();
  m_activeConversationSync.conversationId = conversationId.trimmed();
  m_activeConversationSync.localLastSeq = qMax<qint64>(afterSeq, 0);
  m_activeConversationSync.serverLastSeq = qMax<qint64>(serverLastSeq, 0);
  m_activeConversationSync.onDemand = onDemand;
  m_pendingMessagePullRequestId =
      m_messageSyncClient->pullMessages(m_activeConversationSync.conversationId,
                                        m_activeConversationSync.localLastSeq, 100);
}

/**
 * @brief 继续执行active会话同步流程。
 * @return 无返回值。
 */
void Widget::continueActiveConversationSync() {
  if (!m_activeConversationSync.isActive()) {
    startNextConversationSyncTask();
    return;
  }
  startConversationPull(m_activeConversationSync.conversationId,
                        qMax(m_activeConversationSync.localLastSeq,
                             m_activeConversationSync.nextAfterSeq),
                        m_activeConversationSync.serverLastSeq,
                        m_activeConversationSync.onDemand);
}

/**
 * @brief 完成active会话同步收尾处理。
 * @return 无返回值。
 */
void Widget::finalizeActiveConversationSync() {
  m_activeConversationSync.clear();
  if (!m_pendingMessagePullRequestId.isEmpty() ||
      !m_pendingMessageAckRequestId.isEmpty()) {
    return;
  }
  if (m_conversationSyncQueue.isEmpty()) {
    beginInitialConversationSyncIfNeeded();
  }
  startNextConversationSyncTask();
}

/**
 * @brief 发起会话incremental同步请求。
 * @param conversationId 会话 ID。
 * @return 无返回值。
 */
void Widget::requestConversationIncrementalSync(const QString &conversationId) {
  const QString trimmedConversationId = conversationId.trimmed();
  if (trimmedConversationId.isEmpty() || !m_messageSyncClient || !m_localChatStore) {
    return;
  }
  enqueueConversationSyncTask(trimmedConversationId,
                              serverLastSeqForConversation(trimmedConversationId),
                              true, true);
  startNextConversationSyncTask();
}

/**
 * @brief 执行serverLastSeqForConversation的核心逻辑。
 * @param conversationId 会话 ID。
 * @return 返回计算得到的数值结果。
 */
qint64 Widget::serverLastSeqForConversation(const QString &conversationId) const {
  const QString trimmedConversationId = conversationId.trimmed();
  for (const conversationlist::ConversationItem &conversation :
       m_conversationListManager.conversations()) {
    if (conversation.conversationId.trimmed() == trimmedConversationId) {
      return conversation.lastMessageSeq;
    }
  }
  return 0;
}

/**
 * @brief 执行storeAndRouteMessage的核心逻辑。
 * @param message 消息对象或消息内容。
 * @param incrementUnread 布尔参数 `incrementUnread`。
 * @return 返回布尔结果。
 */
bool Widget::storeAndRouteMessage(const ChatMessage &message, bool incrementUnread) {
  if (!message.isValid()) {
    return false;
  }

  bool stored = true;
  if (!m_localChatStore) {
    stored = false;
  } else {
    QString error;
    if (!m_localChatStore->saveMessage(message, &error)) {
      qWarning() << "[MainWidget] failed to persist message conversation_id="
                 << message.conversationId << "message_id=" << message.messageId
                 << "request_id=" << message.requestId << "error=" << error;
      stored = false;
    }
  }

  if (SessionWindow *sessionWindow =
          m_sessionWindowsByConversationId.value(message.conversationId)) {
    sessionWindow->appendPersistedMessage(message);
  }
  updateConversationStateFromMessage(message, incrementUnread);
  return stored;
}

/**
 * @brief 更新会话状态from消息状态。
 * @param message 消息对象或消息内容。
 * @param incrementUnread 布尔参数 `incrementUnread`。
 * @return 无返回值。
 */
void Widget::updateConversationStateFromMessage(const ChatMessage &message,
                                                bool incrementUnread) {
  const QString conversationId = message.conversationId.trimmed();
  if (conversationId.isEmpty()) {
    return;
  }

  ConversationListState state =
      m_conversationStatesByConversationId.value(conversationId);
  state.conversationId = conversationId;
  state.lastMessagePreview = previewTextForMessage(message);
  state.placeholder = false;

  const conversationlist::ConversationItem *meta = nullptr;
  for (const conversationlist::ConversationItem &conversation :
       m_conversationListManager.conversations()) {
    if (conversation.conversationId.trimmed() == conversationId) {
      meta = &conversation;
      break;
    }
  }

  if (meta) {
    state.conversationUuid = meta->conversationUuid;
    state.groupNumericId = meta->groupNumericId;
    state.conversationType = meta->conversationType;
    state.avatarUrl = meta->avatarUrl;
    state.memberCount = meta->memberCount;
    state.peerUserId = meta->peerUserId;
    state.peerNumericId = meta->peerNumericId;
    state.peerUsername = meta->peerUsername;
    state.peerNickname = meta->peerNickname;
    state.peerAvatarUrl = meta->peerAvatarUrl;
    state.peerBio = meta->peerBio;
    state.peerStatus = meta->peerStatus;
    state.peerIsOnline = meta->peerIsOnline;
    state.peerLastSeenAt = meta->peerLastSeenAt;
    if (state.displayName.isEmpty()) {
      state.displayName = meta->name.trimmed();
    }
  }

  if (state.displayName.isEmpty()) {
    state.displayName = message.senderUsername.trimmed();
  }
  if (state.displayName.isEmpty()) {
    state.displayName = conversationId;
  }
  if (state.peerUserId.isEmpty()) {
    state.peerUserId = message.senderUserId.trimmed();
  }
  if (state.peerNumericId.isEmpty()) {
    state.peerNumericId = message.senderNumericId.trimmed();
  }

  const QString currentUserId = UserSession::instance().userId().trimmed();
  const bool isOutgoing =
      !currentUserId.isEmpty() && message.senderUserId.trimmed() == currentUserId;
  SessionWindow *openWindow = m_sessionWindowsByConversationId.value(conversationId);
  if (openWindow || isOutgoing || !incrementUnread) {
    state.unreadCount = 0;
  } else {
    state.unreadCount += 1;
  }

  m_conversationStatesByConversationId.insert(conversationId, state);
  if (QListWidgetItem *item = findConversationItemByConversationId(conversationId)) {
    applyConversationStateToItem(item, state, meta);
  } else {
    upsertConversationListItem(state, meta);
  }
}

/**
 * @brief 执行previewTextForMessage的核心逻辑。
 * @param message 消息对象或消息内容。
 * @return 返回处理后的字符串结果。
 */
QString Widget::previewTextForMessage(const ChatMessage &message) const {
  if (message.kind == ChatMessageKind::File) {
    return filePreviewText(message.file.originalName);
  }
  return message.text.trimmed();
}

/**
 * @brief --- 拖拽窗口支持 ---
 * @param event 对象参数 `event`。
 * @return 无返回值。
 */
void Widget::mousePressEvent(QMouseEvent *event) {
  if (event->button() == Qt::LeftButton) {
    QWidget *target = childAt(event->position().toPoint());
    const bool inTopPanel =
        target && m_topPanel && (target == m_topPanel || m_topPanel->isAncestorOf(target));

    if (inTopPanel) {
      QWidget *walk = target;
      bool overButton = false;
      while (walk) {
        if (qobject_cast<QAbstractButton *>(walk)) {
          overButton = true;
          break;
        }
        if (walk == m_topPanel) {
          break;
        }
        walk = walk->parentWidget();
      }

      if (!overButton) {
        m_isDragging = true;
        m_dragPosition =
            event->globalPosition().toPoint() - frameGeometry().topLeft();
        event->accept();
        return;
      }
    }
  }
  QWidget::mousePressEvent(event);
}

/**
 * @brief 执行mouseMoveEvent的核心逻辑。
 * @param event 对象参数 `event`。
 * @return 无返回值。
 */
void Widget::mouseMoveEvent(QMouseEvent *event) {
  if (m_isDragging && (event->buttons() & Qt::LeftButton)) {
    move(event->globalPosition().toPoint() - m_dragPosition);
    event->accept();
    return;
  }
  QWidget::mouseMoveEvent(event);
}

/**
 * @brief 执行mouseReleaseEvent的核心逻辑。
 * @param event 对象参数 `event`。
 * @return 无返回值。
 */
void Widget::mouseReleaseEvent(QMouseEvent *event) {
  if (event->button() == Qt::LeftButton) {
    m_isDragging = false;
    event->accept();
    return;
  }
  QWidget::mouseReleaseEvent(event);
}

/**
 * @brief 响应会话double点击事件。
 * @param item 数据项对象。
 * @return 无返回值。
 */
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
  const ConversationListState conversationState =
      m_conversationStatesByConversationId.value(conversationId);
  const QString currentDisplayName =
      m_currentDisplayName.trimmed().isEmpty()
          ? UserSession::instance().username().trimmed()
          : m_currentDisplayName.trimmed();
  const QString currentAvatarSource = m_currentAvatarUrl.trimmed();
  QString peerDisplayName = item->data(kRoleDisplayName).toString().trimmed();
  if (peerDisplayName.isEmpty()) {
    peerDisplayName = conversationState.displayName.trimmed();
  }
  if (peerDisplayName.isEmpty()) {
    peerDisplayName = session.displayName().trimmed();
  }
  QString peerAvatarSource = conversationState.peerAvatarUrl.trimmed();
  if (peerAvatarSource.isEmpty()) {
    peerAvatarSource = item->data(kRoleAvatarUrl).toString().trimmed();
  }
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
    sessionWindow->setCurrentProfile(currentDisplayName, currentAvatarSource);
    sessionWindow->setPeerProfile(peerDisplayName, peerAvatarSource);
    sessionWindow->setPeerIdentity(peerUserId, peerNumericId);
    sessionWindow->updatePeerPresence(item->data(kRoleIsOnline).toBool(),
                                      item->data(kRoleLastSeenAtUtc).toString());
    if (sessionWindow->isMinimized()) {
      sessionWindow->showNormal();
    } else {
      sessionWindow->show();
    }
    sessionWindow->raise();
    sessionWindow->activateWindow();
    requestConversationIncrementalSync(conversationId);
    qInfo().noquote() << "[MainWidget] reuse session window peer_user_id="
                      << peerUserId << "peer_numeric_id=" << peerNumericId;
    return;
  }

  sessionWindow = new SessionWindow(session);
  sessionWindow->setCurrentProfile(currentDisplayName, currentAvatarSource);
  sessionWindow->setPeerProfile(peerDisplayName, peerAvatarSource);
  sessionWindow->setPeerIdentity(peerUserId, peerNumericId);
  sessionWindow->updatePeerPresence(item->data(kRoleIsOnline).toBool(),
                                    item->data(kRoleLastSeenAtUtc).toString());
  if (m_localChatStore && !conversationId.isEmpty()) {
    QString error;
    const QVector<ChatMessage> history =
        m_localChatStore->loadMessages(conversationId, 200, &error);
    if (!error.isEmpty()) {
      qWarning() << "[MainWidget] failed to load local history conversation_id="
                 << conversationId << "error=" << error;
    } else {
      qInfo().noquote() << "[MainWidget] replay local history conversation_id="
                        << conversationId << "count=" << history.size();
      sessionWindow->loadHistory(history);
    }
  }
  if (!peerUserId.isEmpty()) {
    m_sessionWindowsByUserId.insert(peerUserId, sessionWindow);
  }
  if (!peerNumericId.isEmpty()) {
    m_sessionWindowsByNumericId.insert(peerNumericId, sessionWindow);
  }
  if (!conversationId.isEmpty()) {
    m_sessionWindowsByConversationId.insert(conversationId, sessionWindow);
  }
  connect(sessionWindow, &SessionWindow::imageAttachmentRequested, this,
          [this](const QString &conversationId, const QString &conversationName) {
            if (!ensureAttachmentTransferReady(QStringLiteral("图片"))) {
              return;
            }
            startAttachmentTransferForConversation(
                conversationId, conversationName,
                QStringLiteral("选择要发送的图片"),
                QStringLiteral(
                    "图片文件 (*.png *.jpg *.jpeg *.bmp *.gif *.webp);;All Files (*.*)"),
                QStringLiteral("图片"));
          });
  connect(sessionWindow, &SessionWindow::fileAttachmentRequested, this,
          [this](const QString &conversationId, const QString &conversationName) {
            if (!ensureAttachmentTransferReady(QStringLiteral("文件"))) {
              return;
            }
            startAttachmentTransferForConversation(
                conversationId, conversationName,
                QStringLiteral("选择要发送的文件"),
                QStringLiteral("All Files (*.*)"),
                QStringLiteral("文件"));
          });
  connect(sessionWindow, &SessionWindow::fileDownloadRequested, this,
          [this](const ChatMessage &message, bool chooseSavePath) {
            startFileDownloadForMessage(message, chooseSavePath);
          });
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
  connect(sessionWindow, &SessionWindow::messageReadyForPersistence, this,
          [this](const ChatMessage &message) {
            if (!m_localChatStore) {
              qWarning() << "[MainWidget] skip persist: local chat store missing";
              return;
            }
            qInfo().noquote() << "[MainWidget] persist message"
                              << "conversation_id=" << message.conversationId
                              << "request_id=" << message.requestId
                              << "message_id=" << message.messageId
                              << "kind="
                              << (message.kind == ChatMessageKind::File ? "file"
                                                                        : "text");
            QString error;
            if (!m_localChatStore->saveMessage(message, &error)) {
              qWarning() << "[MainWidget] failed to persist message:" << error;
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
  requestConversationIncrementalSync(conversationId);
  sessionWindow->show();
}

/**
 * @brief 执行addSessionItem的核心逻辑。
 * @param session 会话对象。
 * @return 无返回值。
 */
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

/**
 * @brief 响应打开设置事件。
 * @return 无返回值。
 */
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
  connect(m_settingsWindow, &SettingsWindow::themeColorChanged, this,
          [this](const QString &colorHex) {
            applyMainThemeColor(QColor(colorHex.trimmed()));
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
    if (m_searchGroupDialog) {
      m_searchGroupDialog->close();
    }
    if (m_leaveGroupDialog) {
      m_leaveGroupDialog->close();
    }
    if (m_dismissGroupDialog) {
      m_dismissGroupDialog->close();
    }
    QSet<SessionWindow *> sessionWindowsToClose;
    for (const auto &window : m_sessionWindowsByConversationId) {
      if (window) {
        sessionWindowsToClose.insert(window.data());
      }
    }
    for (SessionWindow *window : sessionWindowsToClose) {
      window->close();
    }
    m_currentUserId.clear();
    m_currentUserNumericId.clear();
    m_currentDisplayName.clear();
    m_currentSignature.clear();
    m_currentAvatarUrl.clear();
    m_pendingConversationListRequestId.clear();
    m_pendingFriendListRequestId.clear();
    m_pendingOpenConversationId.clear();
    m_silentConversationListRequestIds.clear();
    m_silentFriendListRequestIds.clear();
    m_conversationSyncQueue.clear();
    m_queuedConversationSyncIds.clear();
    m_activeConversationSync.clear();
    m_pendingMessagePullRequestId.clear();
    m_pendingMessageAckRequestId.clear();
    m_pendingInitialConversationSync = false;
    m_pendingFileTransfer.clear();
    m_pendingFileDownloads.clear();
    if (m_conversationListRefreshTimer) {
      m_conversationListRefreshTimer->stop();
    }
    m_conversationListManager.clear();
    m_friendListManager.clear();
    m_sessionWindowsByUserId.clear();
    m_sessionWindowsByNumericId.clear();
    m_sessionWindowsByConversationId.clear();
    m_conversationStatesByConversationId.clear();
    if (m_localChatStore) {
      m_localChatStore->clearCurrentUser();
    }
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

/**
 * @brief 响应头像回复完成事件。
 * @param reply 网络回复对象。
 * @return 无返回值。
 */
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

/**
 * @brief 响应打开添加好友事件。
 * @return 无返回值。
 */
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

/**
 * @brief 响应打开删除好友事件。
 * @return 无返回值。
 */
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

/**
 * @brief 响应打开创建群组事件。
 * @return 无返回值。
 */
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

/**
 * @brief 响应打开搜索群组事件。
 * @return 无返回值。
 */
void Widget::onOpenSearchGroup() {
  if (!m_profileApiClient) {
    QMessageBox::warning(this, QStringLiteral("无法搜索群聊"),
                         QStringLiteral("Profile 服务未初始化。"));
    return;
  }

  if (m_searchGroupDialog) {
    if (m_searchGroupDialog->isMinimized()) {
      m_searchGroupDialog->showNormal();
    } else {
      m_searchGroupDialog->show();
    }
    m_searchGroupDialog->raise();
    m_searchGroupDialog->activateWindow();
    return;
  }

  m_searchGroupDialog = new SearchGroupDialog(m_profileApiClient, this);
  connect(m_searchGroupDialog, &SearchGroupDialog::groupJoined, this,
          [this](const JoinGroupResult &result) {
            const QString conversationId = result.conversationId.trimmed();
            const QString action = result.message.trimmed();
            if (action == QStringLiteral("open_existing_group")) {
              if (!conversationId.isEmpty()) {
                if (QListWidgetItem *item =
                        findConversationItemByConversationId(conversationId)) {
                  onSessionDoubleClicked(item);
                  return;
                }
                m_pendingOpenConversationId = conversationId;
              }
              requestConversationList(true);
              return;
            }

            if (!conversationId.isEmpty()) {
              m_pendingOpenConversationId = conversationId;
            }
            requestConversationList(true);
          });
  connect(m_searchGroupDialog, &QObject::destroyed, this,
          [this]() { m_searchGroupDialog = nullptr; });
  m_searchGroupDialog->show();
  m_searchGroupDialog->raise();
  m_searchGroupDialog->activateWindow();
}

/**
 * @brief 响应打开退出群组事件。
 * @return 无返回值。
 */
void Widget::onOpenLeaveGroup() {
  if (!m_profileApiClient) {
    QMessageBox::warning(this, QStringLiteral("无法退出群聊"),
                         QStringLiteral("Profile 服务未初始化。"));
    return;
  }

  if (m_leaveGroupDialog) {
    m_leaveGroupDialog->setConversations(m_conversationListManager.conversations());
    if (m_leaveGroupDialog->isMinimized()) {
      m_leaveGroupDialog->showNormal();
    } else {
      m_leaveGroupDialog->show();
    }
    m_leaveGroupDialog->raise();
    m_leaveGroupDialog->activateWindow();
    return;
  }

  m_leaveGroupDialog =
      new LeaveGroupDialog(m_currentUserId, m_conversationListManager.conversations(),
                           m_profileApiClient, this);
  connect(m_leaveGroupDialog, &QObject::destroyed, this,
          [this]() { m_leaveGroupDialog = nullptr; });
  m_leaveGroupDialog->show();
  m_leaveGroupDialog->raise();
  m_leaveGroupDialog->activateWindow();
}

/**
 * @brief 响应打开解散群组事件。
 * @return 无返回值。
 */
void Widget::onOpenDismissGroup() {
  if (!m_profileApiClient) {
    QMessageBox::warning(this, QStringLiteral("无法解散群聊"),
                         QStringLiteral("Profile 服务未初始化。"));
    return;
  }

  if (m_dismissGroupDialog) {
    m_dismissGroupDialog->setConversations(
        m_conversationListManager.conversations());
    if (m_dismissGroupDialog->isMinimized()) {
      m_dismissGroupDialog->showNormal();
    } else {
      m_dismissGroupDialog->show();
    }
    m_dismissGroupDialog->raise();
    m_dismissGroupDialog->activateWindow();
    return;
  }

  m_dismissGroupDialog = new DismissGroupDialog(
      m_currentUserId, m_conversationListManager.conversations(), m_profileApiClient,
      this);
  connect(m_dismissGroupDialog, &QObject::destroyed, this,
          [this]() { m_dismissGroupDialog = nullptr; });
  m_dismissGroupDialog->show();
  m_dismissGroupDialog->raise();
  m_dismissGroupDialog->activateWindow();
}

/**
 * @brief 确保附件传输Ready满足预期条件。
 * @param attachmentLabel 字符串参数 `attachmentLabel`。
 * @return 返回本次处理是否成功。
 */
bool Widget::ensureAttachmentTransferReady(const QString &attachmentLabel) {
  const QString failureTitle =
      QStringLiteral("无法发送%1").arg(attachmentLabel.trimmed());
  if (!m_chatFileService) {
    QMessageBox::warning(this, failureTitle,
                         QStringLiteral("文件传输服务未初始化。"));
    return false;
  }
  if (!m_pendingFileTransfer.uploadRequestId.isEmpty() ||
      !m_pendingFileTransfer.sendRequestId.isEmpty()) {
    QMessageBox::information(this, QStringLiteral("文件传输中"),
                             QStringLiteral("当前已有文件在传输，请稍后再试。"));
    return false;
  }
  if (!UserSession::instance().isLoggedIn()) {
    QMessageBox::warning(this, failureTitle, QStringLiteral("请先登录。"));
    return false;
  }
  if (!UserSession::instance().hasValidUploadToken()) {
    QMessageBox::warning(this, failureTitle,
                         QStringLiteral("上传凭证无效或已过期，请重新登录。"));
    return false;
  }
  return true;
}

/**
 * @brief 启动attachmenttransferfor会话流程。
 * @param conversationId 会话 ID。
 * @param conversationName 会话相关标识或会话数据。
 * @param dialogTitle 字符串参数 `dialogTitle`。
 * @param fileFilter 文件相关数据。
 * @param attachmentLabel 字符串参数 `attachmentLabel`。
 * @return 无返回值。
 */
void Widget::startAttachmentTransferForConversation(
    const QString &conversationId, const QString &conversationName,
    const QString &dialogTitle, const QString &fileFilter,
    const QString &attachmentLabel) {
  const QString trimmedConversationId = conversationId.trimmed();
  const QString trimmedConversationName = conversationName.trimmed();
  const QString failureTitle =
      QStringLiteral("无法发送%1").arg(attachmentLabel.trimmed());
  if (trimmedConversationId.isEmpty()) {
    QMessageBox::warning(this, failureTitle, QStringLiteral("选中的会话无效。"));
    return;
  }

  const QString filePath = QFileDialog::getOpenFileName(
      this, dialogTitle.trimmed(), QString(), fileFilter.trimmed());
  if (filePath.isEmpty()) {
    return;
  }

  QFileInfo fileInfo(filePath);
  if (!fileInfo.exists() || !fileInfo.isFile()) {
    QMessageBox::warning(this, failureTitle, QStringLiteral("文件不存在。"));
    return;
  }
  if (!fileInfo.isReadable()) {
    QMessageBox::warning(this, failureTitle, QStringLiteral("文件不可读。"));
    return;
  }
  if (fileInfo.size() > kMaxTransferFileSizeBytes) {
    const QString sizeHint =
        attachmentLabel.trimmed() == QStringLiteral("图片")
            ? QStringLiteral("当前仅支持单张图片且大小不超过 20MB。")
            : QStringLiteral("当前仅支持单文件且大小不超过 20MB。");
    QMessageBox::warning(this, failureTitle, sizeHint);
    return;
  }

  const QString currentUserId = m_currentUserId.trimmed().isEmpty()
                                    ? UserSession::instance().userId().trimmed()
                                    : m_currentUserId.trimmed();
  if (currentUserId.isEmpty()) {
    QMessageBox::warning(this, failureTitle,
                         QStringLiteral("当前用户信息无效，请重新登录。"));
    return;
  }

  m_pendingFileTransfer.clear();
  m_pendingFileTransfer.conversationId = trimmedConversationId;
  m_pendingFileTransfer.localFilePath = filePath;
  m_pendingFileTransfer.uploadRequestId =
      m_chatFileService->uploadFile(trimmedConversationId, filePath, currentUserId);

  if (m_sessionWindowsByConversationId.value(trimmedConversationId)) {
    return;
  }

  QMessageBox::information(
      this, QStringLiteral("开始传输"),
      QStringLiteral("已开始向 %1 发送%2：%3")
          .arg(trimmedConversationName.isEmpty() ? trimmedConversationId
                                                 : trimmedConversationName,
               attachmentLabel.trimmed(), fileInfo.fileName()));
}

/**
 * @brief 启动文件下载for消息流程。
 * @param message 消息对象或消息内容。
 * @param chooseSavePath 路径相关参数。
 * @return 无返回值。
 */
void Widget::startFileDownloadForMessage(const ChatMessage &message,
                                         bool chooseSavePath) {
  if (message.kind != ChatMessageKind::File || !m_chatFileService) {
    return;
  }
  if (!UserSession::instance().hasValidUploadToken()) {
    QMessageBox::warning(
        this, QStringLiteral("无法下载文件"),
        QStringLiteral("当前下载凭证无效或已过期，请重新登录后再试。"));
    return;
  }

  const QString fileName =
      message.file.originalName.trimmed().isEmpty()
          ? QStringLiteral("file-%1").arg(message.file.fileId.trimmed())
          : message.file.originalName.trimmed();
  const QString defaultDir =
      QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
  const QString preferredPath =
      QDir(defaultDir.isEmpty() ? QDir::homePath() : defaultDir).filePath(fileName);

  QString savePath;
  if (chooseSavePath) {
    const QString conversationName =
        m_conversationStatesByConversationId.value(message.conversationId).displayName.trimmed();
    savePath = QFileDialog::getSaveFileName(
        this,
        conversationName.isEmpty()
            ? QStringLiteral("选择文件保存位置")
            : QStringLiteral("保存来自 %1 的文件").arg(conversationName),
        preferredPath, QStringLiteral("All Files (*.*)"));
    if (savePath.trimmed().isEmpty()) {
      return;
    }
  } else {
    savePath = uniqueDownloadSavePath(preferredPath);
  }

  const QString requestId =
      m_chatFileService->downloadFile(message.file.fileId, savePath);
  PendingFileDownloadState pending;
  pending.conversationId = message.conversationId;
  pending.originalName = fileName;
  pending.savePath = savePath;
  m_pendingFileDownloads.insert(requestId, pending);
}

/**
 * @brief 响应打开文件transfer事件。
 * @return 无返回值。
 */
void Widget::onOpenFileTransfer() {
  if (!ensureAttachmentTransferReady(QStringLiteral("文件"))) {
    return;
  }

  struct TransferTarget {
    QString conversationId;
    QString displayName;
    int conversationType = 0;
  };

  QList<TransferTarget> targets;
  QSet<QString> seenConversationIds;
  const QList<conversationlist::ConversationItem> conversations =
      m_conversationListManager.conversations();
  for (const conversationlist::ConversationItem &conversation : conversations) {
    const QString conversationId = conversation.conversationId.trimmed();
    if (conversationId.isEmpty() || seenConversationIds.contains(conversationId)) {
      continue;
    }
    seenConversationIds.insert(conversationId);

    TransferTarget target;
    target.conversationId = conversationId;
    target.conversationType = conversation.conversationType;
    target.displayName = conversation.name.trimmed();
    if (target.displayName.isEmpty()) {
      target.displayName = conversation.peerNickname.trimmed();
    }
    if (target.displayName.isEmpty()) {
      target.displayName = conversation.peerUsername.trimmed();
    }
    if (target.displayName.isEmpty()) {
      target.displayName = conversation.peerNumericId.trimmed();
    }
    if (target.displayName.isEmpty()) {
      target.displayName = conversationId;
    }
    targets.push_back(target);
  }

  for (const friendlist::FriendItem &friendItem : m_friendListManager.friends()) {
    const QString conversationId = friendItem.conversationId.trimmed();
    if (conversationId.isEmpty() || seenConversationIds.contains(conversationId)) {
      continue;
    }
    seenConversationIds.insert(conversationId);

    TransferTarget target;
    target.conversationId = conversationId;
    target.conversationType = 1;
    target.displayName = friendItem.displayName.trimmed().isEmpty()
                             ? friendItem.numericId.trimmed()
                             : friendItem.displayName.trimmed();
    if (target.displayName.isEmpty()) {
      target.displayName = conversationId;
    }
    targets.push_back(target);
  }

  if (targets.isEmpty()) {
    QMessageBox::information(this, QStringLiteral("暂无可发送对象"),
                             QStringLiteral("当前没有可用于文件传输的好友或群聊。"));
    return;
  }

  QDialog dialog(this);
  dialog.setWindowTitle(QStringLiteral("文件传输"));
  dialog.resize(420, 520);

  auto *layout = new QVBoxLayout(&dialog);
  auto *hintLabel =
      new QLabel(QStringLiteral("选择一个好友或群聊，然后选择单个文件发送。文件大小上限 20MB。"),
                 &dialog);
  hintLabel->setWordWrap(true);
  layout->addWidget(hintLabel);

  auto *listWidget = new QListWidget(&dialog);
  for (const TransferTarget &target : targets) {
    const QString prefix = target.conversationType == 2 ? QStringLiteral("[群聊] ")
                                                        : QStringLiteral("[好友] ");
    auto *item = new QListWidgetItem(prefix + target.displayName, listWidget);
    item->setData(kTransferRoleConversationId, target.conversationId);
    item->setData(kTransferRoleConversationName, target.displayName);
    item->setData(kTransferRoleConversationType, target.conversationType);
  }
  listWidget->setSelectionMode(QAbstractItemView::SingleSelection);
  layout->addWidget(listWidget);

  auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                         &dialog);
  buttonBox->button(QDialogButtonBox::Ok)->setText(QStringLiteral("选择文件"));
  buttonBox->button(QDialogButtonBox::Ok)->setEnabled(false);
  layout->addWidget(buttonBox);

  connect(listWidget, &QListWidget::itemSelectionChanged, &dialog, [&]() {
    buttonBox->button(QDialogButtonBox::Ok)
        ->setEnabled(listWidget->currentItem() != nullptr);
  });
  connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

  if (dialog.exec() != QDialog::Accepted || !listWidget->currentItem()) {
    return;
  }

  QListWidgetItem *selectedItem = listWidget->currentItem();
  const QString conversationId =
      selectedItem->data(kTransferRoleConversationId).toString().trimmed();
  const QString conversationName =
      selectedItem->data(kTransferRoleConversationName).toString().trimmed();
  startAttachmentTransferForConversation(
      conversationId, conversationName, QStringLiteral("选择要发送的文件"),
      QStringLiteral("All Files (*.*)"), QStringLiteral("文件"));
}

/**
 * @brief 发起会话列表请求。
 * @param force 布尔参数 `force`。
 * @param silent 布尔参数 `silent`。
 * @return 无返回值。
 */
void Widget::requestConversationList(bool force, bool silent) {
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
  const QString requestId = m_profileApiClient->fetchConversationList(numericId);
  m_pendingConversationListRequestId = requestId;
  if (silent && !requestId.isEmpty()) {
    m_silentConversationListRequestIds.insert(requestId);
  }
}

/**
 * @brief 发起好友列表forcontacts请求。
 * @param force 布尔参数 `force`。
 * @param silent 布尔参数 `silent`。
 * @return 无返回值。
 */
void Widget::requestFriendListForContacts(bool force, bool silent) {
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
  const QString requestId = m_profileApiClient->fetchFriendList(numericId);
  m_pendingFriendListRequestId = requestId;
  if (silent && !requestId.isEmpty()) {
    m_silentFriendListRequestIds.insert(requestId);
  }
}

/**
 * @brief 刷新会话列表界面显示或缓存。
 * @return 无返回值。
 */
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
    state.groupNumericId = conversationItem.groupNumericId.trimmed();
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

/**
 * @brief 刷新群组列表界面显示或缓存。
 * @return 无返回值。
 */
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
    state.groupNumericId = conversationItem.groupNumericId.trimmed();
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

  if (m_leaveGroupDialog) {
    m_leaveGroupDialog->setConversations(conversations);
  }
  if (m_dismissGroupDialog) {
    m_dismissGroupDialog->setConversations(conversations);
  }

}

/**
 * @brief 刷新contact列表界面显示或缓存。
 * @return 无返回值。
 */
void Widget::refreshContactListUi() {
  if (!m_contactList) {
    qWarning() << "[MainWidget] refresh contact list skipped: contact list is null";
    return;
  }
  /**
   * @brief Contacts still depend on LIST_FRIENDS until dedicated contact models are split out.
   * @param arg1 输入参数 `arg1`。
   * @param arg2 输入参数 `arg2`。
   * @return 无返回值。
   */
  friendlist::FriendListManager::refreshListWidget(m_contactList,
                                                   m_friendListManager.friends());
}

/**
 * @brief 更新会话列表item状态。
 * @param conversationItem 会话相关标识或会话数据。
 * @return 无返回值。
 */
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
  state.groupNumericId = conversationItem.groupNumericId.trimmed();
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

/**
 * @brief 执行syncFriendListToDeleteDialog的核心逻辑。
 * @return 无返回值。
 */
void Widget::syncFriendListToDeleteDialog() {
  if (m_deleteFriendDialog) {
    m_deleteFriendDialog->setFriends(m_friendListManager.friends());
  }
  if (m_createGroupDialog) {
    m_createGroupDialog->setFriends(m_friendListManager.friends());
  }
}

/**
 * @brief 处理退出群组结果流程。
 * @param result 处理结果对象。
 * @return 无返回值。
 */
void Widget::handleLeaveGroupResult(const LeaveGroupResult &result) {
  const QString conversationId = result.conversationId.trimmed();
  const QString groupNumericId = result.groupNumericId.trimmed();

  if (!result.ok) {
    QMessageBox::warning(this, QStringLiteral("退出群聊失败"),
                         QStringLiteral("%1\n错误码: %2")
                             .arg(result.message.isEmpty()
                                      ? QStringLiteral("退出群聊失败")
                                      : result.message,
                                  QString::number(result.code)));
    return;
  }

  conversationlist::ConversationItem removedConversation;
  const bool removedFromCache = m_conversationListManager.removeConversation(
      conversationId, groupNumericId, &removedConversation);
  m_conversationStatesByConversationId.remove(conversationId);

  if (!conversationId.isEmpty()) {
    if (QPointer<SessionWindow> sessionWindow =
            m_sessionWindowsByConversationId.value(conversationId)) {
      sessionWindow->close();
    }
  }

  refreshConversationListUi();
  refreshGroupListUi();
  requestConversationList(true);

  Q_UNUSED(removedFromCache);

  const QString groupName = !result.name.trimmed().isEmpty()
                                ? result.name.trimmed()
                                : (!removedConversation.name.trimmed().isEmpty()
                                       ? removedConversation.name.trimmed()
                                       : conversationId);
  QMessageBox::information(this, QStringLiteral("已退出群聊"),
                           QStringLiteral("已退出群聊“%1”").arg(groupName));
}

/**
 * @brief 处理解散群组结果流程。
 * @param result 处理结果对象。
 * @return 无返回值。
 */
void Widget::handleDismissGroupResult(const DismissGroupResult &result) {
  const QString conversationId = result.conversationId.trimmed();
  const QString groupNumericId = result.groupNumericId.trimmed();

  if (!result.ok) {
    QMessageBox::warning(this, QStringLiteral("解散群聊失败"),
                         QStringLiteral("%1\n错误码: %2")
                             .arg(result.message.isEmpty()
                                      ? QStringLiteral("解散群聊失败")
                                      : result.message,
                                  QString::number(result.code)));
    return;
  }

  conversationlist::ConversationItem removedConversation;
  m_conversationListManager.removeConversation(conversationId, groupNumericId,
                                               &removedConversation);
  m_conversationStatesByConversationId.remove(conversationId);

  if (!conversationId.isEmpty()) {
    if (QPointer<SessionWindow> sessionWindow =
            m_sessionWindowsByConversationId.value(conversationId)) {
      sessionWindow->close();
    }
  }

  refreshConversationListUi();
  refreshGroupListUi();
  requestConversationList(true);

  const QString groupName = !result.name.trimmed().isEmpty()
                                ? result.name.trimmed()
                                : (!removedConversation.name.trimmed().isEmpty()
                                       ? removedConversation.name.trimmed()
                                       : conversationId);
  QMessageBox::information(this, QStringLiteral("已解散群聊"),
                           QStringLiteral("已解散群聊“%1”").arg(groupName));
}

/**
 * @brief 响应退出群组完成事件。
 * @param requestId 请求 ID，用于匹配异步请求与响应。
 * @param result 处理结果对象。
 * @return 无返回值。
 */
void Widget::onLeaveGroupFinished(const QString &requestId,
                                  const LeaveGroupResult &result) {
  Q_UNUSED(requestId);
  handleLeaveGroupResult(result);
}

/**
 * @brief 响应解散群组完成事件。
 * @param requestId 请求 ID，用于匹配异步请求与响应。
 * @param result 处理结果对象。
 * @return 无返回值。
 */
void Widget::onDismissGroupFinished(const QString &requestId,
                                    const DismissGroupResult &result) {
  Q_UNUSED(requestId);
  handleDismissGroupResult(result);
}

/**
 * @brief 处理incomingrealtimepayload流程。
 * @param payload 原始载荷字符串。
 * @param sourceTag 来源标识或来源数据。
 * @return 无返回值。
 */
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

/**
 * @brief 处理消息envelope流程。
 * @param envelope 协议封装数据。
 * @return 无返回值。
 */
void Widget::handleMessageEnvelope(const protocol::Envelope &envelope) {
  if (!envelope.requestId.trimmed().isEmpty()) {
    return;
  }
  ChatMessage message;
  QString error;
  if (!MessageSyncClient::parseIncomingSendEnvelope(envelope, &message, &error)) {
    qWarning() << "[MainWidget] ignore MESSAGE/SEND push parse failure:" << error;
    return;
  }
  const QString currentUserId = UserSession::instance().userId().trimmed();
  if (!currentUserId.isEmpty() &&
      message.senderUserId.trimmed() == currentUserId) {
    return;
  }
  storeAndRouteMessage(message, true);
}

/**
 * @brief 处理在线状态envelope流程。
 * @param data 请求或响应数据对象。
 * @return 无返回值。
 */
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

/**
 * @brief 响应会话列表payload接收事件。
 * @param requestId 请求 ID，用于匹配异步请求与响应。
 * @param data 请求或响应数据对象。
 * @return 无返回值。
 */
void Widget::onConversationListPayloadReceived(const QString &requestId,
                                               const QJsonObject &data) {
  m_silentConversationListRequestIds.remove(requestId);
  if (m_pendingConversationListRequestId.isEmpty() ||
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
  beginInitialConversationSyncIfNeeded();
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

/**
 * @brief 响应会话列表失败事件。
 * @param requestId 请求 ID，用于匹配异步请求与响应。
 * @param code 数值参数 `code`。
 * @param message 消息文本或提示信息。
 * @return 无返回值。
 */
void Widget::onConversationListFailed(const QString &requestId, int code,
                                      const QString &message) {
  const bool silentFailure = m_silentConversationListRequestIds.remove(requestId);
  if (m_pendingConversationListRequestId.isEmpty() ||
      requestId != m_pendingConversationListRequestId) {
    return;
  }
  m_pendingConversationListRequestId.clear();
  qWarning() << "[MainWidget] conversation list request failed, code=" << code
             << "message=" << message;
  if (silentFailure) {
    return;
  }
  m_conversationListManager.clear();
  refreshConversationListUi();
  refreshGroupListUi();
}

/**
 * @brief 响应好友列表payload接收事件。
 * @param requestId 请求 ID，用于匹配异步请求与响应。
 * @param data 请求或响应数据对象。
 * @return 无返回值。
 */
void Widget::onFriendListPayloadReceived(const QString &requestId,
                                         const QJsonObject &data) {
  m_silentFriendListRequestIds.remove(requestId);
  if (m_pendingFriendListRequestId.isEmpty() ||
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

/**
 * @brief 响应好友列表失败事件。
 * @param requestId 请求 ID，用于匹配异步请求与响应。
 * @param code 数值参数 `code`。
 * @param message 消息文本或提示信息。
 * @return 无返回值。
 */
void Widget::onFriendListFailed(const QString &requestId, int code,
                                const QString &message) {
  const bool silentFailure = m_silentFriendListRequestIds.remove(requestId);
  if (m_pendingFriendListRequestId.isEmpty() ||
      requestId != m_pendingFriendListRequestId) {
    return;
  }
  m_pendingFriendListRequestId.clear();
  qWarning() << "[MainWidget] friend list request failed, code=" << code
             << "message=" << message;
  if (silentFailure) {
    return;
  }
  m_friendListManager.clear();
  refreshContactListUi();
  syncFriendListToDeleteDialog();
}

/**
 * @brief 响应资料server请求接收事件。
 * @param requestId 请求 ID，用于匹配异步请求与响应。
 * @param action 字符串参数 `action`。
 * @param data 请求或响应数据对象。
 * @return 无返回值。
 */
void Widget::onProfileServerRequestReceived(const QString &requestId,
                                            const QString &action,
                                            const QJsonObject &data) {
  Q_UNUSED(requestId);

  if (action == QStringLiteral("ADD_FRIEND") ||
      action == QStringLiteral("DELETE_FRIEND")) {
    const QString friendEvent =
        data.value(QStringLiteral("friend_event")).toString().trimmed().toLower();
    const QString refreshHint =
        data.value(QStringLiteral("refresh_hint")).toString().trimmed();
    const bool isAddEvent =
        action == QStringLiteral("ADD_FRIEND") &&
        friendEvent == QStringLiteral("added");
    const bool isDeleteEvent =
        action == QStringLiteral("DELETE_FRIEND") &&
        friendEvent == QStringLiteral("deleted");

    if (!isAddEvent && !isDeleteEvent) {
      qInfo().noquote()
          << "[MainWidget] ignore PROFILE friend broadcast action=" << action
          << "friend_event=" << friendEvent;
      return;
    }

    qInfo().noquote()
        << "[MainWidget] refresh friend/conversation lists for PROFILE action="
        << action << "friend_event=" << friendEvent
        << "refresh_hint=" << refreshHint;
    requestFriendListForContacts(true, true);
    requestConversationList(true, true);
    return;
  }

  if (action == QStringLiteral("CREATE_GROUP") ||
      action == QStringLiteral("DISMISS_GROUP")) {
    qInfo().noquote()
        << "[MainWidget] refresh conversation list for server PROFILE action="
        << action;
    requestConversationList(true, true);
  }
}

/**
 * @brief 执行listWidgetForConversationType的核心逻辑。
 * @param conversationType 会话相关标识或会话数据。
 * @return 返回整理后的集合结果。
 */
QListWidget *Widget::listWidgetForConversationType(int conversationType) const {
  return conversationType == 2 ? m_groupList : m_sessionList;
}

/**
 * @brief 查找会话条目In列表。
 * @param listWidget 列表控件对象。
 * @param conversationId 会话 ID。
 * @return 返回整理后的集合结果。
 */
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

/**
 * @brief 查找会话条目By会话ID。
 * @param conversationId 会话 ID。
 * @return 返回整理后的集合结果。
 */
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

/**
 * @brief 执行upsertConversationListItem的核心逻辑。
 * @param state 对象参数 `state`。
 * @param conversationItem 会话相关标识或会话数据。
 * @return 返回整理后的集合结果。
 */
QListWidgetItem *Widget::upsertConversationListItem(
    const ConversationListState &state,
    const conversationlist::ConversationItem *conversationItem) {
  QListWidget *targetList = listWidgetForConversationType(state.conversationType);
  return upsertConversationListItemToList(targetList, state, conversationItem);
}

/**
 * @brief 执行upsertConversationListItemToList的核心逻辑。
 * @param targetList 对象参数 `targetList`。
 * @param state 对象参数 `state`。
 * @param conversationItem 会话相关标识或会话数据。
 * @return 返回整理后的集合结果。
 */
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
  const Session session = Session::create(displayName, sessionType,
                                          state.conversationId,
                                          state.groupNumericId);
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

/**
 * @brief 执行conversationIcon的核心逻辑。
 * @param conversationType 会话相关标识或会话数据。
 * @return 返回图标对象。
 */
QIcon Widget::conversationIcon(int conversationType) const {
  if (!style()) {
    return QIcon();
  }
  return conversationType == 2
             ? style()->standardIcon(QStyle::SP_DirIcon)
             : style()->standardIcon(QStyle::SP_FileDialogContentsView);
}

/**
 * @brief 应用会话状态toitem配置。
 * @param item 数据项对象。
 * @param state 对象参数 `state`。
 * @param conversationItem 会话相关标识或会话数据。
 * @return 无返回值。
 */
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
    const Session session = Session::create(
        displayName.isEmpty() ? QStringLiteral("未知会话") : displayName,
        sessionType, state.conversationId, state.groupNumericId);
    sessionId = session.id();
    m_sessionsById.insert(sessionId, session);
    item->setData(kRoleSessionId, sessionId);
    item->setData(kRoleSessionType,
                  sessionType == Session::Type::Group ? "group" : "direct");
  }

  item->setText(buildSessionItemText(state.conversationType, displayName,
                                     state.groupNumericId, numericId,
                                     isOnline, userStatus,
                                     state.lastMessagePreview, state.memberCount,
                                     item->listWidget() == m_groupList,
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
    QStringList toolTipParts;
    if (!state.groupNumericId.trimmed().isEmpty()) {
      toolTipParts.push_back(
          QStringLiteral("群号: %1").arg(state.groupNumericId.trimmed()));
    }
    if (state.memberCount > 0) {
      toolTipParts.push_back(QStringLiteral("群聊成员数: %1").arg(state.memberCount));
    }
    item->setToolTip(toolTipParts.isEmpty() ? QStringLiteral("群聊")
                                            : toolTipParts.join(QStringLiteral("\n")));
  } else {
    item->setToolTip(friendPresenceText(isOnline, lastSeenAtUtc));
  }
}

/**
 * @brief 执行resetConversationUnread的核心逻辑。
 * @param conversationId 会话 ID。
 * @return 无返回值。
 */
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

/**
 * @brief 构建会话item文本内容。
 * @param conversationType 会话相关标识或会话数据。
 * @param displayName 字符串参数 `displayName`。
 * @param groupNumericId 群组数字编号。
 * @param numericId 数字编号。
 * @param isOnline 在线状态标记。
 * @param userStatus 状态相关参数。
 * @param preview 字符串参数 `preview`。
 * @param memberCount 数值参数 `memberCount`。
 * @param preferGroupMeta 群组相关数据。
 * @param unreadCount 数值参数 `unreadCount`。
 * @return 返回处理后的字符串结果。
 */
QString Widget::buildSessionItemText(int conversationType,
                                     const QString &displayName,
                                     const QString &groupNumericId,
                                     const QString &numericId, bool isOnline,
                                     int userStatus, const QString &preview,
                                     int memberCount,
                                     bool preferGroupMeta,
                                     int unreadCount) const {
  QString firstLine;
  QString previewText;
  if (conversationType == 2) {
    firstLine = displayName;
    QStringList groupMeta;
    if (!groupNumericId.trimmed().isEmpty()) {
      groupMeta.push_back(QStringLiteral("群号: %1").arg(groupNumericId));
    }
    if (memberCount > 0) {
      groupMeta.push_back(QStringLiteral("%1 人").arg(memberCount));
    }
    const QString groupMetaText =
        groupMeta.isEmpty() ? QStringLiteral("群聊")
                            : groupMeta.join(QStringLiteral("  "));
    if (preferGroupMeta || preview.trimmed().isEmpty()) {
      previewText = groupMetaText;
    } else {
      previewText = elidePreview(preview);
    }
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

/**
 * @brief 执行elidePreview的核心逻辑。
 * @param preview 字符串参数 `preview`。
 * @return 返回处理后的字符串结果。
 */
QString Widget::elidePreview(const QString &preview) const {
  QString singleLine = preview;
  singleLine.replace(QLatin1Char('\n'), QLatin1Char(' '));
  singleLine = singleLine.trimmed();
  if (singleLine.size() > 36) {
    return singleLine.left(36) + QStringLiteral("...");
  }
  return singleLine;
}




