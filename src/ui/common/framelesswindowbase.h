#ifndef FRAMELESSWINDOWBASE_H
#define FRAMELESSWINDOWBASE_H

#include <QPointer>
#include <QSet>
#include <QWidget>

class QEvent;
class QBoxLayout;
class QLabel;
class QMouseEvent;
class QPushButton;
class QResizeEvent;

class FramelessWindowBase : public QWidget {
public:
  enum class ResizeRegion {
    None,
    Left,
    Right,
    Top,
    Bottom,
    TopLeft,
    TopRight,
    BottomLeft,
    BottomRight,
  };

  explicit FramelessWindowBase(QWidget *parent = nullptr);

  void setDragEnabled(bool enabled);
  bool dragEnabled() const;

  void setResizeEnabled(bool enabled);
  bool resizeEnabled() const;

  void setResizeBorderWidth(int width);
  int resizeBorderWidth() const;

  void setTitleBarWidget(QWidget *widget);
  QWidget *titleBarWidget() const;

  void addDragRegion(QWidget *widget);
  void removeDragRegion(QWidget *widget);

  void setStandardTitleBarVisible(bool visible);
  bool isStandardTitleBarVisible() const;

  void setMinimizeButtonVisible(bool visible);
  bool isMinimizeButtonVisible() const;

  void setCloseButtonVisible(bool visible);
  bool isCloseButtonVisible() const;

  int standardTitleBarHeight() const;

protected:
  QWidget *contentWidget() const;

  bool eventFilter(QObject *watched, QEvent *event) override;
  void mousePressEvent(QMouseEvent *event) override;
  void mouseMoveEvent(QMouseEvent *event) override;
  void mouseReleaseEvent(QMouseEvent *event) override;
  void leaveEvent(QEvent *event) override;
  void resizeEvent(QResizeEvent *event) override;
  void changeEvent(QEvent *event) override;

private:
  void ensureStandardTitleBar();
  void updateStandardTitleBarGeometry();
  bool shouldStartWindowDrag(QObject *source, const QPointF &localPos) const;
  ResizeRegion hitTestResizeRegion(const QPoint &localPos) const;
  void updateCursorForPosition(const QPoint &globalPos);
  void beginDrag(const QPoint &globalPos);
  void beginResize(const QPoint &globalPos, ResizeRegion region);
  void updateDrag(const QPoint &globalPos);
  void updateResize(const QPoint &globalPos);
  void endWindowInteraction();

private:
  bool m_dragEnabled = true;
  bool m_resizeEnabled = true;
  bool m_dragging = false;
  bool m_resizing = false;
  int m_resizeBorderWidth = 6;
  QPoint m_dragOffset;
  QPoint m_resizeStartGlobalPos;
  QRect m_resizeStartGeometry;
  ResizeRegion m_activeResizeRegion = ResizeRegion::None;
  QPointer<QWidget> m_titleBarWidget;
  QPointer<QWidget> m_standardTitleBar;
  QPointer<QWidget> m_contentWidget;
  QPointer<QBoxLayout> m_rootLayout;
  QPointer<QLabel> m_titleLabel;
  QPointer<QPushButton> m_minimizeButton;
  QPointer<QPushButton> m_closeButton;
  QSet<QWidget *> m_dragRegions;
};

#endif // FRAMELESSWINDOWBASE_H


