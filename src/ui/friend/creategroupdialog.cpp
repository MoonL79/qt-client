#include "creategroupdialog.h"

#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QVBoxLayout>

CreateGroupDialog::CreateGroupDialog(
    const QList<friendlist::FriendItem> &friends,
    ProfileApiClient *profileApiClient, QWidget *parent)
    : QDialog(parent), m_profileApiClient(profileApiClient), m_friends(friends) {
  setAttribute(Qt::WA_DeleteOnClose);
  setWindowTitle(QStringLiteral("创建群聊"));
  setModal(true);
  resize(420, 520);

  buildUi();
  refreshFriendList();

  if (!m_profileApiClient) {
    m_statusLabel->setText(QStringLiteral("Profile 服务未初始化"));
    m_groupNameEdit->setEnabled(false);
    m_friendListWidget->setEnabled(false);
    return;
  }

  connect(m_profileApiClient, &ProfileApiClient::createGroupSucceeded, this,
          &CreateGroupDialog::onCreateGroupSucceeded);
  connect(m_profileApiClient, &ProfileApiClient::requestFailedDetailed, this,
          &CreateGroupDialog::onRequestFailedDetailed);
}

void CreateGroupDialog::setFriends(const QList<friendlist::FriendItem> &friends) {
  m_friends = friends;
  refreshFriendList();
}

void CreateGroupDialog::buildUi() {
  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(16, 16, 16, 16);
  layout->setSpacing(10);

  auto *tipLabel = new QLabel(QStringLiteral("输入群名称，并至少选择 1 个好友"), this);
  tipLabel->setStyleSheet(QStringLiteral("color:#000000;"));
  layout->addWidget(tipLabel);

  m_groupNameEdit = new QLineEdit(this);
  m_groupNameEdit->setPlaceholderText(QStringLiteral("输入群名称"));
  layout->addWidget(m_groupNameEdit);

  m_friendListWidget = new QListWidget(this);
  m_friendListWidget->setSelectionMode(QAbstractItemView::NoSelection);
  m_friendListWidget->setStyleSheet(
      "QListWidget { border: 1px solid #d9d9d9; border-radius: 6px; "
      "background: #ffffff; color: #000000; }"
      "QListWidget::item { padding: 10px 12px; border-bottom: 1px solid #f0f0f0; "
      "color: #000000; }"
      "QListWidget::item:selected { background: #d6ebff; color: #0f2d4a; }"
      "QListWidget::item:hover { background: #eef7ff; }"
      "QListWidget::indicator { width: 18px; height: 18px; }"
      "QListWidget::indicator:unchecked { border: 2px solid #7d8b99; "
      "background: #ffffff; border-radius: 4px; }"
      "QListWidget::indicator:checked { border: 2px solid #1f6fd6; "
      "background: #1f6fd6; border-radius: 4px; }");
  layout->addWidget(m_friendListWidget, 1);

  m_statusLabel = new QLabel(QStringLiteral("请选择群成员"), this);
  m_statusLabel->setStyleSheet(QStringLiteral("color:#666666;"));
  layout->addWidget(m_statusLabel);

  auto *buttonBox = new QDialogButtonBox(this);
  auto *confirmButton =
      buttonBox->addButton(QStringLiteral("确认创建"), QDialogButtonBox::AcceptRole);
  buttonBox->addButton(QStringLiteral("取消"), QDialogButtonBox::RejectRole);
  layout->addWidget(buttonBox);

  connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
  connect(confirmButton, &QPushButton::clicked, this,
          &CreateGroupDialog::onCreateClicked);
}

void CreateGroupDialog::refreshFriendList() {
  if (!m_friendListWidget) {
    return;
  }

  m_friendListWidget->clear();
  if (m_friends.isEmpty()) {
    auto *emptyItem = new QListWidgetItem(QStringLiteral("暂无可选好友"));
    emptyItem->setFlags(emptyItem->flags() & ~Qt::ItemIsSelectable &
                        ~Qt::ItemIsEnabled);
    m_friendListWidget->addItem(emptyItem);
    return;
  }

  for (const friendlist::FriendItem &friendItem : m_friends) {
    const QString text =
        QStringLiteral("%1 (%2)")
            .arg(friendItem.displayName.trimmed().isEmpty()
                     ? friendItem.username
                     : friendItem.displayName,
                 friendItem.numericId);
    auto *item = new QListWidgetItem(text, m_friendListWidget);
    item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
    item->setCheckState(Qt::Unchecked);
    item->setData(Qt::UserRole, friendItem.numericId);
    item->setToolTip(friendItem.bio.trimmed().isEmpty()
                         ? QStringLiteral("无个性签名")
                         : friendItem.bio.trimmed());
  }
}

bool CreateGroupDialog::isValidNumericId(const QString &numericId) const {
  static const QRegularExpression kUnsignedIntRe(QStringLiteral("^\\d+$"));
  return kUnsignedIntRe.match(numericId.trimmed()).hasMatch();
}

QStringList CreateGroupDialog::selectedMemberNumericIds() const {
  QStringList memberNumericIds;
  if (!m_friendListWidget) {
    return memberNumericIds;
  }

  for (int i = 0; i < m_friendListWidget->count(); ++i) {
    QListWidgetItem *item = m_friendListWidget->item(i);
    if (!item) {
      continue;
    }
    if (item->checkState() != Qt::Checked) {
      continue;
    }
    const QString numericId = item->data(Qt::UserRole).toString().trimmed();
    if (!isValidNumericId(numericId) || memberNumericIds.contains(numericId)) {
      continue;
    }
    memberNumericIds.push_back(numericId);
  }
  return memberNumericIds;
}

void CreateGroupDialog::onCreateClicked() {
  if (!m_profileApiClient || !m_pendingCreateRequestId.isEmpty()) {
    return;
  }

  const QString groupName =
      m_groupNameEdit ? m_groupNameEdit->text().trimmed() : QString();
  const QStringList memberNumericIds = selectedMemberNumericIds();
  if (groupName.isEmpty()) {
    m_statusLabel->setText(QStringLiteral("群名称不能为空"));
    return;
  }
  if (memberNumericIds.isEmpty()) {
    m_statusLabel->setText(QStringLiteral("至少选择 1 个好友"));
    return;
  }

  m_groupNameEdit->setEnabled(false);
  m_friendListWidget->setEnabled(false);
  m_statusLabel->setText(QStringLiteral("群聊创建中..."));
  m_pendingCreateRequestId =
      m_profileApiClient->createGroup(groupName, memberNumericIds);
}

void CreateGroupDialog::onCreateGroupSucceeded(
    const QString &requestId, const CreateGroupResult &result) {
  if (requestId != m_pendingCreateRequestId) {
    return;
  }

  m_pendingCreateRequestId.clear();
  emit groupCreated(result);
  QMessageBox::information(this, QStringLiteral("创建成功"),
                           QStringLiteral("群聊“%1”已创建").arg(result.name));
  accept();
}

void CreateGroupDialog::onRequestFailedDetailed(const QString &requestId,
                                                const QString &action, int,
                                                const QString &error) {
  if (requestId != m_pendingCreateRequestId ||
      action != QStringLiteral("CREATE_GROUP")) {
    return;
  }

  m_pendingCreateRequestId.clear();
  m_groupNameEdit->setEnabled(true);
  m_friendListWidget->setEnabled(true);
  m_statusLabel->setText(error.trimmed().isEmpty()
                             ? QStringLiteral("创建群聊失败，请稍后重试")
                             : error.trimmed());
}
