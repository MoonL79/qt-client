#include "loginwindow.h"
#include "logwindow.h"
#include "profileapiclient.h"
#include "usersession.h"
#include "widget.h"

#include <QApplication>
#include <QDateTime>
#include <QMetaObject>
#include <QRegularExpression>
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
    UserSession::instance().clear();

    LogWindow logWindow;
    g_logWindow = &logWindow;
    g_previousHandler = qInstallMessageHandler(appMessageHandler);
    logWindow.show();
    
    // 创建登录窗口
    LoginWindow loginWindow;
    Widget mainWidget;
    ProfileApiClient profileApiClient;
    QString currentUserId;
    mainWidget.setProfileApiClient(&profileApiClient);

    const auto applyProfileToMainWidget =
        [&](const ProfileInfo &info) {
          const QString displayName = info.nickname.trimmed().isEmpty()
                                          ? currentUserId
                                          : info.nickname.trimmed();
          mainWidget.setCurrentUserNumericId(info.numericId);
          mainWidget.setUserInfo(displayName, info.avatarUrl, info.signature);
        };

    QObject::connect(&profileApiClient, &ProfileApiClient::profileInfoReceived,
                     [&](const QString &requestId, const ProfileInfo &info) {
      Q_UNUSED(requestId);
      applyProfileToMainWidget(info);
      qInfo() << "Profile GET_INFO success for user:" << currentUserId;
    });

    QObject::connect(&profileApiClient, &ProfileApiClient::profileInfoSetSuccess,
                     [&](const QString &requestId, const ProfileInfo &info) {
      applyProfileToMainWidget(info);
      qInfo() << "Profile SET_INFO success request_id:" << requestId;
    });

    QObject::connect(&profileApiClient, &ProfileApiClient::requestFailed,
                     [&](const QString &requestId, const QString &action,
                         const QString &error) {
      qWarning() << "Profile request failed, action:" << action
                 << "request_id:" << requestId << "error:" << error;
    });
    
    // 登录成功后显示主窗口
    QObject::connect(&loginWindow, &LoginWindow::loginSuccess,
                     [&](const QString &username, const QString &userId) {
        static const QRegularExpression kUnsignedIntRe(QStringLiteral("^\\d+$"));
        currentUserId.clear();
        loginWindow.close();
        mainWidget.setUserInfo(username); // 设置用户信息
        mainWidget.setWindowTitle("IM聊天 - " + username);
        mainWidget.show();
        mainWidget.setCurrentUserNumericId(UserSession::instance().numericId());
        const QString normalizedUserId = userId.trimmed();
        if (kUnsignedIntRe.match(normalizedUserId).hasMatch()) {
          currentUserId = normalizedUserId;
        }
        mainWidget.setCurrentUserId(currentUserId);
        if (!currentUserId.isEmpty()) {
          profileApiClient.requestProfileInfo(currentUserId);
        } else {
          qWarning() << "Skip PROFILE GET_INFO: missing numeric user_id from login response";
        }
    });
    
    loginWindow.show();
    const int exitCode = a.exec();
    qInstallMessageHandler(g_previousHandler);
    g_logWindow = nullptr;
    return exitCode;
}
