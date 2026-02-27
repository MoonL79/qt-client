#include "logwindow.h"

#include <QTextEdit>
#include <QVBoxLayout>

LogWindow::LogWindow(QWidget *parent) : QWidget(parent), m_logBox(nullptr) {
  setWindowTitle("测试日志窗口");
  resize(900, 500);

  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(8, 8, 8, 8);
  layout->setSpacing(0);

  m_logBox = new QTextEdit(this);
  m_logBox->setReadOnly(true);
  m_logBox->setPlaceholderText("日志输出...");
  layout->addWidget(m_logBox);
}

void LogWindow::appendLog(const QString &line) {
  if (!m_logBox)
    return;
  m_logBox->append(line);
}
