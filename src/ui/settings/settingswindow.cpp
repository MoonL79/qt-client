#include "settingswindow.h"
#include "usersession.h"
#include "websocketclient.h"

#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHttpMultiPart>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QNetworkRequest>
#include <QPixmap>
#include <QDebug>
#include <QRegularExpression>
#include <QSet>
#include <QUuid>
#include <QtGlobal>
#include <QVBoxLayout>

namespace {
constexpr qint64 kMaxAvatarFileSizeBytes = 2 * 1024 * 1024;
constexpr int kDefaultStaticPort = 18080;
constexpr const char *kStaticPortEnv = "QT_SERVER_STATIC_PORT";
constexpr const char *kStaticHostEnv = "QT_SERVER_STATIC_HOST";
constexpr const char *kWebSocketHostEnv = "QT_SERVER_WS_HOST";
constexpr const char *kDefaultServerHost = "192.168.14.133";

bool isLoopbackHost(const QString &host) {
  const QString lower = host.trimmed().toLower();
  return lower == "127.0.0.1" || lower == "localhost" || lower == "::1";
}

QString resolveServerHost(const QString &currentAvatarUrl) {
  QString host = qEnvironmentVariable(kStaticHostEnv).trimmed();
  if (host.isEmpty()) {
    const QUrl currentAvatar(currentAvatarUrl.trimmed());
    if (currentAvatar.isValid() && !currentAvatar.host().trimmed().isEmpty() &&
        !isLoopbackHost(currentAvatar.host())) {
      host = currentAvatar.host().trimmed();
    }
  }
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

int resolveStaticPort() {
  bool ok = false;
  int staticPort = qEnvironmentVariableIntValue(kStaticPortEnv, &ok);
  if (!ok || staticPort <= 0 || staticPort > 65535) {
    staticPort = kDefaultStaticPort;
  }
  return staticPort;
}

QString contentTypeFromSuffix(const QString &suffixLower) {
  if (suffixLower == "jpg" || suffixLower == "jpeg") {
    return "image/jpeg";
  }
  if (suffixLower == "png") {
    return "image/png";
  }
  if (suffixLower == "webp") {
    return "image/webp";
  }
  if (suffixLower == "gif") {
    return "image/gif";
  }
  return "application/octet-stream";
}
}

SettingsWindow::SettingsWindow(const QString &userId,
                               ProfileApiClient *profileApiClient,
                               QWidget *parent)
    : QWidget(parent), m_profileApiClient(profileApiClient), m_userId(userId) {
  setAttribute(Qt::WA_DeleteOnClose);
  setWindowTitle("设置");
  resize(560, 420);

  buildUi();

  if (!m_profileApiClient) {
    m_statusLabel->setText("Profile 服务未初始化");
    m_refreshButton->setEnabled(false);
    m_saveButton->setEnabled(false);
    m_chooseAvatarButton->setEnabled(false);
    m_uploadAvatarButton->setEnabled(false);
    return;
  }

  connect(m_profileApiClient, &ProfileApiClient::profileInfoReceived, this,
          &SettingsWindow::onProfileInfoReceived);
  connect(m_profileApiClient, &ProfileApiClient::profileInfoSetSuccess, this,
          &SettingsWindow::onProfileSetSuccess);
  connect(m_profileApiClient, &ProfileApiClient::requestFailed, this,
          &SettingsWindow::onProfileRequestFailed);

  if (!hasValidUserId()) {
    m_statusLabel->setText("user_id 非法，必须为纯数字字符串");
    m_refreshButton->setEnabled(false);
    m_saveButton->setEnabled(false);
    m_chooseAvatarButton->setEnabled(false);
    m_uploadAvatarButton->setEnabled(false);
    return;
  }

  onRefreshClicked();
}

SettingsWindow::~SettingsWindow() {
  if (m_avatarPreviewReply) {
    m_avatarPreviewReply->abort();
    m_avatarPreviewReply->deleteLater();
    m_avatarPreviewReply = nullptr;
  }
  if (m_uploadReply) {
    qInfo() << "[AvatarUpload] cancel pending request, request_id="
            << m_pendingUploadRequestId;
    m_pendingUploadRequestId.clear();
    m_uploadReply->abort();
    m_uploadReply->deleteLater();
    m_uploadReply = nullptr;
  }
}

void SettingsWindow::buildUi() {
  auto *rootLayout = new QVBoxLayout(this);
  rootLayout->setContentsMargins(20, 20, 20, 20);
  rootLayout->setSpacing(14);

  auto *titleLabel = new QLabel("个人资料设置", this);
  titleLabel->setStyleSheet("font-size: 18px; font-weight: bold; color: #ffffff;");
  rootLayout->addWidget(titleLabel);

  auto *hintLabel = new QLabel("可编辑头像 URL、昵称、个人签名。", this);
  hintLabel->setStyleSheet("color: #555;");
  rootLayout->addWidget(hintLabel);

  auto *formLayout = new QFormLayout();
  formLayout->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
  formLayout->setHorizontalSpacing(12);
  formLayout->setVerticalSpacing(12);

  auto *avatarBox = new QWidget(this);
  auto *avatarLayout = new QHBoxLayout(avatarBox);
  avatarLayout->setContentsMargins(0, 0, 0, 0);
  avatarLayout->setSpacing(10);

  m_avatarPreviewLabel = new QLabel(avatarBox);
  m_avatarPreviewLabel->setFixedSize(72, 72);
  m_avatarPreviewLabel->setAlignment(Qt::AlignCenter);
  m_avatarPreviewLabel->setStyleSheet(
      "border: 1px solid #cccccc; border-radius: 36px; background: #f2f2f2;");
  applyDefaultAvatarPreview();

  auto *avatarBtnLayout = new QVBoxLayout();
  avatarBtnLayout->setContentsMargins(0, 0, 0, 0);
  avatarBtnLayout->setSpacing(8);
  m_chooseAvatarButton = new QPushButton("选择头像", avatarBox);
  m_uploadAvatarButton = new QPushButton("上传头像", avatarBox);
  avatarBtnLayout->addWidget(m_chooseAvatarButton);
  avatarBtnLayout->addWidget(m_uploadAvatarButton);
  avatarBtnLayout->addStretch();

  avatarLayout->addWidget(m_avatarPreviewLabel, 0, Qt::AlignVCenter);
  avatarLayout->addLayout(avatarBtnLayout);
  formLayout->addRow("头像上传", avatarBox);

  m_nicknameEdit = new QLineEdit(this);
  m_nicknameEdit->setPlaceholderText("请输入昵称");
  formLayout->addRow("昵称", m_nicknameEdit);

  m_signatureEdit = new QTextEdit(this);
  m_signatureEdit->setPlaceholderText("请输入个人签名");
  m_signatureEdit->setFixedHeight(120);
  formLayout->addRow("个人签名", m_signatureEdit);

  rootLayout->addLayout(formLayout);

  m_statusLabel = new QLabel("就绪", this);
  m_statusLabel->setStyleSheet("color: #666;");
  rootLayout->addWidget(m_statusLabel);

  auto *buttonLayout = new QHBoxLayout();
  buttonLayout->addStretch();

  m_refreshButton = new QPushButton("刷新", this);
  m_saveButton = new QPushButton("保存", this);
  m_saveButton->setDefault(true);

  buttonLayout->addWidget(m_refreshButton);
  buttonLayout->addWidget(m_saveButton);
  rootLayout->addLayout(buttonLayout);

  connect(m_refreshButton, &QPushButton::clicked, this,
          &SettingsWindow::onRefreshClicked);
  connect(m_saveButton, &QPushButton::clicked, this, &SettingsWindow::onSaveClicked);
  connect(m_chooseAvatarButton, &QPushButton::clicked, this,
          &SettingsWindow::onChooseAvatarClicked);
  connect(m_uploadAvatarButton, &QPushButton::clicked, this,
          &SettingsWindow::onUploadAvatarClicked);
  updateActionButtons();
}

bool SettingsWindow::hasValidUserId() const {
  static const QRegularExpression kUnsignedIntRe(QStringLiteral("^\\d+$"));
  return kUnsignedIntRe.match(m_userId.trimmed()).hasMatch();
}

void SettingsWindow::updateActionButtons() {
  const bool busy = m_loading || m_saving || m_uploading;
  m_refreshButton->setEnabled(!busy);
  m_saveButton->setEnabled(!busy);
  m_chooseAvatarButton->setEnabled(!busy);
  m_uploadAvatarButton->setEnabled(!busy && !m_selectedAvatarFilePath.isEmpty());
  m_nicknameEdit->setReadOnly(m_saving || m_uploading);
  m_signatureEdit->setReadOnly(m_saving || m_uploading);
}

void SettingsWindow::setLoading(bool loading, const QString &statusText) {
  m_loading = loading;
  updateActionButtons();
  if (!statusText.isEmpty()) {
    m_statusLabel->setText(statusText);
  }
}

void SettingsWindow::setSaving(bool saving, const QString &statusText) {
  m_saving = saving;
  updateActionButtons();
  if (!statusText.isEmpty()) {
    m_statusLabel->setText(statusText);
  }
}

void SettingsWindow::setUploading(bool uploading, const QString &statusText) {
  m_uploading = uploading;
  m_uploadAvatarButton->setText(uploading ? "上传中..." : "上传头像");
  updateActionButtons();
  if (!statusText.isEmpty()) {
    m_statusLabel->setText(statusText);
  }
}

void SettingsWindow::onRefreshClicked() {
  if (!m_profileApiClient || !hasValidUserId() || m_loading || m_saving ||
      m_uploading) {
    return;
  }

  setLoading(true, "资料加载中...");
  m_pendingGetRequestId = m_profileApiClient->requestProfileInfo(m_userId.trimmed());
}

bool SettingsWindow::validateInput(QString *error) const {
  const QString avatarUrl = m_avatarUrl.trimmed();
  const QString nickname = m_nicknameEdit->text().trimmed();
  const QString signature = m_signatureEdit->toPlainText().trimmed();

  if (avatarUrl.isEmpty()) {
    if (error) {
      *error = "avatar_url 不能为空";
    }
    return false;
  }
  if (nickname.isEmpty()) {
    if (error) {
      *error = "nickname 不能为空";
    }
    return false;
  }
  if (avatarUrl.size() > 255) {
    if (error) {
      *error = "avatar_url 长度不能超过 255";
    }
    return false;
  }
  if (nickname.size() > 64) {
    if (error) {
      *error = "nickname 长度不能超过 64";
    }
    return false;
  }
  if (signature.size() > 255) {
    if (error) {
      *error = "signature 长度不能超过 255";
    }
    return false;
  }

  return true;
}

bool SettingsWindow::validateProfileTextInput(QString *error) const {
  const QString nickname = m_nicknameEdit->text().trimmed();
  const QString signature = m_signatureEdit->toPlainText().trimmed();
  if (nickname.isEmpty()) {
    if (error) {
      *error = "nickname 不能为空";
    }
    return false;
  }
  if (nickname.size() > 64) {
    if (error) {
      *error = "nickname 长度不能超过 64";
    }
    return false;
  }
  if (signature.size() > 255) {
    if (error) {
      *error = "signature 长度不能超过 255";
    }
    return false;
  }
  return true;
}

bool SettingsWindow::validateSelectedAvatarFile(QString *error) const {
  if (m_selectedAvatarFilePath.trimmed().isEmpty()) {
    if (error) {
      *error = "请先选择头像文件";
    }
    return false;
  }

  const QFileInfo info(m_selectedAvatarFilePath);
  if (!info.exists() || !info.isFile()) {
    if (error) {
      *error = "头像文件不存在";
    }
    return false;
  }
  if (!info.isReadable()) {
    if (error) {
      *error = "头像文件不可读";
    }
    return false;
  }

  const QString suffix = info.suffix().trimmed().toLower();
  static const QSet<QString> allowed = {"jpg", "jpeg", "png", "webp", "gif"};
  if (!allowed.contains(suffix)) {
    if (error) {
      *error = "仅支持 jpg/jpeg/png/webp/gif 文件";
    }
    return false;
  }
  if (info.size() > kMaxAvatarFileSizeBytes) {
    if (error) {
      *error = "头像文件不能超过 2MB";
    }
    return false;
  }

  return true;
}

void SettingsWindow::onSaveClicked() {
  if (!m_profileApiClient || !hasValidUserId() || m_loading || m_saving ||
      m_uploading) {
    return;
  }

  QString error;
  if (!validateInput(&error)) {
    m_statusLabel->setText("保存失败: " + error);
    QMessageBox::warning(this, "参数错误", error);
    return;
  }

  const QString avatarUrl = m_avatarUrl.trimmed();
  const QString nickname = m_nicknameEdit->text().trimmed();
  const QString signature = m_signatureEdit->toPlainText().trimmed();

  setSaving(true, "保存中...");
  m_pendingSetRequestId = m_profileApiClient->setProfileInfo(
      m_userId.trimmed(), avatarUrl, nickname, signature);
}

void SettingsWindow::onChooseAvatarClicked() {
  if (m_loading || m_saving || m_uploading) {
    return;
  }

  const QString filePath = QFileDialog::getOpenFileName(
      this, "选择头像", QString(),
      "Images (*.jpg *.jpeg *.png *.webp *.gif)");
  if (filePath.isEmpty()) {
    return;
  }

  m_selectedAvatarFilePath = filePath;
  QString error;
  if (!validateSelectedAvatarFile(&error)) {
    m_selectedAvatarFilePath.clear();
    applyDefaultAvatarPreview();
    updateActionButtons();
    m_statusLabel->setText("选择头像失败: " + error);
    QMessageBox::warning(this, "选择头像失败", error);
    return;
  }

  updateAvatarPreviewFromLocal(filePath);
  updateActionButtons();
  m_statusLabel->setText("头像已选择，点击“上传头像”提交。");
}

QUrl SettingsWindow::buildUploadEndpoint() const {
  const int staticPort = resolveStaticPort();
  const QString host = resolveServerHost(m_avatarUrl);
  QUrl uploadUrl;
  uploadUrl.setScheme("http");
  uploadUrl.setHost(host);
  uploadUrl.setPort(staticPort);
  uploadUrl.setPath("/upload/avatar");
  return uploadUrl;
}

QUrl SettingsWindow::resolveAvatarUrlForPreview(const QString &avatarUrl) const {
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
      absolute.setHost(resolveServerHost(m_avatarUrl));
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

  QUrl url;
  url.setScheme("http");
  url.setHost(resolveServerHost(m_avatarUrl));
  url.setPort(resolveStaticPort());
  url.setPath(staticPath);
  return url;
}

void SettingsWindow::onUploadAvatarClicked() {
  if (!hasValidUserId() || m_loading || m_saving || m_uploading) {
    return;
  }

  QString error;
  if (!validateSelectedAvatarFile(&error)) {
    m_statusLabel->setText("上传失败: " + error);
    QMessageBox::warning(this, "上传失败", error);
    return;
  }
  if (!validateProfileTextInput(&error)) {
    m_statusLabel->setText("上传失败: " + error);
    QMessageBox::warning(this, "上传失败", error);
    return;
  }

  const UserSession &session = UserSession::instance();
  if (!session.isLoggedIn()) {
    const QString message = "未登录，请先登录后再上传头像。";
    m_statusLabel->setText("上传失败: " + message);
    QMessageBox::warning(this, "上传失败", message);
    return;
  }
  if (session.userId() != m_userId.trimmed()) {
    const QString message = "用户身份不匹配，请重新登录。";
    m_statusLabel->setText("上传失败: " + message);
    QMessageBox::warning(this, "上传失败", message);
    return;
  }
  if (!session.hasValidUploadToken()) {
    const QString message =
        "上传凭证失效，请重新登录。";
    m_statusLabel->setText("上传失败: " + message);
    QMessageBox::warning(this, "上传失败", message);
    return;
  }

  QFile *file = new QFile(m_selectedAvatarFilePath);
  if (!file->open(QIODevice::ReadOnly)) {
    const QString message = "头像文件打开失败，请重试。";
    file->deleteLater();
    m_statusLabel->setText("上传失败: " + message);
    QMessageBox::warning(this, "上传失败", message);
    return;
  }

  QHttpMultiPart *multiPart = new QHttpMultiPart(QHttpMultiPart::FormDataType);

  QHttpPart userIdPart;
  userIdPart.setHeader(QNetworkRequest::ContentDispositionHeader,
                       QVariant("form-data; name=\"user_id\""));
  userIdPart.setBody(m_userId.trimmed().toUtf8());
  multiPart->append(userIdPart);

  const QFileInfo fileInfo(*file);
  const QString suffixLower = fileInfo.suffix().trimmed().toLower();

  QHttpPart filePart;
  filePart.setHeader(QNetworkRequest::ContentTypeHeader,
                     QVariant(contentTypeFromSuffix(suffixLower)));
  filePart.setHeader(
      QNetworkRequest::ContentDispositionHeader,
      QVariant(QString("form-data; name=\"file\"; filename=\"%1\"")
                   .arg(fileInfo.fileName())));
  filePart.setBodyDevice(file);
  file->setParent(multiPart);
  multiPart->append(filePart);

  const QUrl uploadUrl = buildUploadEndpoint();
  if (!uploadUrl.isValid()) {
    multiPart->deleteLater();
    const QString message = "上传地址无效";
    m_statusLabel->setText("上传失败: " + message);
    QMessageBox::warning(this, "上传失败", message);
    return;
  }

  QNetworkRequest request(uploadUrl);
  m_pendingUploadRequestId =
      QUuid::createUuid().toString(QUuid::WithoutBraces);
  request.setRawHeader("Authorization",
                       session.authorizationHeaderValue().toUtf8());
  request.setTransferTimeout(20000);

  m_uploadReply = m_uploadNetworkManager.post(request, multiPart);
  multiPart->setParent(m_uploadReply);
  connect(m_uploadReply, &QNetworkReply::finished, this,
          &SettingsWindow::onUploadReplyFinished);
  qInfo() << "[AvatarUpload] send request_id=" << m_pendingUploadRequestId
          << "user_id=" << m_userId.trimmed() << "url=" << uploadUrl.toString();
  setUploading(true, "头像上传中...");
}

void SettingsWindow::applyProfileToUi(const ProfileInfo &info) {
  m_avatarUrl = info.avatarUrl.trimmed();
  m_nicknameEdit->setText(info.nickname);
  m_signatureEdit->setPlainText(info.signature);
  updateAvatarPreviewFromUrl(info.avatarUrl);
}

void SettingsWindow::updateAvatarPreviewFromLocal(const QString &filePath) {
  QPixmap pixmap(filePath);
  if (pixmap.isNull()) {
    applyDefaultAvatarPreview();
    return;
  }
  const QPixmap scaled =
      pixmap.scaled(m_avatarPreviewLabel->size(), Qt::KeepAspectRatioByExpanding,
                    Qt::SmoothTransformation);
  m_avatarPreviewLabel->setPixmap(scaled);
}

void SettingsWindow::updateAvatarPreviewFromUrl(const QString &avatarUrl) {
  if (!m_selectedAvatarFilePath.isEmpty()) {
    updateAvatarPreviewFromLocal(m_selectedAvatarFilePath);
    return;
  }

  if (m_avatarPreviewReply) {
    m_avatarPreviewReply->abort();
    m_avatarPreviewReply->deleteLater();
    m_avatarPreviewReply = nullptr;
  }

  const QUrl resolved = resolveAvatarUrlForPreview(avatarUrl);
  if (!resolved.isValid()) {
    applyDefaultAvatarPreview();
    return;
  }

  QNetworkRequest request(resolved);
  request.setTransferTimeout(8000);
  m_avatarPreviewReply = m_uploadNetworkManager.get(request);
  connect(m_avatarPreviewReply, &QNetworkReply::finished, this,
          &SettingsWindow::onAvatarPreviewReplyFinished);
}

void SettingsWindow::onAvatarPreviewReplyFinished() {
  QNetworkReply *reply = m_avatarPreviewReply.data();
  m_avatarPreviewReply = nullptr;
  if (!reply) {
    applyDefaultAvatarPreview();
    return;
  }

  const QVariant statusCode =
      reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
  const int httpCode = statusCode.isValid() ? statusCode.toInt() : 0;
  if (reply->error() != QNetworkReply::NoError || httpCode != 200) {
    applyDefaultAvatarPreview();
    reply->deleteLater();
    return;
  }

  QPixmap pixmap;
  if (!pixmap.loadFromData(reply->readAll())) {
    applyDefaultAvatarPreview();
    reply->deleteLater();
    return;
  }
  const QPixmap scaled =
      pixmap.scaled(m_avatarPreviewLabel->size(), Qt::KeepAspectRatioByExpanding,
                    Qt::SmoothTransformation);
  m_avatarPreviewLabel->setPixmap(scaled);
  m_avatarPreviewLabel->setText(QString());
  reply->deleteLater();
}

void SettingsWindow::applyDefaultAvatarPreview() {
  if (!m_avatarPreviewLabel) {
    return;
  }
  m_avatarPreviewLabel->setPixmap(QPixmap());
  m_avatarPreviewLabel->setText("头像");
}

QString SettingsWindow::extractMessageFromJson(const QJsonObject &obj) const {
  const QString message = obj.value("message").toString().trimmed();
  return message.isEmpty() ? QStringLiteral("请求失败") : message;
}

void SettingsWindow::onUploadReplyFinished() {
  QNetworkReply *reply = m_uploadReply.data();
  m_uploadReply = nullptr;
  const QString requestId = m_pendingUploadRequestId;
  m_pendingUploadRequestId.clear();
  if (!reply) {
    setUploading(false, QString());
    return;
  }

  const QVariant statusCodeAttr =
      reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
  const int httpCode = statusCodeAttr.isValid() ? statusCodeAttr.toInt() : 0;
  const QByteArray body = reply->readAll();
  if (reply->error() != QNetworkReply::NoError) {
    QString message;
    if (httpCode == 401) {
      message = QStringLiteral("上传凭证失效，请重新登录");
    } else if (httpCode == 413) {
      message = QStringLiteral("头像超过 2MB 限制");
    } else if (httpCode == 415) {
      message = QStringLiteral("仅支持 jpg/png/webp/gif");
    } else {
      message = QStringLiteral("网络异常，请稍后重试");
    }
    qWarning() << "[AvatarUpload] failed request_id=" << requestId
               << "user_id=" << m_userId.trimmed() << "http_code=" << httpCode
               << "message=" << message << "error=" << reply->errorString();
    setUploading(false, "上传失败: " + message);
    QMessageBox::warning(this, "上传失败", message);
    reply->deleteLater();
    return;
  }

  QJsonParseError parseError;
  const QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);
  if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
    const QString message = QStringLiteral("上传响应解析失败");
    qWarning() << "[AvatarUpload] parse failed request_id=" << requestId
               << "user_id=" << m_userId.trimmed() << "http_code=" << httpCode
               << "message=" << message;
    setUploading(false, "上传失败: " + message);
    QMessageBox::warning(this, "上传失败", message);
    reply->deleteLater();
    return;
  }

  const QJsonObject obj = doc.object();
  const bool ok = obj.value("ok").toBool(false);
  if (!ok) {
    QString message = extractMessageFromJson(obj);
    if (httpCode == 401) {
      message = QStringLiteral("上传凭证失效，请重新登录");
    } else if (httpCode == 413) {
      message = QStringLiteral("头像超过 2MB 限制");
    } else if (httpCode == 415) {
      message = QStringLiteral("仅支持 jpg/png/webp/gif");
    }
    qWarning() << "[AvatarUpload] business failed request_id=" << requestId
               << "user_id=" << m_userId.trimmed() << "http_code=" << httpCode
               << "message=" << message;
    setUploading(false, "上传失败: " + message);
    QMessageBox::warning(this, "上传失败", message);
    reply->deleteLater();
    return;
  }

  const QString avatarUrl = obj.value("avatar_url").toString().trimmed();
  if (avatarUrl.isEmpty()) {
    const QString message = QStringLiteral("上传成功但未返回 avatar_url");
    qWarning() << "[AvatarUpload] empty avatar_url request_id=" << requestId
               << "user_id=" << m_userId.trimmed() << "http_code=" << httpCode
               << "message=" << message;
    setUploading(false, "上传失败: " + message);
    QMessageBox::warning(this, "上传失败", message);
    reply->deleteLater();
    return;
  }

  m_avatarUrl = avatarUrl;
  updateAvatarPreviewFromLocal(m_selectedAvatarFilePath);
  qInfo() << "[AvatarUpload] success request_id=" << requestId
          << "user_id=" << m_userId.trimmed() << "http_code=" << httpCode
          << "message=" << extractMessageFromJson(obj);
  setUploading(false, "头像上传成功，正在保存资料...");
  QMessageBox::information(this, "成功", "头像上传成功");

  const QString nickname = m_nicknameEdit->text().trimmed();
  const QString signature = m_signatureEdit->toPlainText().trimmed();
  if (!m_profileApiClient) {
    setUploading(false, "保存失败: Profile 服务未初始化");
    QMessageBox::warning(this, "保存失败", "Profile 服务未初始化");
    reply->deleteLater();
    return;
  }
  setSaving(true, "保存中...");
  m_pendingSetRequestId = m_profileApiClient->setProfileInfo(
      m_userId.trimmed(), avatarUrl, nickname, signature);
  reply->deleteLater();
}

void SettingsWindow::onProfileInfoReceived(const QString &requestId,
                                           const ProfileInfo &info) {
  if (requestId != m_pendingGetRequestId) {
    return;
  }
  m_pendingGetRequestId.clear();
  setLoading(false, "资料加载成功");
  applyProfileToUi(info);
}

void SettingsWindow::onProfileSetSuccess(const QString &requestId,
                                         const ProfileInfo &info) {
  if (requestId != m_pendingSetRequestId) {
    return;
  }
  m_pendingSetRequestId.clear();
  setSaving(false, "保存成功");
  applyProfileToUi(info);
  emit profileApplied(info.nickname.trimmed().isEmpty() ? m_userId.trimmed()
                                                        : info.nickname.trimmed(),
                    info.avatarUrl);
  QMessageBox::information(this, "成功", "个人资料保存成功");
}

void SettingsWindow::onProfileRequestFailed(const QString &requestId,
                                            const QString &action,
                                            const QString &error) {
  if (requestId == m_pendingGetRequestId && action == "GET_INFO") {
    m_pendingGetRequestId.clear();
    setLoading(false, "加载失败: " + error);
    QMessageBox::warning(this, "加载失败", error);
    return;
  }

  if (requestId == m_pendingSetRequestId && action == "SET_INFO") {
    m_pendingSetRequestId.clear();
    setSaving(false, "保存失败: " + error);
    QMessageBox::warning(this, "保存失败", error);
  }
}
