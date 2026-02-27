#ifndef LOGWINDOW_H
#define LOGWINDOW_H

#include <QWidget>

class QTextEdit;

class LogWindow : public QWidget {
  Q_OBJECT
public:
  explicit LogWindow(QWidget *parent = nullptr);
  void appendLog(const QString &line);

private:
  QTextEdit *m_logBox;
};

#endif // LOGWINDOW_H
