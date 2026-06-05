#include "framelesswindowbase.h"

#include <QAbstractButton>
#include <QBoxLayout>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QResizeEvent>
#include <QSizePolicy>
#include <QVBoxLayout>

namespace {
/**
 * @brief 执行cursorShapeForRegion的核心逻辑。
 * @param region 输入参数 `region`。
 * @return 返回 Qt::CursorShape 结果。
 */
Qt::CursorShape cursorShapeForRegion(FramelessWindowBase::ResizeRegion region) {
  switch (region) {
  case FramelessWindowBase::ResizeRegion::Left:
  case FramelessWindowBase::ResizeRegion::Right:
    return Qt::SizeHorCursor;
  case FramelessWindowBase::ResizeRegion::Top:
  case FramelessWindowBase::ResizeRegion::Bottom:
    return Qt::SizeVerCursor;
  case FramelessWindowBase::ResizeRegion::TopLeft:
  case FramelessWindowBase::ResizeRegion::BottomRight:
    return Qt::SizeFDiagCursor;
  case FramelessWindowBase::ResizeRegion::TopRight:
  case FramelessWindowBase::ResizeRegion::BottomLeft:
    return Qt::SizeBDiagCursor;
  case FramelessWindowBase::ResizeRegion::None:
  default:
    return Qt::ArrowCursor;
  }
}
} // namespace

/**
 * @brief 构造并初始化FramelessWindowBase实例。
 * @param parent 父级对象指针，用于管理当前对象的生命周期。
 * @return 无返回值。
 */
FramelessWindowBase::FramelessWindowBase(QWidget *parent) : QWidget(parent) {
  setWindowFlags(windowFlags() | Qt::FramelessWindowHint);
  setMouseTracking(true);

  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);
  m_rootLayout = layout;

  m_contentWidget = new QWidget(this);
  m_contentWidget->setObjectName(QStringLiteral("FramelessWindowContent"));
  m_contentWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

  ensureStandardTitleBar();
  layout->addWidget(m_standardTitleBar);
  layout->addWidget(m_contentWidget, 1);
}

/**
 * @brief 设置dragenabled值。
 * @param enabled 布尔参数 `enabled`。
 * @return 无返回值。
 */
void FramelessWindowBase::setDragEnabled(bool enabled) { m_dragEnabled = enabled; }

/**
 * @brief 实现 dragEnabled 的核心逻辑。
 * @return 返回本次处理是否成功。
 */
bool FramelessWindowBase::dragEnabled() const { return m_dragEnabled; }

/**
 * @brief 设置resizeenabled值。
 * @param enabled 布尔参数 `enabled`。
 * @return 无返回值。
 */
void FramelessWindowBase::setResizeEnabled(bool enabled) {
  m_resizeEnabled = enabled;
  if (!enabled) {
    unsetCursor();
    m_activeResizeRegion = ResizeRegion::None;
  }
}

/**
 * @brief 实现 resizeEnabled 的核心逻辑。
 * @return 返回本次处理是否成功。
 */
bool FramelessWindowBase::resizeEnabled() const { return m_resizeEnabled; }

/**
 * @brief 设置resizeborderwidth值。
 * @param width 数值参数 `width`。
 * @return 无返回值。
 */
void FramelessWindowBase::setResizeBorderWidth(int width) {
  m_resizeBorderWidth = qMax(2, width);
}

/**
 * @brief 实现 resizeBorderWidth 的核心逻辑。
 * @return 返回计算得到的数值结果。
 */
int FramelessWindowBase::resizeBorderWidth() const { return m_resizeBorderWidth; }

/**
 * @brief 设置titlebarwidget值。
 * @param widget 界面对象。
 * @return 无返回值。
 */
void FramelessWindowBase::setTitleBarWidget(QWidget *widget) {
  if (m_titleBarWidget == widget) {
    return;
  }
  if (m_titleBarWidget) {
    m_titleBarWidget->removeEventFilter(this);
    m_titleBarWidget->setMouseTracking(false);
    m_dragRegions.remove(m_titleBarWidget);
  }
  m_titleBarWidget = widget;
  if (m_titleBarWidget) {
    m_titleBarWidget->installEventFilter(this);
    m_titleBarWidget->setMouseTracking(true);
    m_dragRegions.insert(m_titleBarWidget);
  }
}

/**
 * @brief 实现 titleBarWidget 的核心逻辑。
 * @return 返回处理得到的对象指针。
 */
QWidget *FramelessWindowBase::titleBarWidget() const { return m_titleBarWidget; }

/**
 * @brief 执行addDragRegion的核心逻辑。
 * @param widget 界面对象。
 * @return 无返回值。
 */
void FramelessWindowBase::addDragRegion(QWidget *widget) {
  if (!widget || m_dragRegions.contains(widget)) {
    return;
  }
  m_dragRegions.insert(widget);
  widget->installEventFilter(this);
  widget->setMouseTracking(true);
}

/**
 * @brief 移除dragregion数据或状态。
 * @param widget 界面对象。
 * @return 无返回值。
 */
void FramelessWindowBase::removeDragRegion(QWidget *widget) {
  if (!widget || !m_dragRegions.contains(widget)) {
    return;
  }
  m_dragRegions.remove(widget);
  widget->removeEventFilter(this);
}

/**
 * @brief 设置standardtitlebarvisible值。
 * @param visible 布尔参数 `visible`。
 * @return 无返回值。
 */
void FramelessWindowBase::setStandardTitleBarVisible(bool visible) {
  ensureStandardTitleBar();
  if (m_standardTitleBar) {
    m_standardTitleBar->setVisible(visible);
    if (m_rootLayout) {
      m_rootLayout->invalidate();
    }
  }
}

/**
 * @brief 判断standardtitlebarvisible条件是否满足。
 * @return 返回条件判断结果，`true` 表示满足，`false` 表示不满足。
 */
bool FramelessWindowBase::isStandardTitleBarVisible() const {
  return m_standardTitleBar && m_standardTitleBar->isVisible();
}

/**
 * @brief 设置minimizebuttonvisible值。
 * @param visible 布尔参数 `visible`。
 * @return 无返回值。
 */
void FramelessWindowBase::setMinimizeButtonVisible(bool visible) {
  ensureStandardTitleBar();
  if (m_minimizeButton) {
    m_minimizeButton->setVisible(visible);
  }
}

/**
 * @brief 判断minimizebuttonvisible条件是否满足。
 * @return 返回条件判断结果，`true` 表示满足，`false` 表示不满足。
 */
bool FramelessWindowBase::isMinimizeButtonVisible() const {
  return m_minimizeButton && m_minimizeButton->isVisible();
}

/**
 * @brief 设置关闭buttonvisible值。
 * @param visible 布尔参数 `visible`。
 * @return 无返回值。
 */
void FramelessWindowBase::setCloseButtonVisible(bool visible) {
  ensureStandardTitleBar();
  if (m_closeButton) {
    m_closeButton->setVisible(visible);
  }
}

/**
 * @brief 判断关闭buttonvisible条件是否满足。
 * @return 返回条件判断结果，`true` 表示满足，`false` 表示不满足。
 */
bool FramelessWindowBase::isCloseButtonVisible() const {
  return m_closeButton && m_closeButton->isVisible();
}

/**
 * @brief 执行standardTitleBarHeight的核心逻辑。
 * @return 返回计算得到的数值结果。
 */
int FramelessWindowBase::standardTitleBarHeight() const {
  return isStandardTitleBarVisible() ? 40 : 0;
}

/**
 * @brief 实现 contentWidget 的核心逻辑。
 * @return 返回处理得到的对象指针。
 */
QWidget *FramelessWindowBase::contentWidget() const { return m_contentWidget; }

/**
 * @brief 执行eventFilter的核心逻辑。
 * @param watched 对象参数 `watched`。
 * @param event 对象参数 `event`。
 * @return 返回布尔结果。
 */
bool FramelessWindowBase::eventFilter(QObject *watched, QEvent *event) {
  QWidget *widget = qobject_cast<QWidget *>(watched);
  if (!widget || !m_dragRegions.contains(widget)) {
    return QWidget::eventFilter(watched, event);
  }

  switch (event->type()) {
  case QEvent::MouseButtonPress: {
    auto *mouseEvent = static_cast<QMouseEvent *>(event);
    if (mouseEvent->button() == Qt::LeftButton &&
        shouldStartWindowDrag(watched, mouseEvent->position())) {
      beginDrag(mouseEvent->globalPosition().toPoint());
      return true;
    }
    break;
  }
  case QEvent::MouseMove: {
    auto *mouseEvent = static_cast<QMouseEvent *>(event);
    if (m_dragging && (mouseEvent->buttons() & Qt::LeftButton)) {
      updateDrag(mouseEvent->globalPosition().toPoint());
      return true;
    }
    break;
  }
  case QEvent::MouseButtonRelease: {
    auto *mouseEvent = static_cast<QMouseEvent *>(event);
    if (m_dragging && mouseEvent->button() == Qt::LeftButton) {
      endWindowInteraction();
      return true;
    }
    break;
  }
  default:
    break;
  }

  return QWidget::eventFilter(watched, event);
}

/**
 * @brief 执行mousePressEvent的核心逻辑。
 * @param event 对象参数 `event`。
 * @return 无返回值。
 */
void FramelessWindowBase::mousePressEvent(QMouseEvent *event) {
  if (event->button() == Qt::LeftButton) {
    const ResizeRegion region = hitTestResizeRegion(event->position().toPoint());
    if (m_resizeEnabled && region != ResizeRegion::None) {
      beginResize(event->globalPosition().toPoint(), region);
      event->accept();
      return;
    }
    if (m_dragEnabled) {
      beginDrag(event->globalPosition().toPoint());
      event->accept();
      return;
    }
  }
  QWidget::mousePressEvent(event);
}

/**
 * @brief 执行mouseMoveEvent的核心逻辑。
 * @param event 对象参数 `event`。
 * @return 无返回值。
 */
void FramelessWindowBase::mouseMoveEvent(QMouseEvent *event) {
  const QPoint globalPos = event->globalPosition().toPoint();
  if (m_resizing && (event->buttons() & Qt::LeftButton)) {
    updateResize(globalPos);
    event->accept();
    return;
  }
  if (m_dragging && (event->buttons() & Qt::LeftButton)) {
    updateDrag(globalPos);
    event->accept();
    return;
  }
  updateCursorForPosition(globalPos);
  QWidget::mouseMoveEvent(event);
}

/**
 * @brief 执行mouseReleaseEvent的核心逻辑。
 * @param event 对象参数 `event`。
 * @return 无返回值。
 */
void FramelessWindowBase::mouseReleaseEvent(QMouseEvent *event) {
  if ((m_dragging || m_resizing) && event->button() == Qt::LeftButton) {
    endWindowInteraction();
    event->accept();
    return;
  }
  QWidget::mouseReleaseEvent(event);
}

/**
 * @brief 执行leaveEvent的核心逻辑。
 * @param event 对象参数 `event`。
 * @return 无返回值。
 */
void FramelessWindowBase::leaveEvent(QEvent *event) {
  if (!m_dragging && !m_resizing) {
    unsetCursor();
  }
  QWidget::leaveEvent(event);
}

/**
 * @brief 实现 resizeEvent 的核心逻辑。
 * @param event 对象参数 `event`。
 * @return 无返回值。
 */
void FramelessWindowBase::resizeEvent(QResizeEvent *event) { QWidget::resizeEvent(event); }

/**
 * @brief 执行changeEvent的核心逻辑。
 * @param event 对象参数 `event`。
 * @return 无返回值。
 */
void FramelessWindowBase::changeEvent(QEvent *event) {
  if (event->type() == QEvent::WindowTitleChange && m_titleLabel) {
    m_titleLabel->setText(windowTitle());
  }
  QWidget::changeEvent(event);
}

/**
 * @brief 确保StandardTitleBar满足预期条件。
 * @return 无返回值。
 */
void FramelessWindowBase::ensureStandardTitleBar() {
  if (m_standardTitleBar) {
    return;
  }

  m_standardTitleBar = new QWidget(this);
  m_standardTitleBar->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  m_standardTitleBar->setFixedHeight(standardTitleBarHeight());
  m_standardTitleBar->setObjectName(QStringLiteral("FramelessStandardTitleBar"));
  m_standardTitleBar->setStyleSheet(
      QStringLiteral("#FramelessStandardTitleBar { background: transparent; border: none; }"));

  auto *layout = new QHBoxLayout(m_standardTitleBar);
  layout->setContentsMargins(10, 8, 6, 4);
  layout->setSpacing(6);

  m_titleLabel = new QLabel(windowTitle(), m_standardTitleBar);
  m_titleLabel->setStyleSheet(QStringLiteral(
      "font-size: 13px; font-weight: 600; color: #111827; background: transparent; border: none;"));
  layout->addWidget(m_titleLabel);
  layout->addStretch();

  m_minimizeButton = new QPushButton(QStringLiteral("-"), m_standardTitleBar);
  m_minimizeButton->setCursor(Qt::ArrowCursor);
  m_minimizeButton->setFixedSize(28, 28);
  m_minimizeButton->setStyleSheet(QStringLiteral(
      "QPushButton { border: none; color: #4b5563; font-size: 18px; background: transparent; border-radius: 6px; }"
      "QPushButton:hover { background: #e5e7eb; color: #111827; }"));
  connect(m_minimizeButton, &QPushButton::clicked, this, &QWidget::showMinimized);
  layout->addWidget(m_minimizeButton);

  m_closeButton = new QPushButton(QStringLiteral("×"), m_standardTitleBar);
  m_closeButton->setCursor(Qt::ArrowCursor);
  m_closeButton->setFixedSize(28, 28);
  m_closeButton->setStyleSheet(QStringLiteral(
      "QPushButton { border: none; color: #4b5563; font-size: 18px; background: transparent; border-radius: 6px; }"
      "QPushButton:hover { background: #ef4444; color: #ffffff; }"));
  connect(m_closeButton, &QPushButton::clicked, this, &QWidget::close);
  layout->addWidget(m_closeButton);

  setTitleBarWidget(m_standardTitleBar);
  m_standardTitleBar->show();
}

/**
 * @brief 更新standardtitlebargeometry状态。
 * @return 无返回值。
 */
void FramelessWindowBase::updateStandardTitleBarGeometry() {}

/**
 * @brief 判断Start窗口Drag条件是否满足。
 * @param source 来源标识或来源数据。
 * @param localPos 数值参数 `localPos`。
 * @return 返回条件判断结果，`true` 表示满足，`false` 表示不满足。
 */
bool FramelessWindowBase::shouldStartWindowDrag(QObject *source,
                                                const QPointF &localPos) const {
  if (!m_dragEnabled) {
    return false;
  }
  QWidget *widget = qobject_cast<QWidget *>(source);
  if (!widget) {
    return false;
  }
  QWidget *child = widget->childAt(localPos.toPoint());
  while (child) {
    if (qobject_cast<QAbstractButton *>(child)) {
      return false;
    }
    child = child->parentWidget();
    if (child == widget) {
      break;
    }
  }
  return true;
}

FramelessWindowBase::ResizeRegion
/**
 * @brief 构造并初始化hitTestResizeRegion实例。
 * @param localPos 数值参数 `localPos`。
 * @return 无返回值。
 */
FramelessWindowBase::hitTestResizeRegion(const QPoint &localPos) const {
  if (!m_resizeEnabled || isMaximized()) {
    return ResizeRegion::None;
  }

  const QRect bounds = rect();
  const bool left = localPos.x() <= m_resizeBorderWidth;
  const bool right = localPos.x() >= bounds.width() - m_resizeBorderWidth;
  const bool top = localPos.y() <= m_resizeBorderWidth;
  const bool bottom = localPos.y() >= bounds.height() - m_resizeBorderWidth;

  if (top && left) {
    return ResizeRegion::TopLeft;
  }
  if (top && right) {
    return ResizeRegion::TopRight;
  }
  if (bottom && left) {
    return ResizeRegion::BottomLeft;
  }
  if (bottom && right) {
    return ResizeRegion::BottomRight;
  }
  if (left) {
    return ResizeRegion::Left;
  }
  if (right) {
    return ResizeRegion::Right;
  }
  if (top) {
    return ResizeRegion::Top;
  }
  if (bottom) {
    return ResizeRegion::Bottom;
  }
  return ResizeRegion::None;
}

/**
 * @brief 更新cursorforposition状态。
 * @param globalPos 数值参数 `globalPos`。
 * @return 无返回值。
 */
void FramelessWindowBase::updateCursorForPosition(const QPoint &globalPos) {
  if (m_dragging || m_resizing) {
    return;
  }
  const ResizeRegion region = hitTestResizeRegion(mapFromGlobal(globalPos));
  setCursor(cursorShapeForRegion(region));
}

/**
 * @brief 执行beginDrag的核心逻辑。
 * @param globalPos 数值参数 `globalPos`。
 * @return 无返回值。
 */
void FramelessWindowBase::beginDrag(const QPoint &globalPos) {
  if (!m_dragEnabled) {
    return;
  }
  m_dragging = true;
  m_resizing = false;
  m_dragOffset = globalPos - frameGeometry().topLeft();
}

/**
 * @brief 执行beginResize的核心逻辑。
 * @param globalPos 数值参数 `globalPos`。
 * @param region 输入参数 `region`。
 * @return 无返回值。
 */
void FramelessWindowBase::beginResize(const QPoint &globalPos,
                                      ResizeRegion region) {
  m_resizing = true;
  m_dragging = false;
  m_activeResizeRegion = region;
  m_resizeStartGlobalPos = globalPos;
  m_resizeStartGeometry = geometry();
}

/**
 * @brief 更新drag状态。
 * @param globalPos 数值参数 `globalPos`。
 * @return 无返回值。
 */
void FramelessWindowBase::updateDrag(const QPoint &globalPos) {
  move(globalPos - m_dragOffset);
}

/**
 * @brief 更新resize状态。
 * @param globalPos 数值参数 `globalPos`。
 * @return 无返回值。
 */
void FramelessWindowBase::updateResize(const QPoint &globalPos) {
  QRect nextGeometry = m_resizeStartGeometry;
  const QPoint delta = globalPos - m_resizeStartGlobalPos;
  const QSize minSize = minimumSize();

  switch (m_activeResizeRegion) {
  case ResizeRegion::Left:
  case ResizeRegion::TopLeft:
  case ResizeRegion::BottomLeft:
    nextGeometry.setLeft(nextGeometry.left() + delta.x());
    if (nextGeometry.width() < minSize.width()) {
      nextGeometry.setLeft(nextGeometry.right() - minSize.width() + 1);
    }
    break;
  default:
    break;
  }

  switch (m_activeResizeRegion) {
  case ResizeRegion::Right:
  case ResizeRegion::TopRight:
  case ResizeRegion::BottomRight:
    nextGeometry.setRight(nextGeometry.right() + delta.x());
    if (nextGeometry.width() < minSize.width()) {
      nextGeometry.setRight(nextGeometry.left() + minSize.width() - 1);
    }
    break;
  default:
    break;
  }

  switch (m_activeResizeRegion) {
  case ResizeRegion::Top:
  case ResizeRegion::TopLeft:
  case ResizeRegion::TopRight:
    nextGeometry.setTop(nextGeometry.top() + delta.y());
    if (nextGeometry.height() < minSize.height()) {
      nextGeometry.setTop(nextGeometry.bottom() - minSize.height() + 1);
    }
    break;
  default:
    break;
  }

  switch (m_activeResizeRegion) {
  case ResizeRegion::Bottom:
  case ResizeRegion::BottomLeft:
  case ResizeRegion::BottomRight:
    nextGeometry.setBottom(nextGeometry.bottom() + delta.y());
    if (nextGeometry.height() < minSize.height()) {
      nextGeometry.setBottom(nextGeometry.top() + minSize.height() - 1);
    }
    break;
  default:
    break;
  }

  setGeometry(nextGeometry.normalized());
}

/**
 * @brief 执行endWindowInteraction的核心逻辑。
 * @return 无返回值。
 */
void FramelessWindowBase::endWindowInteraction() {
  m_dragging = false;
  m_resizing = false;
  m_activeResizeRegion = ResizeRegion::None;
  unsetCursor();
}




