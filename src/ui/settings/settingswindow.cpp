#include "settingswindow.h"

#include <QLabel>
#include <QVBoxLayout>

SettingsWindow::SettingsWindow(QWidget *parent) : QWidget(parent) {
  setAttribute(Qt::WA_DeleteOnClose);
  setWindowTitle("设置");
  resize(480, 320);

  QVBoxLayout *layout = new QVBoxLayout(this);
  QLabel *placeholder = new QLabel("设置页面（空）", this);
  placeholder->setAlignment(Qt::AlignCenter);
  layout->addWidget(placeholder);
}
