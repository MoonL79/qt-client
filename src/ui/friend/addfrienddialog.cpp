#include "addfrienddialog.h"

#include "websocketclient.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QRegularExpression>
#include <QUrl>
#include <QVBoxLayout>
#include <QtGlobal>

namespace {
constexpr int kDefaultStaticPort = 18080;
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

QUrl resolveAvatarUrl(const QString &avatarUrl) {
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

  QUrl url;
  url.setScheme("http");
  url.setHost(resolveServerHost());
  url.setPort(staticPort);
  url.setPath(staticPath);
  return url;
}
} // namespace

AddFriendDialog::AddFriendDialog(const QString &currentUserId,
                                 const QString &currentUserNumericId,
                                 ProfileApiClient *profileApiClient,
                                 QWidget *parent)
    : QDialog(parent), m_profileApiClient(profileApiClient),
      m_currentUserId(currentUserId.trimmed()),
      m_currentUserNumericId(currentUserNumericId.trimmed()) {
  setWindowTitle("添加好友");
  setModal(true);
  resize(420, 280);
  buildUi();
  applyDefaultAvatar();

  if (!m_profileApiClient) {
    m_statusLabel->setText("Profile 服务未初始化");
    m_queryButton->setEnabled(false);
    m_addButton->setEnabled(false);
    return;
  }

  connect(m_profileApiClient, &ProfileApiClient::userProfileQueried, this,
          &AddFriendDialog::onUserProfileQueried);
  connect(m_profileApiClient, &ProfileApiClient::addFriendSuccess, this,
          &AddFriendDialog::onAddFriendSuccess);
  connect(m_profileApiClient, &ProfileApiClient::requestFailedDetailed, this,
          &AddFriendDialog::onRequestFailedDetailed);
}

AddFriendDialog::~AddFriendDialog() {
  if (m_avatarReply) {
    m_avatarReply->abort();
    m_avatarReply->deleteLater();
    m_avatarReply = nullptr;
  }
}

void AddFriendDialog::buildUi() {
  auto *rootLayout = new QVBoxLayout(this);
  rootLayout->setContentsMargins(16, 16, 16, 16);
  rootLayout->setSpacing(10);

  auto *inputLayout = new QHBoxLayout();
  m_numericIdEdit = new QLineEdit(this);
  m_numericIdEdit->setPlaceholderText("输入目标用户编号（numeric_id）");
  m_queryButton = new QPushButton("查询", this);
  inputLayout->addWidget(m_numericIdEdit, 1);
  inputLayout->addWidget(m_queryButton);
  rootLayout->addLayout(inputLayout);
  m_remarkEdit = new QLineEdit(this);
  m_remarkEdit->setPlaceholderText("备注（可选，最多255字符）");
  rootLayout->addWidget(m_remarkEdit);

  auto *contentLayout = new QHBoxLayout();
  contentLayout->setSpacing(12);
  m_avatarLabel = new QLabel(this);
  m_avatarLabel->setFixedSize(64, 64);
  m_avatarLabel->setStyleSheet(
      "border:1px solid #cccccc; border-radius:32px; background:#f2f2f2;");
  m_avatarLabel->setAlignment(Qt::AlignCenter);
  contentLayout->addWidget(m_avatarLabel, 0, Qt::AlignTop);

  auto *form = new QFormLayout();
  m_nicknameValue = new QLabel("-", this);
  m_numericIdValue = new QLabel("-", this);
  m_signatureValue = new QLabel("-", this);
  m_signatureValue->setWordWrap(true);
  m_signatureValue->setMinimumWidth(240);
  form->addRow("昵称", m_nicknameValue);
  form->addRow("numeric_id", m_numericIdValue);
  form->addRow("签名", m_signatureValue);
  contentLayout->addLayout(form, 1);
  rootLayout->addLayout(contentLayout);

  m_statusLabel = new QLabel("请输入用户编号并查询", this);
  m_statusLabel->setStyleSheet("color:#666;");
  rootLayout->addWidget(m_statusLabel);

  auto *buttonBox = new QDialogButtonBox(this);
  m_addButton = buttonBox->addButton("添加好友", QDialogButtonBox::AcceptRole);
  buttonBox->addButton("取消", QDialogButtonBox::RejectRole);
  m_addButton->setEnabled(false);
  rootLayout->addWidget(buttonBox);

  connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
  connect(m_queryButton, &QPushButton::clicked, this,
          &AddFriendDialog::onQueryClicked);
  connect(m_numericIdEdit, &QLineEdit::returnPressed, this,
          &AddFriendDialog::onQueryClicked);
  connect(m_addButton, &QPushButton::clicked, this,
          &AddFriendDialog::onAddFriendClicked);
}

void AddFriendDialog::setQueryLoading(bool loading, const QString &text) {
  m_queryLoading = loading;
  m_queryButton->setEnabled(!loading);
  m_queryButton->setText(loading ? "查询中..." : "查询");
  m_numericIdEdit->setEnabled(!loading && !m_addLoading);
  const bool canAdd = !m_lastQueriedNumericId.isEmpty() && !isQueriedSelf();
  m_addButton->setEnabled(!loading && !m_addLoading && canAdd);
  if (!text.isEmpty()) {
    m_statusLabel->setText(text);
  }
}

void AddFriendDialog::setAddLoading(bool loading, const QString &text) {
  m_addLoading = loading;
  const bool canAdd = !m_lastQueriedNumericId.isEmpty() && !isQueriedSelf();
  m_addButton->setEnabled(!loading && !m_queryLoading && canAdd);
  m_addButton->setText(loading ? "添加中..." : "添加好友");
  m_numericIdEdit->setEnabled(!loading && !m_queryLoading);
  m_queryButton->setEnabled(!loading && !m_queryLoading);
  if (!text.isEmpty()) {
    m_statusLabel->setText(text);
  }
}

void AddFriendDialog::clearQueryResult() {
  m_lastQueriedNumericId.clear();
  m_lastQueriedAvatarUrl.clear();
  m_lastProfile = ProfileInfo();
  m_nicknameValue->setText("-");
  m_numericIdValue->setText("-");
  m_signatureValue->setText("-");
  m_addButton->setEnabled(false);
  if (m_avatarReply) {
    m_avatarReply->abort();
    m_avatarReply->deleteLater();
    m_avatarReply = nullptr;
  }
  applyDefaultAvatar();
}

void AddFriendDialog::applyQueryResult(const ProfileInfo &info) {
  m_lastProfile = info;
  m_lastQueriedNumericId = info.numericId.trimmed();
  m_lastQueriedAvatarUrl = info.avatarUrl.trimmed();
  m_nicknameValue->setText(info.nickname.trimmed().isEmpty() ? "-" : info.nickname);
  m_numericIdValue->setText(info.numericId.trimmed().isEmpty() ? "-" : info.numericId);
  m_signatureValue->setText(info.signature.trimmed().isEmpty() ? "-" : info.signature);
  const bool canAdd = !m_lastQueriedNumericId.isEmpty() && !isQueriedSelf();
  m_addButton->setEnabled(!m_queryLoading && !m_addLoading && canAdd);
  requestAvatar(info.avatarUrl);
}

bool AddFriendDialog::isQueriedSelf() const {
  const QString queriedNumericId = m_lastQueriedNumericId.trimmed();
  if (!queriedNumericId.isEmpty() &&
      !m_currentUserNumericId.trimmed().isEmpty() &&
      queriedNumericId == m_currentUserNumericId.trimmed()) {
    return true;
  }
  const QString queriedUserId = m_lastProfile.userId.trimmed();
  if (!queriedUserId.isEmpty() && !m_currentUserId.trimmed().isEmpty() &&
      queriedUserId == m_currentUserId.trimmed()) {
    return true;
  }
  return false;
}

bool AddFriendDialog::isValidNumericId(const QString &numericId) const {
  static const QRegularExpression kUnsignedIntRe(QStringLiteral("^\\d+$"));
  return kUnsignedIntRe.match(numericId.trimmed()).hasMatch();
}

QString AddFriendDialog::resolveQueryErrorMessage(int code) const {
  if (code == 3001) {
    return QStringLiteral("用户不存在");
  }
  if (code == 3003) {
    return QStringLiteral("用户编号格式错误");
  }
  return QStringLiteral("查询失败，请稍后重试");
}

QString AddFriendDialog::resolveAddFriendErrorMessage(int code) const {
  if (code == 3001) {
    return QStringLiteral("用户不存在");
  }
  if (code == 3002) {
    return QStringLiteral("关系冲突");
  }
  if (code == 3003) {
    return QStringLiteral("参数错误");
  }
  if (code == 1099) {
    return QStringLiteral("服务端异常");
  }
  return QStringLiteral("添加好友失败，请稍后重试");
}

void AddFriendDialog::requestAvatar(const QString &avatarUrl) {
  if (m_avatarReply) {
    m_avatarReply->abort();
    m_avatarReply->deleteLater();
    m_avatarReply = nullptr;
  }

  const QUrl resolved = resolveAvatarUrl(avatarUrl);
  if (!resolved.isValid()) {
    applyDefaultAvatar();
    return;
  }

  QNetworkRequest request(resolved);
  request.setTransferTimeout(8000);
  m_avatarReply = m_avatarNetworkManager.get(request);
  connect(m_avatarReply, &QNetworkReply::finished, this,
          &AddFriendDialog::onAvatarReplyFinished);
}

void AddFriendDialog::applyDefaultAvatar() {
  QPixmap pixmap(m_avatarLabel->size());
  pixmap.fill(Qt::transparent);
  QPainter painter(&pixmap);
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setPen(Qt::NoPen);
  painter.setBrush(QColor("#e0e0e0"));
  painter.drawEllipse(0, 0, pixmap.width(), pixmap.height());
  painter.setPen(QColor("#555555"));
  const QString letter =
      m_lastProfile.nickname.trimmed().isEmpty()
          ? QStringLiteral("U")
          : m_lastProfile.nickname.trimmed().left(1).toUpper();
  painter.drawText(pixmap.rect(), Qt::AlignCenter, letter);
  painter.end();
  m_avatarLabel->setPixmap(pixmap);
}

void AddFriendDialog::onQueryClicked() {
  if (!m_profileApiClient || m_queryLoading || m_addLoading) {
    return;
  }

  const QString numericId = m_numericIdEdit->text().trimmed();
  clearQueryResult();
  if (!isValidNumericId(numericId)) {
    m_statusLabel->setText("用户编号格式错误");
    return;
  }

  setQueryLoading(true, "查询中...");
  m_pendingQueryRequestId = m_profileApiClient->queryUserProfile(numericId);
}

void AddFriendDialog::onAddFriendClicked() {
  if (!m_profileApiClient || m_addLoading || m_queryLoading) {
    return;
  }
  const QString userNumericId = m_currentUserNumericId.trimmed();
  const QString friendNumericId = m_lastQueriedNumericId.trimmed();
  const QString remark = m_remarkEdit ? m_remarkEdit->text().trimmed() : QString();
  if (isQueriedSelf()) {
    m_statusLabel->setText("不能添加自己");
    m_addButton->setEnabled(false);
    return;
  }
  if (!isValidNumericId(userNumericId)) {
    m_statusLabel->setText("当前用户编号无效，请重新登录");
    return;
  }
  if (!isValidNumericId(friendNumericId)) {
    m_statusLabel->setText("请先查询有效用户");
    return;
  }
  if (userNumericId == friendNumericId) {
    m_statusLabel->setText("不能添加自己");
    m_addButton->setEnabled(false);
    return;
  }
  if (remark.size() > 255) {
    m_statusLabel->setText("备注长度不能超过255");
    return;
  }
  setAddLoading(true, "发送好友请求中...");
  m_pendingAddRequestId =
      m_profileApiClient->addFriend(userNumericId, friendNumericId, remark);
}

void AddFriendDialog::onUserProfileQueried(const QString &requestId,
                                           const ProfileInfo &info) {
  if (requestId != m_pendingQueryRequestId) {
    return;
  }
  m_pendingQueryRequestId.clear();
  setQueryLoading(false);
  applyQueryResult(info);
  if (isQueriedSelf()) {
    m_statusLabel->setText("不能添加自己");
    return;
  }
  m_statusLabel->setText("查询成功");
}

void AddFriendDialog::onAddFriendSuccess(const QString &requestId,
                                         const AddFriendResult &result) {
  if (requestId != m_pendingAddRequestId) {
    return;
  }
  m_pendingAddRequestId.clear();
  setAddLoading(false, "添加好友成功");
  const QString successNumericId =
      result.friendNumericId.trimmed().isEmpty() ? m_lastProfile.numericId.trimmed()
                                                 : result.friendNumericId.trimmed();
  QMessageBox::information(this, "成功",
                           QString("已发送添加好友请求，numeric_id=%1")
                               .arg(successNumericId));
  accept();
}

void AddFriendDialog::onRequestFailedDetailed(const QString &requestId,
                                              const QString &action, int code,
                                              const QString &error) {
  if (requestId == m_pendingQueryRequestId && action == "GET") {
    m_pendingQueryRequestId.clear();
    setQueryLoading(false);
    m_addButton->setEnabled(false);
    const QString lowerError = error.trimmed().toLower();
    if (code == 3001 || code == 3003) {
      m_statusLabel->setText(resolveQueryErrorMessage(code));
    } else if (lowerError.contains("timeout") || lowerError.contains("disconnect")) {
      m_statusLabel->setText("查询超时或网络断开，请手动重试");
    } else {
      m_statusLabel->setText(resolveQueryErrorMessage(code));
    }
    return;
  }

  if (requestId == m_pendingAddRequestId && action == "ADD_FRIEND") {
    m_pendingAddRequestId.clear();
    QString message = resolveAddFriendErrorMessage(code);
    if (message == QStringLiteral("添加好友失败，请稍后重试") &&
        !error.trimmed().isEmpty()) {
      message = error.trimmed();
    }
    setAddLoading(false, message);
    return;
  }
}

void AddFriendDialog::onAvatarReplyFinished() {
  QNetworkReply *reply = m_avatarReply.data();
  m_avatarReply = nullptr;
  if (!reply) {
    applyDefaultAvatar();
    return;
  }

  const QVariant statusCode =
      reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
  const int httpCode = statusCode.isValid() ? statusCode.toInt() : 0;
  if (reply->error() != QNetworkReply::NoError || httpCode != 200) {
    applyDefaultAvatar();
    reply->deleteLater();
    return;
  }

  QPixmap pixmap;
  if (!pixmap.loadFromData(reply->readAll())) {
    applyDefaultAvatar();
    reply->deleteLater();
    return;
  }

  const int side = qMin(m_avatarLabel->width(), m_avatarLabel->height());
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

  reply->deleteLater();
}
