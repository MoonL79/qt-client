#include "leavegroupdialog.h"

#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QVBoxLayout>

// 实现 `m_conversations` 的核心逻辑。
LeaveGroupDialog::LeaveGroupDialog(
    const QString &currentUserId,
    const QList<conversationlist::ConversationItem> &conversations,
    ProfileApiClient *profileApiClient, QWidget *parent)
    : QDialog(parent), m_profileApiClient(profileApiClient),
      m_currentUserId(currentUserId.trimmed()), m_conversations(conversations) {
  setAttribute(Qt::WA_DeleteOnClose);
  setWindowTitle(QStringLiteral("退出群聊"));
  setModal(true);
  resize(460, 540);

  buildUi();
  refreshList();

  if (!m_profileApiClient) {
    m_tipLabel->setText(QStringLiteral("Profile 服务未初始化"));
    m_groupListWidget->setEnabled(false);
    return;
  }

  connect(m_profileApiClient, &ProfileApiClient::groupsListed, this,
          &LeaveGroupDialog::onGroupsListed);
  connect(m_profileApiClient, &ProfileApiClient::leaveGroupFinished, this,
          &LeaveGroupDialog::onLeaveGroupFinished);
  connect(m_profileApiClient, &ProfileApiClient::requestFailedDetailed, this,
          &LeaveGroupDialog::onRequestFailedDetailed);
  requestOwnerInfoForGroups();
  refreshList();
}

// 设置会话值。
void LeaveGroupDialog::setConversations(
    const QList<conversationlist::ConversationItem> &conversations) {
  m_conversations = conversations;
  requestOwnerInfoForGroups();
  refreshList();
}

// 构建界面内容。
void LeaveGroupDialog::buildUi() {
  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(16, 16, 16, 16);
  layout->setSpacing(10);

  m_tipLabel = new QLabel(QStringLiteral("双击群聊项即可退出"), this);
  m_tipLabel->setStyleSheet(QStringLiteral("color:#000000;"));
  layout->addWidget(m_tipLabel);

  m_groupListWidget = new QListWidget(this);
  m_groupListWidget->setStyleSheet(
      "QListWidget { border: 1px solid #d9d9d9; border-radius: 6px; "
      "background: #ffffff; color: #000000; }"
      "QListWidget::item { padding: 10px 12px; border-bottom: 1px solid #f0f0f0; "
      "color: #000000; }"
      "QListWidget::item:selected { background: #f8e9e9; color: #222222; }"
      "QListWidget::item:hover { background: #faf4f4; }");
  layout->addWidget(m_groupListWidget, 1);

  connect(m_groupListWidget, &QListWidget::itemDoubleClicked, this,
          &LeaveGroupDialog::onItemDoubleClicked);
}

// 发起拥有者信息for群组请求。
void LeaveGroupDialog::requestOwnerInfoForGroups() {
  if (!m_profileApiClient) {
    return;
  }

  for (const conversationlist::ConversationItem &conversation : m_conversations) {
    if (conversation.conversationType != 2) {
      continue;
    }

    const QString groupNumericId = conversation.groupNumericId.trimmed();
    if (groupNumericId.isEmpty() ||
        m_ownerUserIdByGroupNumericId.contains(groupNumericId) ||
        m_ownerLookupRequestIdToGroupNumericId.values().contains(groupNumericId)) {
      continue;
    }

    const QString requestId = m_profileApiClient->listGroups(QString(), groupNumericId);
    m_ownerLookupRequestIdToGroupNumericId.insert(requestId, groupNumericId);
  }
}

// 刷新列表显示或缓存。
void LeaveGroupDialog::refreshList() {
  if (!m_groupListWidget) {
    return;
  }

  m_groupListWidget->clear();

  QList<conversationlist::ConversationItem> groups;
  for (const conversationlist::ConversationItem &conversation : m_conversations) {
    if (conversation.conversationType != 2 ||
        conversation.conversationId.trimmed().isEmpty()) {
      continue;
    }

    const QString groupNumericId = conversation.groupNumericId.trimmed();
    QString ownerUserId = conversation.ownerUserId.trimmed();
    if (ownerUserId.isEmpty() && !groupNumericId.isEmpty()) {
      ownerUserId = m_ownerUserIdByGroupNumericId.value(groupNumericId).trimmed();
    }

    if (!m_currentUserId.isEmpty() && !ownerUserId.isEmpty() &&
        ownerUserId == m_currentUserId) {
      continue;
    }

    if (!groupNumericId.isEmpty() && ownerUserId.isEmpty()) {
      continue;
    }

    if (m_currentUserId.isEmpty() || ownerUserId != m_currentUserId) {
      groups.push_back(conversation);
    }
  }
  if (groups.isEmpty()) {
    const QString emptyText = m_ownerLookupRequestIdToGroupNumericId.isEmpty()
                                  ? QStringLiteral("暂无可退出的群聊")
                                  : QStringLiteral("群信息加载中...");
    auto *emptyItem = new QListWidgetItem(emptyText);
    emptyItem->setFlags(emptyItem->flags() & ~Qt::ItemIsSelectable &
                        ~Qt::ItemIsEnabled);
    m_groupListWidget->addItem(emptyItem);
    m_tipLabel->setText(m_ownerLookupRequestIdToGroupNumericId.isEmpty()
                            ? QStringLiteral("当前没有可退出的群聊")
                            : QStringLiteral("正在加载群主信息..."));
    return;
  }

  for (const conversationlist::ConversationItem &conversation : groups) {
    QString text = conversation.name.trimmed();
    if (text.isEmpty()) {
      text = conversation.conversationId.trimmed();
    }

    QStringList meta;
    if (!conversation.groupNumericId.trimmed().isEmpty()) {
      meta.push_back(QStringLiteral("群号:%1").arg(conversation.groupNumericId.trimmed()));
    }
    if (conversation.memberCount > 0) {
      meta.push_back(QStringLiteral("%1人").arg(conversation.memberCount));
    }
    if (!meta.isEmpty()) {
      text += QStringLiteral("\n") + meta.join(QStringLiteral("  "));
    }

    auto *item = new QListWidgetItem(text);
    item->setData(Qt::UserRole, conversation.conversationId.trimmed());
    item->setData(Qt::UserRole + 1, conversation.groupNumericId.trimmed());
    item->setData(Qt::UserRole + 2, conversation.name.trimmed());
    item->setToolTip(conversation.groupNumericId.trimmed().isEmpty()
                         ? QStringLiteral("双击退出群聊")
                         : QStringLiteral("群号: %1").arg(
                               conversation.groupNumericId.trimmed()));
    m_groupListWidget->addItem(item);
  }

  m_tipLabel->setText(QStringLiteral("双击群聊项即可退出"));
}

// 解析并确定退出错误消息结果。
QString LeaveGroupDialog::resolveLeaveErrorMessage(int code,
                                                   const QString &error) const {
  if (code == 2001) {
    return QStringLiteral("当前登录状态无效，请重新登录后再试");
  }
  if (code == 2005) {
    return QStringLiteral("群主不能直接退群");
  }
  return error.trimmed().isEmpty() ? QStringLiteral("退出群聊失败，请稍后重试")
                                   : error.trimmed();
}

// 响应itemdouble点击事件。
void LeaveGroupDialog::onItemDoubleClicked(QListWidgetItem *item) {
  if (!item || !m_profileApiClient || !m_pendingLeaveRequestId.isEmpty()) {
    return;
  }

  const QString conversationId = item->data(Qt::UserRole).toString().trimmed();
  const QString displayName = item->data(Qt::UserRole + 2).toString().trimmed();
  if (conversationId.isEmpty()) {
    m_tipLabel->setText(QStringLiteral("群聊会话标识无效"));
    return;
  }

  const int answer = QMessageBox::question(
      this, QStringLiteral("确认退出群聊"),
      QStringLiteral("确定退出群聊：%1 ?")
          .arg(displayName.isEmpty() ? conversationId : displayName),
      QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
  if (answer != QMessageBox::Yes) {
    return;
  }

  m_groupListWidget->setEnabled(false);
  m_tipLabel->setText(QStringLiteral("正在退出群聊..."));
  m_pendingLeaveRequestId =
      m_profileApiClient->leaveGroupByConversationId(conversationId);
}

// 响应群组listed事件。
void LeaveGroupDialog::onGroupsListed(const QString &requestId,
                                      const QVector<GroupSearchItem> &groups) {
  const auto it = m_ownerLookupRequestIdToGroupNumericId.find(requestId);
  if (it == m_ownerLookupRequestIdToGroupNumericId.end()) {
    return;
  }

  const QString groupNumericId = it.value();
  m_ownerLookupRequestIdToGroupNumericId.erase(it);
  for (const GroupSearchItem &group : groups) {
    if (!group.groupNumericId.trimmed().isEmpty()) {
      m_ownerUserIdByGroupNumericId.insert(group.groupNumericId.trimmed(),
                                           group.ownerUserId.trimmed());
    } else if (!groupNumericId.isEmpty()) {
      m_ownerUserIdByGroupNumericId.insert(groupNumericId,
                                           group.ownerUserId.trimmed());
    }
  }
  refreshList();
}

// 响应退出群组完成事件。
void LeaveGroupDialog::onLeaveGroupFinished(const QString &requestId,
                                            const LeaveGroupResult &result) {
  if (requestId != m_pendingLeaveRequestId) {
    return;
  }

  m_pendingLeaveRequestId.clear();
  m_groupListWidget->setEnabled(true);

  if (!result.ok) {
    m_tipLabel->setText(resolveLeaveErrorMessage(result.code, result.message));
    QMessageBox::warning(this, QStringLiteral("退出群聊失败"), m_tipLabel->text());
    return;
  }

  const QString conversationId = result.conversationId.trimmed();
  for (qsizetype i = 0; i < m_conversations.size(); ++i) {
    if (m_conversations.at(i).conversationId.trimmed() == conversationId) {
      m_conversations.removeAt(i);
      break;
    }
  }

  refreshList();
  m_tipLabel->setText(result.message.trimmed().isEmpty()
                          ? QStringLiteral("退出群聊成功")
                          : result.message.trimmed());
  emit groupLeft(result);
}

// 响应请求失败detailed事件。
void LeaveGroupDialog::onRequestFailedDetailed(const QString &requestId,
                                               const QString &action, int code,
                                               const QString &error) {
  if (action == QStringLiteral("LIST_GROUPS") &&
      m_ownerLookupRequestIdToGroupNumericId.contains(requestId)) {
    m_ownerLookupRequestIdToGroupNumericId.remove(requestId);
    refreshList();
    return;
  }

  if (requestId != m_pendingLeaveRequestId ||
      action != QStringLiteral("LEAVE_GROUP")) {
    return;
  }

  m_pendingLeaveRequestId.clear();
  m_groupListWidget->setEnabled(true);
  m_tipLabel->setText(resolveLeaveErrorMessage(code, error));
}

