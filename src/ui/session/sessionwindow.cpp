#include "sessionwindow.h"
#include <QAbstractSocket>
#include <QDebug>
#include <QHBoxLayout>
#include <QTextCursor>

SessionWindow::SessionWindow(const QString &sessionName, QWidget *parent)
    : QWidget(parent), m_sessionName(sessionName), m_isDragging(false),
      m_resizeDir(None), m_receiveBox(nullptr), m_inputLine(nullptr),
      m_sendBtn(nullptr) {
  setAttribute(Qt::WA_DeleteOnClose);
  setMouseTracking(true); // Enable mouse tracking for resize cursor feedback
  initUI();
}

void SessionWindow::initUI() {
  setWindowTitle(m_sessionName);
  resize(600, 400);

  // 1. 去除系统标题栏
  setWindowFlags(Qt::FramelessWindowHint | Qt::WindowMinMaxButtonsHint);
  setAttribute(Qt::WA_TranslucentBackground);

  // 2. 主容器与样式
  QVBoxLayout *mainLayout = new QVBoxLayout(this);
  // 设置 5px
  // 的边距，这部分区域实际上是透明的，专门用于捕获鼠标移动事件来实现调整大小
  // 只有鼠标位于这 5px 的边缘区域时，事件才会直接发送给
  // SessionWindow，而不是被子控件遮挡
  mainLayout->setContentsMargins(2, 2, 2, 2);
  mainLayout->setSpacing(0);

  QWidget *container = new QWidget(this);
  container->setObjectName("SessionContainer");
  container->setStyleSheet("#SessionContainer { background-color: #f5f5f5; "
                           "border: 1px solid #dcdcdc; border-radius: 4px; }");
  mainLayout->addWidget(container);

  QVBoxLayout *containerLayout = new QVBoxLayout(container);
  containerLayout->setContentsMargins(0, 0, 0, 0);
  containerLayout->setSpacing(0);

  // 3. 自定义顶部标题栏
  QWidget *header = new QWidget(container);
  header->setFixedHeight(50);
  header->setStyleSheet(
      "background-color: #ffffff; border-bottom: 1px solid #e0e0e0; "
      "border-top-left-radius: 4px; border-top-right-radius: 4px;");

  QHBoxLayout *headerLayout = new QHBoxLayout(header);
  headerLayout->setContentsMargins(15, 0, 10, 0);

  // 标题文本 (居中)
  QLabel *titleLabel = new QLabel(m_sessionName, header);
  titleLabel->setStyleSheet("font-size: 16px; font-weight: bold; color: #333;");
  titleLabel->setAlignment(Qt::AlignCenter);

  // 关闭按钮
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
  headerLayout->addWidget(titleLabel);
  headerLayout->addStretch();
  headerLayout->addWidget(closeBtn);

  containerLayout->addWidget(header);

  // Enable mouse tracking
  container->setMouseTracking(true);
  header->setMouseTracking(true);

  // Install event filter
  container->installEventFilter(this);
  header->installEventFilter(this);

  // 内容区域
  QWidget *contentArea = new QWidget(container);
  contentArea->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  contentArea->setMouseTracking(true);   // Enable for content too
  contentArea->installEventFilter(this); // Filter for content too

  QVBoxLayout *contentLayout = new QVBoxLayout(contentArea);
  contentLayout->setContentsMargins(12, 12, 12, 12);
  contentLayout->setSpacing(12);

  m_receiveBox = new QTextEdit(contentArea);
  m_receiveBox->setPlaceholderText("接收服务器响应");
  m_receiveBox->setReadOnly(true);
  m_receiveBox->setStyleSheet("background-color: #ffffff; border: 1px solid "
                              "#dcdcdc; border-radius: 4px;");
  contentLayout->addWidget(m_receiveBox);

  QHBoxLayout *inputLayout = new QHBoxLayout();
  inputLayout->setSpacing(8);

  m_inputLine = new QLineEdit(contentArea);
  m_inputLine->setPlaceholderText("输入测试消息");
  m_inputLine->setStyleSheet(
      "border: 1px solid #dcdcdc; border-radius: 4px; padding: 4px;");
  inputLayout->addWidget(m_inputLine);

  m_sendBtn = new QPushButton("发送", contentArea);
  m_sendBtn->setCursor(Qt::PointingHandCursor);
  m_sendBtn->setStyleSheet(
      "QPushButton { background-color: #4a90e2; color: white; border: none; "
      "border-radius: 4px; padding: 6px 16px; }"
      "QPushButton:hover { background-color: #3a78d6; }");
  inputLayout->addWidget(m_sendBtn);

  connect(m_inputLine, &QLineEdit::returnPressed, m_sendBtn,
          &QPushButton::click);

  contentLayout->addLayout(inputLayout);

  containerLayout->addWidget(contentArea);
}

bool SessionWindow::eventFilter(QObject *obj, QEvent *event) {
  if (event->type() == QEvent::MouseMove ||
      event->type() == QEvent::MouseButtonPress ||
      event->type() == QEvent::MouseButtonRelease) {

    QMouseEvent *mouseEvent = static_cast<QMouseEvent *>(event);
    if (event->type() == QEvent::MouseMove)
      handleMouseMove(mouseEvent);
    else if (event->type() == QEvent::MouseButtonPress)
      handleMousePress(mouseEvent);
    else if (event->type() == QEvent::MouseButtonRelease)
      handleMouseRelease(mouseEvent);

    return false;
  }
  return QWidget::eventFilter(obj, event);
}

// --- 拖拽与缩放支持 ---
void SessionWindow::checkCursorShape(const QPoint &globalPos) {
  const int margin = 8;
  QPoint pos = mapFromGlobal(globalPos);

  int w = width();
  int h = height();
  int x = pos.x();
  int y = pos.y();

  bool left = x < margin;
  bool right = x > w - margin;
  bool top = y < margin;
  bool bottom = y > h - margin;

  if (top && left)
    m_resizeDir = TopLeft;
  else if (top && right)
    m_resizeDir = TopRight;
  else if (bottom && left)
    m_resizeDir = BottomLeft;
  else if (bottom && right)
    m_resizeDir = BottomRight;
  else if (top)
    m_resizeDir = Top;
  else if (bottom)
    m_resizeDir = Bottom;
  else if (left)
    m_resizeDir = Left;
  else if (right)
    m_resizeDir = Right;
  else
    m_resizeDir = None;

  if (m_resizeDir == TopLeft || m_resizeDir == BottomRight)
    setCursor(Qt::SizeFDiagCursor);
  else if (m_resizeDir == TopRight || m_resizeDir == BottomLeft)
    setCursor(Qt::SizeBDiagCursor);
  else if (m_resizeDir == Left || m_resizeDir == Right)
    setCursor(Qt::SizeHorCursor);
  else if (m_resizeDir == Top || m_resizeDir == Bottom)
    setCursor(Qt::SizeVerCursor);
  else
    setCursor(Qt::ArrowCursor);
}

void SessionWindow::handleMousePress(QMouseEvent *event) {
  if (event->button() == Qt::LeftButton) {
    if (m_resizeDir != None) {
      // Resize mode started
    } else {
      // Move mode
      m_isDragging = true;
      m_dragPosition =
          event->globalPosition().toPoint() - frameGeometry().topLeft();
    }
  }
}

void SessionWindow::handleMouseMove(QMouseEvent *event) {
  if (event->buttons() & Qt::LeftButton) {
    if (m_resizeDir != None) {
      // Handle resizing
      QPoint globalPos = event->globalPosition().toPoint();
      QRect rect = geometry();
      int minW = minimumWidth();
      int minH = minimumHeight();

      if (m_resizeDir == Left || m_resizeDir == TopLeft ||
          m_resizeDir == BottomLeft) {
        int newW = rect.right() - globalPos.x();
        if (newW > minW)
          rect.setLeft(globalPos.x());
      }
      if (m_resizeDir == Right || m_resizeDir == TopRight ||
          m_resizeDir == BottomRight) {
        rect.setRight(globalPos.x());
      }
      if (m_resizeDir == Top || m_resizeDir == TopLeft ||
          m_resizeDir == TopRight) {
        int newH = rect.bottom() - globalPos.y();
        if (newH > minH)
          rect.setTop(globalPos.y());
      }
      if (m_resizeDir == Bottom || m_resizeDir == BottomLeft ||
          m_resizeDir == BottomRight) {
        rect.setBottom(globalPos.y());
      }
      setGeometry(rect);
    } else if (m_isDragging) {
      move(event->globalPosition().toPoint() - m_dragPosition);
    }
  } else {
    checkCursorShape(event->globalPosition().toPoint());
  }
}

void SessionWindow::handleMouseRelease(QMouseEvent *event) {
  if (event->button() == Qt::LeftButton) {
    m_isDragging = false;
  }
}

void SessionWindow::mousePressEvent(QMouseEvent *event) {
  handleMousePress(event);
  event->accept();
}

void SessionWindow::mouseMoveEvent(QMouseEvent *event) {
  handleMouseMove(event);
  event->accept();
}

void SessionWindow::mouseReleaseEvent(QMouseEvent *event) {
  handleMouseRelease(event);
  event->accept();
}
