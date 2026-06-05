#include "logwindow.h"

#include <QTextEdit>
#include <QVBoxLayout>

/**
 * @brief 构造并初始化LogWindow实例。
 * @param parent 父级对象指针，用于管理当前对象的生命周期。
 * @return 无返回值。
 */
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

/**
 * @brief 追加log内容。
 * @param line 字符串参数 `line`。
 * @return 无返回值。
 */
void LogWindow::appendLog(const QString &line) {
  if (!m_logBox)
    return;
  m_logBox->append(line);
}


