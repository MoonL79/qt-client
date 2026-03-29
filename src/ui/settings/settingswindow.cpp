#include "settingswindow.h"
#include "usersession.h"
#include "websocketclient.h"

#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QEvent>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHttpMultiPart>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QNetworkRequest>
#include <QPixmap>
#include <QDebug>
#include <QFrame>
#include <QRegularExpression>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QPropertyAnimation>
#include <QSet>
#include <QSignalBlocker>
#include <QStyle>
#include <QTabBar>
#include <QTimer>
#include <QUuid>
#include <QtGlobal>
#include <QVBoxLayout>
#include <functional>
#include <memory>

namespace {
constexpr qint64 kMaxAvatarFileSizeBytes = 2 * 1024 * 1024;
constexpr int kDefaultStaticPort = 18080;
constexpr const char *kStaticPortEnv = "QT_SERVER_STATIC_PORT";
constexpr const char *kStaticHostEnv = "QT_SERVER_STATIC_HOST";
constexpr const char *kWebSocketHostEnv = "QT_SERVER_WS_HOST";
constexpr const char *kDefaultServerHost = "192.168.14.133";

class SaturationValuePalette : public QWidget {
public:
  explicit SaturationValuePalette(QWidget *parent = nullptr) : QWidget(parent) {
    setMinimumSize(220, 220);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  }

  void setHue(int hue) {
    hue = qBound(0, hue, 359);
    if (m_hue == hue) {
      return;
    }
    m_hue = hue;
    update();
  }

  void setColor(const QColor &color) {
    const QColor hsv = color.toHsv();
    const int hue = hsv.hue() < 0 ? m_hue : hsv.hue();
    const qreal saturation = hsv.hsvSaturationF() < 0.0 ? 0.0 : hsv.hsvSaturationF();
    const qreal value = hsv.valueF() < 0.0 ? 0.0 : hsv.valueF();
    const bool changed = m_hue != hue || !qFuzzyCompare(m_saturation, saturation) ||
                         !qFuzzyCompare(m_value, value);
    m_hue = hue;
    m_saturation = saturation;
    m_value = value;
    if (changed) {
      update();
    }
  }

  QColor color() const {
    return QColor::fromHsvF(m_hue / 359.0, m_saturation, m_value);
  }

  std::function<void(const QColor &)> onColorChanged;

protected:
  void paintEvent(QPaintEvent *event) override {
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QRect content = rect().adjusted(4, 4, -4, -4);
    QImage gradient(content.size(), QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < gradient.height(); ++y) {
      const qreal value = 1.0 - static_cast<qreal>(y) /
                                    qMax(1, gradient.height() - 1);
      QRgb *scanLine = reinterpret_cast<QRgb *>(gradient.scanLine(y));
      for (int x = 0; x < gradient.width(); ++x) {
        const qreal saturation = static_cast<qreal>(x) /
                                 qMax(1, gradient.width() - 1);
        scanLine[x] = QColor::fromHsvF(m_hue / 359.0, saturation, value).rgba();
      }
    }

    QPainterPath clipPath;
    clipPath.addRoundedRect(content, 12, 12);
    painter.setClipPath(clipPath);
    painter.drawImage(content.topLeft(), gradient);
    painter.setClipping(false);

    painter.setPen(QPen(QColor(255, 255, 255, 70), 1));
    painter.drawRoundedRect(content, 12, 12);

    const QPointF handle = handleCenter(content);
    painter.setPen(QPen(QColor("#ffffff"), 2));
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(handle, 8, 8);
    painter.setPen(QPen(QColor(15, 23, 42, 190), 2));
    painter.drawEllipse(handle, 11, 11);
  }

  void mousePressEvent(QMouseEvent *event) override { updateFromPosition(event->pos()); }

  void mouseMoveEvent(QMouseEvent *event) override {
    if (event->buttons() & Qt::LeftButton) {
      updateFromPosition(event->pos());
    }
  }

private:
  QRect contentRect() const { return rect().adjusted(4, 4, -4, -4); }

  QPointF handleCenter(const QRect &content) const {
    const qreal x = content.left() + m_saturation * content.width();
    const qreal y = content.top() + (1.0 - m_value) * content.height();
    return QPointF(x, y);
  }

  void updateFromPosition(const QPoint &pos) {
    const QRect content = contentRect();
    const qreal x =
        qBound(static_cast<qreal>(content.left()), static_cast<qreal>(pos.x()),
               static_cast<qreal>(content.right()));
    const qreal y =
        qBound(static_cast<qreal>(content.top()), static_cast<qreal>(pos.y()),
               static_cast<qreal>(content.bottom()));

    m_saturation = (x - content.left()) / qMax(1, content.width());
    m_value = 1.0 - (y - content.top()) / qMax(1, content.height());
    update();
    if (onColorChanged) {
      onColorChanged(color());
    }
  }

  int m_hue = 210;
  qreal m_saturation = 0.75;
  qreal m_value = 0.85;
};

class HueSlider : public QWidget {
public:
  explicit HueSlider(QWidget *parent = nullptr) : QWidget(parent) {
    setFixedWidth(28);
    setMinimumHeight(220);
  }

  void setHue(int hue) {
    hue = qBound(0, hue, 359);
    if (m_hue == hue) {
      return;
    }
    m_hue = hue;
    update();
  }

  int hue() const { return m_hue; }

  std::function<void(int)> onHueChanged;

protected:
  void paintEvent(QPaintEvent *event) override {
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QRect content = rect().adjusted(6, 6, -6, -6);
    QLinearGradient gradient(content.topLeft(), content.bottomLeft());
    gradient.setColorAt(0.0, QColor::fromHsv(0, 255, 255));
    gradient.setColorAt(1.0 / 6.0, QColor::fromHsv(60, 255, 255));
    gradient.setColorAt(2.0 / 6.0, QColor::fromHsv(120, 255, 255));
    gradient.setColorAt(3.0 / 6.0, QColor::fromHsv(180, 255, 255));
    gradient.setColorAt(4.0 / 6.0, QColor::fromHsv(240, 255, 255));
    gradient.setColorAt(5.0 / 6.0, QColor::fromHsv(300, 255, 255));
    gradient.setColorAt(1.0, QColor::fromHsv(359, 255, 255));

    painter.setPen(Qt::NoPen);
    painter.setBrush(gradient);
    painter.drawRoundedRect(content, 10, 10);

    painter.setPen(QPen(QColor(255, 255, 255, 70), 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(content, 10, 10);

    const qreal ratio = m_hue / 359.0;
    const qreal y = content.top() + ratio * content.height();
    QRectF handleRect(content.left() - 3, y - 5, content.width() + 6, 10);
    painter.setPen(QPen(QColor("#ffffff"), 2));
    painter.drawRoundedRect(handleRect, 5, 5);
  }

  void mousePressEvent(QMouseEvent *event) override { updateFromPosition(event->pos()); }

  void mouseMoveEvent(QMouseEvent *event) override {
    if (event->buttons() & Qt::LeftButton) {
      updateFromPosition(event->pos());
    }
  }

private:
  QRect contentRect() const { return rect().adjusted(6, 6, -6, -6); }

  void updateFromPosition(const QPoint &pos) {
    const QRect content = contentRect();
    const qreal y =
        qBound(static_cast<qreal>(content.top()), static_cast<qreal>(pos.y()),
               static_cast<qreal>(content.bottom()));
    m_hue = qRound(((y - content.top()) / qMax(1, content.height())) * 359.0);
    update();
    if (onHueChanged) {
      onHueChanged(m_hue);
    }
  }

  int m_hue = 210;
};

class CurrentTabSizeHintWidget : public QTabWidget {
public:
  explicit CurrentTabSizeHintWidget(QWidget *parent = nullptr) : QTabWidget(parent) {}

  QSize sizeHint() const override { return currentTabSize(); }

  QSize minimumSizeHint() const override { return currentTabSize(); }

private:
  QSize currentTabSize() const {
    QSize size = QTabWidget::sizeHint();
    QWidget *page = currentWidget();
    if (!page) {
      return size;
    }

    const QSize pageSize = page->sizeHint();
    const int tabBarHeight = tabBar() ? tabBar()->sizeHint().height() : 0;
    const int frameWidth = style()->pixelMetric(QStyle::PM_DefaultFrameWidth, nullptr, this);
    const QMargins margins = contentsMargins();
    const int width = qMax(size.width(),
                           pageSize.width() + margins.left() + margins.right() +
                               frameWidth * 2);
    const int height = pageSize.height() + tabBarHeight + margins.top() +
                       margins.bottom() + frameWidth * 2 + 8;
    return QSize(width, height);
  }
};

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

QString settingsTabsStyleSheetForColor(const QColor &color) {
  const QColor accent = color.isValid() ? color : QColor(QStringLiteral("#3B82F6"));
  return QStringLiteral(
             "QTabWidget::pane { border: none; background: #ffffff; }"
             "QTabBar::tab { background: #f8fafc; color: #374151; padding: 8px 18px; "
             "margin-right: 6px; border: 1px solid #e5e7eb; border-radius: 8px; }"
             "QTabBar::tab:selected { background: %1; color: #111827; font-weight: 600; border-color: %2; }"
             "QTabBar::tab:hover { background: %3; }")
      .arg(accent.lighter(150).name(QColor::HexRgb), accent.lighter(135).name(QColor::HexRgb),
           accent.lighter(180).name(QColor::HexRgb));
}

QString primaryButtonStyleSheetForColor(const QColor &color) {
  const QColor accent = color.isValid() ? color : QColor(QStringLiteral("#3B82F6"));
  return QStringLiteral(
             "QPushButton { background: %1; color: #ffffff; border: none; border-radius: 8px; "
             "padding: 8px 16px; font-weight: 600; }"
             "QPushButton:hover { background: %2; }"
             "QPushButton:pressed { background: %3; }"
             "QPushButton:disabled { background: #cbd5e1; color: #f8fafc; }")
      .arg(accent.name(QColor::HexRgb), accent.lighter(110).name(QColor::HexRgb),
           accent.darker(110).name(QColor::HexRgb));
}

QString secondaryButtonStyleSheet() {
  return QStringLiteral(
      "QPushButton { background: #f8fafc; color: #374151; border: 1px solid #d1d5db; border-radius: 8px; "
      "padding: 8px 16px; font-weight: 600; }"
      "QPushButton:hover { background: #eef2f7; color: #111827; border-color: #cbd5e1; }"
      "QPushButton:pressed { background: #e5e7eb; }"
      "QPushButton:disabled { background: #f8fafc; color: #9ca3af; border-color: #e5e7eb; }");
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
    : QWidget(parent), m_profileApiClient(profileApiClient), m_userId(userId),
      m_themeColor(QStringLiteral("#3B82F6")),
      m_authApiClient(websocketclient::instance(), this) {
  setWindowFlags(Qt::FramelessWindowHint | Qt::Dialog);
  setAttribute(Qt::WA_DeleteOnClose);
  setObjectName(QStringLiteral("settingsWindow"));
  setWindowTitle("设置");
  setStyleSheet(QStringLiteral("#settingsWindow { background: #ffffff; }"));
  resize(560, 420);

  buildUi();
  m_defaultMinimumHeight = minimumHeight();
  m_defaultMaximumHeight = maximumHeight();
  QTimer::singleShot(0, this, [this]() { adjustWindowSizeForCurrentTab(false); });

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
  connect(&m_authApiClient, &AuthApiClient::logoutSucceeded, this,
          &SettingsWindow::onLogoutSucceeded);
  connect(&m_authApiClient, &AuthApiClient::authRequestFailed, this,
          &SettingsWindow::onAuthRequestFailed);

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

bool SettingsWindow::eventFilter(QObject *watched, QEvent *event) {
  if (watched == m_titleBar) {
    switch (event->type()) {
    case QEvent::MouseButtonPress: {
      auto *mouseEvent = static_cast<QMouseEvent *>(event);
      if (mouseEvent->button() == Qt::LeftButton) {
        QWidget *child = m_titleBar->childAt(mouseEvent->position().toPoint());
        if (child != m_titleCloseButton) {
          m_dragging = true;
          m_dragOffset = mouseEvent->globalPosition().toPoint() - frameGeometry().topLeft();
          m_titleBar->setCursor(Qt::ClosedHandCursor);
          return true;
        }
      }
      break;
    }
    case QEvent::MouseMove: {
      auto *mouseEvent = static_cast<QMouseEvent *>(event);
      if (m_dragging && (mouseEvent->buttons() & Qt::LeftButton)) {
        move(mouseEvent->globalPosition().toPoint() - m_dragOffset);
        return true;
      }
      break;
    }
    case QEvent::MouseButtonRelease: {
      auto *mouseEvent = static_cast<QMouseEvent *>(event);
      if (mouseEvent->button() == Qt::LeftButton) {
        m_dragging = false;
        m_titleBar->setCursor(Qt::OpenHandCursor);
        return true;
      }
      break;
    }
    default:
      break;
    }
  }

  return QWidget::eventFilter(watched, event);
}

void SettingsWindow::buildUi() {
  auto *rootLayout = new QVBoxLayout(this);
  rootLayout->setContentsMargins(20, 14, 20, 20);
  rootLayout->setSpacing(12);

  m_titleBar = new QWidget(this);
  m_titleBar->setFixedHeight(40);
  m_titleBar->setCursor(Qt::OpenHandCursor);
  m_titleBar->installEventFilter(this);

  auto *titleBarLayout = new QHBoxLayout(m_titleBar);
  titleBarLayout->setContentsMargins(0, 0, 0, 0);
  titleBarLayout->setSpacing(8);

  m_titleBarLabel = new QLabel(windowTitle(), m_titleBar);
  m_titleBarLabel->setStyleSheet(
      "font-size: 15px; font-weight: 600; color: #111827;");
  titleBarLayout->addWidget(m_titleBarLabel);
  titleBarLayout->addStretch();

  m_titleCloseButton = new QPushButton(QStringLiteral("×"), m_titleBar);
  m_titleCloseButton->setCursor(Qt::ArrowCursor);
  m_titleCloseButton->setFixedSize(30, 30);
  m_titleCloseButton->setStyleSheet(
      "QPushButton { border: none; color: #4b5563; font-size: 18px; background: transparent; border-radius: 6px; }"
      "QPushButton:hover { background: #ef4444; color: #ffffff; }");
  connect(m_titleCloseButton, &QPushButton::clicked, this, &QWidget::close);
  titleBarLayout->addWidget(m_titleCloseButton);

  rootLayout->addWidget(m_titleBar);

  m_tabWidget = new CurrentTabSizeHintWidget(this);
  m_tabWidget->setDocumentMode(true);
  m_tabWidget->tabBar()->setDrawBase(false);
  rootLayout->addWidget(m_tabWidget);

  auto *userTab = new QWidget(m_tabWidget);
  auto *userLayout = new QVBoxLayout(userTab);
  userLayout->setContentsMargins(0, 8, 0, 0);
  userLayout->setSpacing(12);

  auto *formLayout = new QFormLayout();
  formLayout->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
  formLayout->setHorizontalSpacing(12);
  formLayout->setVerticalSpacing(12);

  auto *avatarBox = new QWidget(userTab);
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

  m_nicknameEdit = new QLineEdit(userTab);
  m_nicknameEdit->setPlaceholderText("请输入昵称");
  m_nicknameEdit->setStyleSheet(
      "QLineEdit { background: #ffffff; border: 1px solid #d1d5db; border-radius: 8px; padding: 8px 10px; color: #111827; }"
      "QLineEdit:focus { border-color: #93c5fd; }");
  formLayout->addRow("昵称", m_nicknameEdit);

  m_signatureEdit = new QTextEdit(userTab);
  m_signatureEdit->setPlaceholderText("请输入个人签名");
  m_signatureEdit->setFixedHeight(120);
  m_signatureEdit->setStyleSheet(
      "QTextEdit { background: #ffffff; border: 1px solid #d1d5db; border-radius: 8px; padding: 8px 10px; color: #111827; }"
      "QTextEdit:focus { border-color: #93c5fd; }");
  formLayout->addRow("个人签名", m_signatureEdit);

  userLayout->addLayout(formLayout);
  userLayout->addStretch();

  auto *appearanceTab = new QWidget(m_tabWidget);
  auto *appearanceLayout = new QVBoxLayout(appearanceTab);
  appearanceLayout->setContentsMargins(0, 8, 0, 0);
  appearanceLayout->setSpacing(14);

  auto *appearanceTitle = new QLabel(QStringLiteral("主题颜色"), appearanceTab);
  appearanceTitle->setStyleSheet(
      "font-size: 16px; font-weight: 600; color: #111827;");
  appearanceLayout->addWidget(appearanceTitle);

  auto *appearanceHint =
      new QLabel(QStringLiteral("调整主题颜色，实时影响界面中的关键高亮区域。"),
                 appearanceTab);
  appearanceHint->setStyleSheet("color: #6b7280;");
  appearanceLayout->addWidget(appearanceHint);

  auto *paletteCard = new QWidget(appearanceTab);
  paletteCard->setStyleSheet(
      "background: #ffffff; border: 1px solid #e5e7eb; border-radius: 14px;");
  auto *paletteCardLayout = new QVBoxLayout(paletteCard);
  paletteCardLayout->setContentsMargins(18, 18, 18, 18);
  paletteCardLayout->setSpacing(16);

  auto *pickerLayout = new QHBoxLayout();
  pickerLayout->setSpacing(18);

  auto *svPalette = new SaturationValuePalette(paletteCard);
  auto *hueSlider = new HueSlider(paletteCard);
  pickerLayout->addWidget(svPalette, 1);
  pickerLayout->addWidget(hueSlider, 0, Qt::AlignTop);

  auto *controlPanel = new QFrame(paletteCard);
  controlPanel->setStyleSheet(
      "background: #ffffff; border: 1px solid #e5e7eb; "
      "border-radius: 12px;");
  controlPanel->setMinimumWidth(220);
  auto *controlLayout = new QVBoxLayout(controlPanel);
  controlLayout->setContentsMargins(16, 16, 16, 16);
  controlLayout->setSpacing(12);

  auto *previewSwatch = new QFrame(controlPanel);
  previewSwatch->setFixedHeight(84);
  previewSwatch->setStyleSheet(
      "background: #3b82f6; border-radius: 12px; border: 1px solid rgba(255,255,255,0.10);");
  controlLayout->addWidget(previewSwatch);

  auto *previewName = new QLabel(QStringLiteral("当前颜色"), controlPanel);
  previewName->setStyleSheet("color: #111827; font-size: 14px; font-weight: 600;");
  controlLayout->addWidget(previewName);

  auto *hexLabel = new QLabel(QStringLiteral("Hex"), controlPanel);
  hexLabel->setStyleSheet("color: #6b7280;");
  controlLayout->addWidget(hexLabel);

  auto *hexEdit = new QLineEdit(controlPanel);
  hexEdit->setPlaceholderText(QStringLiteral("#3B82F6"));
  hexEdit->setStyleSheet(
      "QLineEdit { background: #ffffff; border: 1px solid #d1d5db; border-radius: 8px; padding: 8px 10px; color: #111827; }"
      "QLineEdit:focus { border-color: #93c5fd; }");
  controlLayout->addWidget(hexEdit);

  auto *rgbLabel = new QLabel(QStringLiteral("RGB"), controlPanel);
  rgbLabel->setStyleSheet("color: #6b7280;");
  controlLayout->addWidget(rgbLabel);

  auto *rgbRow = new QHBoxLayout();
  rgbRow->setSpacing(8);
  auto *rEdit = new QLineEdit(controlPanel);
  auto *gEdit = new QLineEdit(controlPanel);
  auto *bEdit = new QLineEdit(controlPanel);
  rEdit->setPlaceholderText(QStringLiteral("R"));
  gEdit->setPlaceholderText(QStringLiteral("G"));
  bEdit->setPlaceholderText(QStringLiteral("B"));
  const QString channelEditStyle =
      QStringLiteral("QLineEdit { background: #ffffff; border: 1px solid #d1d5db; border-radius: 8px; padding: 8px 10px; color: #111827; }"
                     "QLineEdit:focus { border-color: #93c5fd; }");
  rEdit->setStyleSheet(channelEditStyle);
  gEdit->setStyleSheet(channelEditStyle);
  bEdit->setStyleSheet(channelEditStyle);
  rgbRow->addWidget(rEdit);
  rgbRow->addWidget(gEdit);
  rgbRow->addWidget(bEdit);
  controlLayout->addLayout(rgbRow);

  controlLayout->addStretch();

  pickerLayout->addWidget(controlPanel);
  paletteCardLayout->addLayout(pickerLayout);
  appearanceLayout->addWidget(paletteCard);
  appearanceLayout->addStretch();

  const auto currentColor = std::make_shared<QColor>(QColor(QStringLiteral("#3B82F6")));
  const auto isUpdating = std::make_shared<bool>(false);
  const auto syncInputs = std::make_shared<std::function<void(const QColor &)>>();
  *syncInputs = [=](const QColor &color) {
    if (!color.isValid()) {
      return;
    }

    *isUpdating = true;
    *currentColor = color;

    const QColor hsv = color.toHsv();
    const int hue = hsv.hue() < 0 ? 0 : hsv.hue();
    svPalette->setHue(hue);
    svPalette->setColor(color);
    hueSlider->setHue(hue);

    previewSwatch->setStyleSheet(
        QStringLiteral(
            "background: %1; border-radius: 12px; border: 1px solid rgba(255,255,255,0.10);")
            .arg(color.name(QColor::HexRgb)));
    previewName->setText(QStringLiteral("当前颜色 %1").arg(color.name(QColor::HexRgb).toUpper()));

    {
      const QSignalBlocker blocker(hexEdit);
      hexEdit->setText(color.name(QColor::HexRgb).toUpper());
    }
    {
      const QSignalBlocker blocker(rEdit);
      rEdit->setText(QString::number(color.red()));
    }
    {
      const QSignalBlocker blocker(gEdit);
      gEdit->setText(QString::number(color.green()));
    }
    {
      const QSignalBlocker blocker(bEdit);
      bEdit->setText(QString::number(color.blue()));
    }

    applyThemeColor(color);
    emit themeColorChanged(color.name(QColor::HexRgb).toUpper());
    *isUpdating = false;
  };

  svPalette->onColorChanged = [=](const QColor &color) {
    if (*isUpdating) {
      return;
    }
    (*syncInputs)(color);
  };
  hueSlider->onHueChanged = [=](int hue) {
    if (*isUpdating) {
      return;
    }
    QColor next = svPalette->color();
    next.setHsv(hue, next.hsvSaturation(), next.value());
    (*syncInputs)(next);
  };

  const auto applyHexInput = [=]() {
    if (*isUpdating) {
      return;
    }
    QString value = hexEdit->text().trimmed();
    if (!value.startsWith('#')) {
      value.prepend('#');
    }
    const QColor parsed(value);
    if (parsed.isValid()) {
      (*syncInputs)(parsed);
    }
  };

  const auto applyRgbInput = [=]() {
    if (*isUpdating) {
      return;
    }
    bool rOk = false;
    bool gOk = false;
    bool bOk = false;
    const int red = rEdit->text().trimmed().toInt(&rOk);
    const int green = gEdit->text().trimmed().toInt(&gOk);
    const int blue = bEdit->text().trimmed().toInt(&bOk);
    if (!rOk || !gOk || !bOk) {
      return;
    }
    const QColor parsed(qBound(0, red, 255), qBound(0, green, 255),
                        qBound(0, blue, 255));
    (*syncInputs)(parsed);
  };

  connect(hexEdit, &QLineEdit::editingFinished, this, applyHexInput);
  connect(rEdit, &QLineEdit::editingFinished, this, applyRgbInput);
  connect(gEdit, &QLineEdit::editingFinished, this, applyRgbInput);
  connect(bEdit, &QLineEdit::editingFinished, this, applyRgbInput);
  (*syncInputs)(*currentColor);

  auto *downloadTab = new QWidget(m_tabWidget);
  auto *downloadLayout = new QVBoxLayout(downloadTab);
  downloadLayout->setContentsMargins(0, 8, 0, 0);
  downloadLayout->addStretch();

  m_tabWidget->addTab(userTab, QStringLiteral("用户"));
  m_tabWidget->addTab(appearanceTab, QStringLiteral("界面"));
  m_tabWidget->addTab(downloadTab, QStringLiteral("下载"));

  m_statusLabel = new QLabel("就绪", this);
  m_statusLabel->setStyleSheet("color: #666;");
  rootLayout->addWidget(m_statusLabel);

  auto *buttonLayout = new QHBoxLayout();
  m_logoutButton = new QPushButton("登出", this);
  buttonLayout->addWidget(m_logoutButton);
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
  connect(m_logoutButton, &QPushButton::clicked, this,
          &SettingsWindow::onLogoutClicked);
  applyThemeColor(m_themeColor);
  connect(m_tabWidget, &QTabWidget::currentChanged, this,
          [this](int) {
            m_tabWidget->updateGeometry();
            if (layout()) {
              layout()->activate();
            }
            adjustWindowSizeForCurrentTab(true);
          });
  updateActionButtons();
}

int SettingsWindow::targetWindowHeightForTab(int tabIndex) const {
  if (!layout() || !m_tabWidget || tabIndex < 0 || tabIndex >= m_tabWidget->count()) {
    return height();
  }

  const QMargins rootMargins = layout()->contentsMargins();
  const int spacing = static_cast<QVBoxLayout *>(layout())->spacing();
  QWidget *page = m_tabWidget->widget(tabIndex);
  const QMargins tabMargins = m_tabWidget->contentsMargins();
  const int tabBarHeight = m_tabWidget->tabBar() ? m_tabWidget->tabBar()->sizeHint().height() : 0;
  const int pageHeight = page ? page->sizeHint().height() : 0;
  const int statusHeight = m_statusLabel ? m_statusLabel->sizeHint().height() : 0;
  const int buttonHeight = m_logoutButton ? m_logoutButton->sizeHint().height() : 0;
  const int frameExtra = m_tabWidget->height() - m_tabWidget->contentsRect().height();

  const int totalHeight = rootMargins.top() + rootMargins.bottom() + tabMargins.top() +
                          tabMargins.bottom() + frameExtra + tabBarHeight + pageHeight +
                          statusHeight + buttonHeight + spacing * 2;
  return qMax(totalHeight, minimumSizeHint().height());
}

void SettingsWindow::adjustWindowSizeForCurrentTab(bool animated) {
  if (!m_tabWidget) {
    return;
  }

  const int targetHeight = targetWindowHeightForTab(m_tabWidget->currentIndex());
  const QSize startSize = size();
  const QSize endSize(startSize.width(), targetHeight);
  if (startSize == endSize) {
    return;
  }

  if (!m_resizeAnimation) {
    m_resizeAnimation = new QPropertyAnimation(this, "size", this);
    m_resizeAnimation->setDuration(300);
    m_resizeAnimation->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_resizeAnimation, &QPropertyAnimation::valueChanged, this,
            [this](const QVariant &value) {
              const QSize animatedSize = value.toSize();
              setMinimumHeight(animatedSize.height());
              setMaximumHeight(animatedSize.height());
              resize(animatedSize);
            });
    connect(m_resizeAnimation, &QPropertyAnimation::finished, this, [this]() {
      setMinimumHeight(m_defaultMinimumHeight);
      setMaximumHeight(m_defaultMaximumHeight);
      if (m_tabWidget) {
        const int targetHeight =
            targetWindowHeightForTab(m_tabWidget->currentIndex());
        resize(width(), targetHeight);
      }
    });
  }

  m_resizeAnimation->stop();
  if (!animated) {
    setMinimumHeight(m_defaultMinimumHeight);
    setMaximumHeight(m_defaultMaximumHeight);
    resize(endSize);
    return;
  }

  setMinimumHeight(startSize.height());
  setMaximumHeight(startSize.height());
  m_resizeAnimation->setStartValue(startSize);
  m_resizeAnimation->setEndValue(endSize);
  m_resizeAnimation->start();
}

void SettingsWindow::applyThemeColor(const QColor &color) {
  m_themeColor = color.isValid() ? color : QColor(QStringLiteral("#3B82F6"));
  if (m_tabWidget) {
    m_tabWidget->setStyleSheet(settingsTabsStyleSheetForColor(m_themeColor));
  }
  if (m_chooseAvatarButton) {
    m_chooseAvatarButton->setStyleSheet(secondaryButtonStyleSheet());
  }
  if (m_refreshButton) {
    m_refreshButton->setStyleSheet(secondaryButtonStyleSheet());
  }
  if (m_logoutButton) {
    m_logoutButton->setStyleSheet(secondaryButtonStyleSheet());
  }
  if (m_saveButton) {
    m_saveButton->setStyleSheet(primaryButtonStyleSheetForColor(m_themeColor));
  }
  if (m_uploadAvatarButton) {
    m_uploadAvatarButton->setStyleSheet(primaryButtonStyleSheetForColor(m_themeColor));
  }
}

bool SettingsWindow::hasValidUserId() const {
  static const QRegularExpression kUnsignedIntRe(QStringLiteral("^\\d+$"));
  return kUnsignedIntRe.match(m_userId.trimmed()).hasMatch();
}

void SettingsWindow::updateActionButtons() {
  const bool busy = m_loading || m_saving || m_uploading || m_loggingOut;
  m_refreshButton->setEnabled(!busy);
  m_saveButton->setEnabled(!busy);
  m_chooseAvatarButton->setEnabled(!busy);
  m_uploadAvatarButton->setEnabled(!busy && !m_selectedAvatarFilePath.isEmpty());
  m_logoutButton->setEnabled(!busy);
  m_nicknameEdit->setReadOnly(m_saving || m_uploading || m_loggingOut);
  m_signatureEdit->setReadOnly(m_saving || m_uploading || m_loggingOut);
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
                    info.avatarUrl, info.signature);
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

void SettingsWindow::onLogoutClicked() {
  if (m_loading || m_saving || m_uploading || m_loggingOut) {
    return;
  }
  const QMessageBox::StandardButton confirm = QMessageBox::question(
      this, "确认登出", "确定要退出当前账号吗？", QMessageBox::Yes | QMessageBox::No,
      QMessageBox::No);
  if (confirm != QMessageBox::Yes) {
    return;
  }

  if (m_avatarPreviewReply) {
    m_avatarPreviewReply->abort();
  }
  if (m_uploadReply) {
    m_uploadReply->abort();
  }

  m_loggingOut = true;
  updateActionButtons();
  m_statusLabel->setText("退出登录中...");
  m_pendingLogoutRequestId = m_authApiClient.logout();
  if (m_pendingLogoutRequestId.isEmpty()) {
    m_loggingOut = false;
    updateActionButtons();
    m_statusLabel->setText("退出登录失败");
  }
}

void SettingsWindow::onLogoutSucceeded(const QString &requestId,
                                       const LogoutResult &result) {
  if (requestId != m_pendingLogoutRequestId) {
    return;
  }

  m_loggingOut = false;
  m_pendingLogoutRequestId.clear();
  m_pendingGetRequestId.clear();
  m_pendingSetRequestId.clear();
  m_pendingUploadRequestId.clear();
  m_statusLabel->setText(result.message.trimmed().isEmpty() ? "已退出登录"
                                                            : result.message.trimmed());
  updateActionButtons();
  emit logoutRequested();
  close();
}

void SettingsWindow::onAuthRequestFailed(const QString &requestId,
                                         const QString &action,
                                         const QString &error) {
  if (action != QStringLiteral("LOGOUT") || requestId != m_pendingLogoutRequestId) {
    return;
  }

  m_loggingOut = false;
  m_pendingLogoutRequestId.clear();
  updateActionButtons();
  m_statusLabel->setText("退出登录失败: " + error);
  QMessageBox::warning(this, "退出登录失败", error);
}
