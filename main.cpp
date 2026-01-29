#include "loginwindow.h"
#include "widget.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    
    // 创建登录窗口
    LoginWindow loginWindow;
    Widget mainWidget;
    
    // 登录成功后显示主窗口
    QObject::connect(&loginWindow, &LoginWindow::loginSuccess, [&](const QString &username) {
        loginWindow.close();
        mainWidget.setWindowTitle("IM聊天 - " + username);
        mainWidget.show();
    });
    
    loginWindow.show();
    return a.exec();
}
