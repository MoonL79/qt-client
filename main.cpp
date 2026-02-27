#include "loginwindow.h"
#include "logwindow.h"
#include "widget.h"

#include <QApplication>
#include <QDateTime>
#include <QMetaObject>
#include <QtGlobal>
#include <cstdlib>
#include <cstdio>

namespace {
LogWindow *g_logWindow = nullptr;
QtMessageHandler g_previousHandler = nullptr;

QString messageTypeName(QtMsgType type) {
  switch (type) {
  case QtDebugMsg:
    return "DEBUG";
  case QtInfoMsg:
    return "INFO";
  case QtWarningMsg:
    return "WARN";
  case QtCriticalMsg:
    return "ERROR";
  case QtFatalMsg:
    return "FATAL";
  }
  return "UNKNOWN";
}

void forwardLogToWindow(const QString &line) {
  if (!g_logWindow)
    return;
  QMetaObject::invokeMethod(
      g_logWindow,
      [line]() {
        if (g_logWindow) {
          g_logWindow->appendLog(line);
        }
      },
      Qt::QueuedConnection);
}

void appMessageHandler(QtMsgType type, const QMessageLogContext &context,
                       const QString &message) {
  const QString timestamp = QDateTime::currentDateTime().toString("HH:mm:ss.zzz");
  const QString category =
      (context.category && context.category[0] != '\0') ? context.category : "app";
  const QString line =
      QString("%1 [%2] [%3] %4")
          .arg(timestamp, messageTypeName(type), category, message);

  std::fprintf(stderr, "%s\n", line.toLocal8Bit().constData());
  forwardLogToWindow(line);

  if (g_previousHandler) {
    g_previousHandler(type, context, message);
  }
  if (type == QtFatalMsg) {
    std::abort();
  }
}
} // namespace

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    LogWindow logWindow;
    g_logWindow = &logWindow;
    g_previousHandler = qInstallMessageHandler(appMessageHandler);
    logWindow.show();
    
    // 创建登录窗口
    LoginWindow loginWindow;
    Widget mainWidget;
    
    // 登录成功后显示主窗口
    QObject::connect(&loginWindow, &LoginWindow::loginSuccess, [&](const QString &username) {
        loginWindow.close();
        mainWidget.setUserInfo(username); // 设置用户信息
        mainWidget.setWindowTitle("IM聊天 - " + username);
        mainWidget.show();
    });
    
    loginWindow.show();
    const int exitCode = a.exec();
    qInstallMessageHandler(g_previousHandler);
    g_logWindow = nullptr;
    return exitCode;
}
