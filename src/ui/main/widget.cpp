#include "widget.h"
#include "sessionwindow.h"
#include "ui_widget.h"

Widget::Widget(QWidget *parent)
    : QWidget(parent), ui(new Ui::Widget), m_isDragging(false) {
  initUI();
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
  m_nameLabel->setStyleSheet("font-size: 18px; font-weight: bold; color: #333; "
                             "margin-left: 10px; border: none;");

  leftContentLayout->addWidget(m_avatarLabel);
  leftContentLayout->addWidget(m_nameLabel);

  mainTopLayout->addLayout(leftContentLayout);

  mainTopLayout->addStretch();

  QVBoxLayout *rightBtnLayout = new QVBoxLayout();
  rightBtnLayout->setContentsMargins(0, 0, 0, 0);
  rightBtnLayout->setSpacing(0);

  QHBoxLayout *btnRowLayout = new QHBoxLayout();
  btnRowLayout->setContentsMargins(0, 0, 0, 0);
  btnRowLayout->setSpacing(0);

  // 关闭和最小化按钮
  QPushButton *closeBtn = new QPushButton("×", m_topPanel);
  QPushButton *minBtn = new QPushButton("−", m_topPanel);

  btnRowLayout->addWidget(minBtn);
  btnRowLayout->addWidget(closeBtn);

  rightBtnLayout->addLayout(btnRowLayout);
  rightBtnLayout->addStretch();

  mainTopLayout->addLayout(rightBtnLayout);

  // 样式：悬浮时背景变灰/红
  QString baseBtnStyle =
      "QPushButton { border: none; font-weight: bold; color: #555; font-size: "
      "16px; background: transparent; }";

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

  connect(minBtn, &QPushButton::clicked, this, &QWidget::showMinimized);
  connect(closeBtn, &QPushButton::clicked, this, &QWidget::close);

  btnRowLayout->addWidget(minBtn);
  btnRowLayout->addWidget(closeBtn);

  rightBtnLayout->addLayout(btnRowLayout);
  rightBtnLayout->addStretch(); // 顶上去

  mainTopLayout->addLayout(rightBtnLayout);

  // --- 中下部：会话列表 ---
  m_sessionList = new QListWidget(container);
  m_sessionList->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
  m_sessionList->setFrameShape(QFrame::NoFrame); // 无边框
  m_sessionList->setVerticalScrollBarPolicy(
      Qt::ScrollBarAlwaysOn); // 滚动条常驻
  m_sessionList->setStyleSheet(
      // 修改背景色为浅灰 (#f5f5f5)

      // 方案：让 container 稍微灰一点，或者让 ListWidget

      // 1. Container 背景设为白色 (保持)
      // 2. ListWidget 背景设为浅灰 (#f7f7f7)
      // 3. 这样 ListWidget 的圆角边界就能看出来了

      "QListWidget { background-color: #ffffff; color: #000000; border: "
      "none; "
      "margin: 10px; border-radius: 1px; outline: none; }"
      "QListWidget::item { height: 70px; border-bottom: 1px solid #e0e0e0; " // 分割线也稍微深一点
      "padding: 10px; color: #000000; outline: none; }"
      "QListWidget::item:selected { background-color: #d0d0d0; color: "
      "#000000; "
      "}"
      "QListWidget::item:hover { background-color: #f0f0f0; color: "
      "#000000; " // 悬浮变成纯白高亮
      "}"
      // 滚动条样式定制
      "QScrollBar:vertical { border: none; background: #f7f7f7; width: "
      "12px; " // 滚动条背景也跟随变浅灰
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

  // 添加一些测试数据（单聊与群聊共享 Session 类）
  addSessionItem(Session::create("Alice", Session::Type::Direct));
  addSessionItem(Session::create("Bob", Session::Type::Direct));
  addSessionItem(Session::create("项目群", Session::Type::Group));
  addSessionItem(Session::create("研发群", Session::Type::Group));

  // 添加到容器布局
  containerLayout->addWidget(m_topPanel);
  containerLayout->addWidget(m_sessionList);

  connect(m_sessionList, &QListWidget::itemDoubleClicked, this,
          &Widget::onSessionDoubleClicked);
}

void Widget::setUserInfo(const QString &username, const QString &avatarPath) {
  m_nameLabel->setText(username);
  // 简单取首字母作为头像占位符
  if (!username.isEmpty()) {
    m_avatarLabel->setText(username.left(1).toUpper());
  }
  // TODO: 如果有真实图片路径，这里可以用 QPixmap 加载
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
