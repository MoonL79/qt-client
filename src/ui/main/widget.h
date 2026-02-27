#ifndef WIDGET_H
#define WIDGET_H

#include "session.h"

#include <QHash>
#include <QListWidget>
#include <QPoint>
#include <QMouseEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QWidget>
#include <QVBoxLayout>

QT_BEGIN_NAMESPACE
namespace Ui {
class Widget;
}
QT_END_NAMESPACE

class Widget : public QWidget
{
    Q_OBJECT

public:
    Widget(QWidget *parent = nullptr);
    ~Widget();
    
    // 设置当前登录用户信息的接口
    void setUserInfo(const QString& username, const QString& avatarPath = ":/resources/avatar.png");

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    void initUI();
    void addSessionItem(const Session &session);

    Ui::Widget *ui;
    
    // UI Components
    QWidget* m_topPanel;
    QLabel* m_avatarLabel;
    QLabel* m_nameLabel;
    
    QListWidget* m_sessionList;
    QHash<QString, Session> m_sessionsById;
    
    // Dragging support
    bool m_isDragging;
    QPoint m_dragPosition;

private slots:
    void onSessionDoubleClicked(QListWidgetItem *item);
};
#endif // WIDGET_H
