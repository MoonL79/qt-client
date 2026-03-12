#include "deletefrienddialog.h"

#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QRegularExpression>
#include <QVBoxLayout>

namespace {
QString friendStatusText(int status) {
  switch (status) {
  case 1:
    return QStringLiteral("在线");
  case 0:
    return QStringLiteral("离线");
  default:
    return QStringLiteral("状态:%1").arg(status);
  }
}
}

DeleteFriendDialog::DeleteFriendDialog(
    const QString &currentUserNumericId,
    const QList<friendlist::FriendItem> &friends,
    ProfileApiClient *profileApiClient, QWidget *parent)
    : QDialog(parent), m_profileApiClient(profileApiClient),
      m_currentUserNumericId(currentUserNumericId.trimmed()), m_friends(friends) {
  setAttribute(Qt::WA_DeleteOnClose);
  setWindowTitle(QStringLiteral("删除好友"));
  setModal(true);
  resize(420, 520);

  buildUi();
  refreshList();

  if (!m_profileApiClient) {
    m_tipLabel->setText(QStringLiteral("Profile 服务未初始化"));
    m_friendListWidget->setEnabled(false);
    return;
  }

  connect(m_profileApiClient, &ProfileApiClient::deleteFriendFinished, this,
          &DeleteFriendDialog::onDeleteFriendFinished);
  connect(m_profileApiClient, &ProfileApiClient::requestFailedDetailed, this,
          &DeleteFriendDialog::onRequestFailedDetailed);
}

void DeleteFriendDialog::setFriends(const QList<friendlist::FriendItem> &friends) {
  m_friends = friends;
  refreshList();
}

void DeleteFriendDialog::buildUi() {
  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(16, 16, 16, 16);
  layout->setSpacing(10);

  m_tipLabel =
      new QLabel(QStringLiteral("双击好友项即可删除"), this);
  m_tipLabel->setStyleSheet(QStringLiteral("color:#000000;"));
  layout->addWidget(m_tipLabel);

  m_friendListWidget = new QListWidget(this);
  m_friendListWidget->setStyleSheet(
      "QListWidget { border: 1px solid #d9d9d9; border-radius: 6px; "
      "background: #ffffff; color: #000000; }"
      "QListWidget::item { padding: 10px 12px; border-bottom: 1px solid #f0f0f0; "
      "color: #000000; }"
      "QListWidget::item:selected { background: #f8e9e9; color: #222222; }"
      "QListWidget::item:hover { background: #faf4f4; }");
  layout->addWidget(m_friendListWidget, 1);

  connect(m_friendListWidget, &QListWidget::itemDoubleClicked, this,
          &DeleteFriendDialog::onItemDoubleClicked);
}

void DeleteFriendDialog::refreshList() {
  if (!m_friendListWidget) {
    return;
  }

  m_friendListWidget->clear();
  if (m_friends.isEmpty()) {
    auto *emptyItem = new QListWidgetItem(QStringLiteral("暂无好友"));
    emptyItem->setFlags(emptyItem->flags() & ~Qt::ItemIsSelectable &
                        ~Qt::ItemIsEnabled);
    m_friendListWidget->addItem(emptyItem);
  } else {
    for (const friendlist::FriendItem &friendItem : m_friends) {
      const QString text =
          QStringLiteral("%1 (%2) [%3]")
              .arg(friendItem.displayName, friendItem.numericId,
                   friendStatusText(friendItem.status));
      auto *item = new QListWidgetItem(text);
      item->setToolTip(friendItem.bio.trimmed().isEmpty()
                           ? QStringLiteral("无个性签名")
                           : friendItem.bio.trimmed());
      item->setData(Qt::UserRole, friendItem.userId);
      item->setData(Qt::UserRole + 1, friendItem.numericId);
      item->setData(Qt::UserRole + 2, friendItem.status);
      m_friendListWidget->addItem(item);
    }
  }

  if (m_friends.isEmpty()) {
    m_tipLabel->setText(QStringLiteral("当前没有可删除的好友"));
  }
}

bool DeleteFriendDialog::isValidNumericId(const QString &numericId) const {
  static const QRegularExpression kUnsignedIntRe(QStringLiteral("^\\d+$"));
  return kUnsignedIntRe.match(numericId.trimmed()).hasMatch();
}

QString DeleteFriendDialog::resolveDeleteErrorMessage(int code,
                                                      const QString &error) const {
  if (code == 3001) {
    return QStringLiteral("好友关系不存在或用户不存在");
  }
  if (code == 3003) {
    return QStringLiteral("删除好友参数错误");
  }
  if (code == 1099) {
    return QStringLiteral("服务端异常，请稍后重试");
  }
  return error.trimmed().isEmpty() ? QStringLiteral("删除好友失败，请稍后重试")
                                   : error.trimmed();
}

void DeleteFriendDialog::onItemDoubleClicked(QListWidgetItem *item) {
  if (!item || !m_profileApiClient || !m_pendingDeleteRequestId.isEmpty()) {
    return;
  }

  const QString friendNumericId = item->data(Qt::UserRole + 1).toString().trimmed();
  const QString displayName = item->text().trimmed();
  if (!isValidNumericId(m_currentUserNumericId) ||
      !isValidNumericId(friendNumericId)) {
    m_tipLabel->setText(QStringLiteral("当前用户编号或好友编号无效"));
    return;
  }

  const int answer = QMessageBox::question(
      this, QStringLiteral("确认删除"),
      QStringLiteral("确定删除好友：%1 ?").arg(displayName),
      QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
  if (answer != QMessageBox::Yes) {
    return;
  }

  m_friendListWidget->setEnabled(false);
  m_tipLabel->setText(QStringLiteral("正在删除好友..."));
  m_pendingDeleteRequestId =
      m_profileApiClient->deleteFriend(m_currentUserNumericId, friendNumericId);
}

void DeleteFriendDialog::onDeleteFriendFinished(const QString &requestId,
                                                const DeleteFriendResult &result) {
  if (requestId != m_pendingDeleteRequestId) {
    return;
  }

  m_pendingDeleteRequestId.clear();
  m_friendListWidget->setEnabled(true);

  const QString deletedNumericId = result.friendNumericId.trimmed();
  for (qsizetype i = 0; i < m_friends.size(); ++i) {
    if (m_friends.at(i).numericId.trimmed() == deletedNumericId) {
      m_friends.removeAt(i);
      break;
    }
  }

  refreshList();
  m_tipLabel->setText(result.message.trimmed().isEmpty()
                          ? QStringLiteral("好友删除成功")
                          : result.message.trimmed());
  emit friendDeleted(result);
}

void DeleteFriendDialog::onRequestFailedDetailed(const QString &requestId,
                                                 const QString &action, int code,
                                                 const QString &error) {
  if (requestId != m_pendingDeleteRequestId ||
      action != QStringLiteral("DELETE_FRIEND")) {
    return;
  }

  m_pendingDeleteRequestId.clear();
  m_friendListWidget->setEnabled(true);
  m_tipLabel->setText(resolveDeleteErrorMessage(code, error));
}
