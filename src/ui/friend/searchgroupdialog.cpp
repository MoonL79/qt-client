#include "searchgroupdialog.h"

#include <QDialogButtonBox>
#include <QColor>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QSizePolicy>
#include <QStyle>
#include <QVBoxLayout>

SearchGroupDialog::SearchGroupDialog(ProfileApiClient *profileApiClient,
                                     QWidget *parent)
    : QDialog(parent), m_profileApiClient(profileApiClient) {
  setAttribute(Qt::WA_DeleteOnClose);
  setWindowTitle(QStringLiteral("搜索群聊"));
  setModal(true);
  resize(460, 540);

  buildUi();
  clearResults(QStringLiteral("请输入关键字后搜索"));

  if (!m_profileApiClient) {
    m_keywordEdit->setEnabled(false);
    m_resultListWidget->setEnabled(false);
    m_statusLabel->setText(QStringLiteral("Profile 服务未初始化"));
    return;
  }

  connect(m_profileApiClient, &ProfileApiClient::groupsListed, this,
          &SearchGroupDialog::onGroupsListed);
  connect(m_profileApiClient, &ProfileApiClient::joinGroupSucceeded, this,
          &SearchGroupDialog::onJoinGroupSucceeded);
  connect(m_profileApiClient, &ProfileApiClient::requestFailedDetailed, this,
          &SearchGroupDialog::onRequestFailedDetailed);
}

void SearchGroupDialog::buildUi() {
  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(16, 16, 16, 16);
  layout->setSpacing(10);

  auto *topRow = new QHBoxLayout();
  topRow->setSpacing(8);

  m_keywordEdit = new QLineEdit(this);
  m_keywordEdit->setPlaceholderText(QStringLiteral("输入群名称关键字或群号"));
  topRow->addWidget(m_keywordEdit, 1);

  m_searchButton = new QPushButton(QStringLiteral("搜索"), this);
  topRow->addWidget(m_searchButton);
  layout->addLayout(topRow);

  m_statusLabel = new QLabel(this);
  m_statusLabel->setStyleSheet(QStringLiteral("color:#666666;"));
  layout->addWidget(m_statusLabel);

  m_resultListWidget = new QListWidget(this);
  m_resultListWidget->setStyleSheet(
      "QListWidget { border: 1px solid #d9d9d9; border-radius: 6px; "
      "background: #ffffff; color: #000000; }"
      "QListWidget::item { padding: 10px 12px; border-bottom: 1px solid #f0f0f0; "
      "color: #000000; }"
      "QListWidget::item:selected { background: #e9f3ff; color: #16324f; }"
      "QListWidget::item:hover { background: #f5f9ff; }");
  m_resultListWidget->setSpacing(6);
  layout->addWidget(m_resultListWidget, 1);

  auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Close, this);
  layout->addWidget(buttonBox);

  connect(m_searchButton, &QPushButton::clicked, this,
          &SearchGroupDialog::onSearchClicked);
  connect(m_keywordEdit, &QLineEdit::returnPressed, this,
          &SearchGroupDialog::onSearchClicked);
  connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
  connect(m_resultListWidget, &QListWidget::itemDoubleClicked, this,
          &SearchGroupDialog::onItemActivated);
}

void SearchGroupDialog::onSearchClicked() {
  if (!m_profileApiClient || !m_keywordEdit || !m_pendingJoinRequestId.isEmpty()) {
    return;
  }

  const QString keyword = m_keywordEdit->text().trimmed();
  if (keyword.isEmpty()) {
    m_pendingSearchRequestId.clear();
    clearResults(QStringLiteral("关键字为空，未发起搜索"));
    return;
  }

  if (m_searchButton) {
    m_searchButton->setEnabled(false);
  }
  m_statusLabel->setText(QStringLiteral("搜索中..."));
  m_resultListWidget->setEnabled(false);
  m_pendingSearchRequestId =
      m_profileApiClient->listGroups(keyword, keyword);
}

void SearchGroupDialog::onGroupsListed(const QString &requestId,
                                       const QVector<GroupSearchItem> &groups) {
  if (requestId != m_pendingSearchRequestId) {
    return;
  }

  m_pendingSearchRequestId.clear();
  if (m_searchButton) {
    m_searchButton->setEnabled(true);
  }
  m_resultListWidget->setEnabled(true);
  renderGroups(groups);
}

void SearchGroupDialog::onJoinGroupSucceeded(const QString &requestId,
                                             const JoinGroupResult &result) {
  if (requestId != m_pendingJoinRequestId) {
    return;
  }

  const int joinedIndex = m_pendingJoinGroupIndex;
  m_pendingJoinRequestId.clear();
  m_pendingJoinGroupIndex = -1;
  setJoinLoading(false);

  if (joinedIndex >= 0 && joinedIndex < m_groups.size()) {
    GroupSearchItem &group = m_groups[joinedIndex];
    group.isMember = true;
    if (!result.conversationId.trimmed().isEmpty()) {
      group.conversationId = result.conversationId.trimmed();
    }
    if (!result.conversationUuid.trimmed().isEmpty()) {
      group.conversationUuid = result.conversationUuid.trimmed();
    }
    if (!result.groupNumericId.trimmed().isEmpty()) {
      group.groupNumericId = result.groupNumericId.trimmed();
    }
    if (!result.name.trimmed().isEmpty()) {
      group.name = result.name.trimmed();
    }
    if (result.memberCount > 0) {
      group.memberCount = result.memberCount;
    }
  }

  renderGroups(m_groups);
  emit groupJoined(result);

  const QString trimmedMessage = result.message.trimmed().toLower();
  QMessageBox::information(
      this, QStringLiteral("群聊状态"),
      trimmedMessage == QStringLiteral("already in group")
          ? QStringLiteral("已加入该群，正在进入群聊")
          : QStringLiteral("加入成功，正在进入群聊"));
  accept();
}

void SearchGroupDialog::onRequestFailedDetailed(const QString &requestId,
                                                const QString &action, int,
                                                const QString &error) {
  if (requestId != m_pendingSearchRequestId ||
      action != QStringLiteral("LIST_GROUPS")) {
    if (requestId != m_pendingJoinRequestId ||
        action != QStringLiteral("JOIN_GROUP")) {
      return;
    }

    const int failedIndex = m_pendingJoinGroupIndex;
    m_pendingJoinRequestId.clear();
    m_pendingJoinGroupIndex = -1;
    setJoinLoading(false);
    if (failedIndex >= 0 && failedIndex < m_groups.size()) {
      m_statusLabel->setText(error.trimmed().isEmpty()
                                 ? QStringLiteral("加入群聊失败，请稍后重试")
                                 : error.trimmed());
    }
    QMessageBox::warning(this, QStringLiteral("加入群聊失败"),
                         error.trimmed().isEmpty()
                             ? QStringLiteral("加入群聊失败，请稍后重试")
                             : error.trimmed());
    return;
  }

  m_pendingSearchRequestId.clear();
  if (m_searchButton) {
    m_searchButton->setEnabled(true);
  }
  m_resultListWidget->setEnabled(true);
  clearResults(error.trimmed().isEmpty() ? QStringLiteral("搜索群聊失败，请稍后重试")
                                         : error.trimmed());
  QMessageBox::warning(this, QStringLiteral("搜索群聊失败"), m_statusLabel->text());
}

void SearchGroupDialog::onItemActivated(QListWidgetItem *item) {
  if (!item) {
    return;
  }

  const int index = item->data(Qt::UserRole).toInt();
  if (index < 0 || index >= m_groups.size()) {
    return;
  }

  triggerGroupAction(index);
}

void SearchGroupDialog::onActionButtonClicked() {
  const QObject *senderObject = sender();
  if (!senderObject) {
    return;
  }
  const int index = senderObject->property("group_index").toInt();
  triggerGroupAction(index);
}

void SearchGroupDialog::triggerGroupAction(int index) {
  if (!m_profileApiClient || index < 0 || index >= m_groups.size()) {
    return;
  }
  if (!m_pendingJoinRequestId.isEmpty()) {
    return;
  }

  const GroupSearchItem &group = m_groups.at(index);
  if (group.isMember) {
    JoinGroupResult result;
    result.ok = true;
    result.message = QStringLiteral("open_existing_group");
    result.conversationId = group.conversationId.trimmed();
    result.conversationUuid = group.conversationUuid.trimmed();
    result.groupNumericId = group.groupNumericId.trimmed();
    result.conversationType =
        group.conversationType == 0 ? 2 : group.conversationType;
    result.name = group.name.trimmed();
    result.ownerUserId = group.ownerUserId.trimmed();
    result.memberCount = group.memberCount;
    emit groupJoined(result);
    accept();
    return;
  }

  setJoinLoading(true, index);
  m_pendingJoinGroupIndex = index;
  m_statusLabel->setText(QStringLiteral("加入群聊中..."));
  m_pendingJoinRequestId = m_profileApiClient->joinGroup(
      group.groupNumericId.trimmed(), group.conversationId.trimmed());
}

void SearchGroupDialog::renderGroups(const QVector<GroupSearchItem> &groups) {
  m_groups = groups;
  m_resultListWidget->clear();

  if (groups.isEmpty()) {
    clearResults(QStringLiteral("未找到匹配的群聊"));
    return;
  }

  for (int i = 0; i < groups.size(); ++i) {
    const GroupSearchItem &group = groups.at(i);
    auto *item = new QListWidgetItem(m_resultListWidget);
    item->setData(Qt::UserRole, i);
    item->setData(Qt::UserRole + 1, group.conversationId);
    item->setData(Qt::UserRole + 2, group.groupNumericId);
    item->setData(Qt::UserRole + 3, group.name);

    auto *rowWidget = new QWidget(m_resultListWidget);
    rowWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    auto *rowLayout = new QHBoxLayout(rowWidget);
    rowLayout->setContentsMargins(12, 8, 12, 8);
    rowLayout->setSpacing(12);

    auto *iconLabel = new QLabel(rowWidget);
    iconLabel->setPixmap(defaultGroupIcon().pixmap(20, 20));
    rowLayout->addWidget(iconLabel, 0, Qt::AlignTop);

    auto *textLayout = new QVBoxLayout();
    textLayout->setContentsMargins(0, 0, 0, 0);
    textLayout->setSpacing(3);

    auto *titleLabel = new QLabel(
        QStringLiteral("%1  [%2]")
            .arg(group.name, group.isMember ? QStringLiteral("已加入")
                                            : QStringLiteral("可加入")),
        rowWidget);
    titleLabel->setStyleSheet(QStringLiteral("font-weight:600; color:#111111;"));
    titleLabel->setWordWrap(true);
    textLayout->addWidget(titleLabel);

    QString metaText = QStringLiteral("成员数: %1").arg(group.memberCount);
    if (!group.groupNumericId.trimmed().isEmpty()) {
      metaText += QStringLiteral("  群号: %1").arg(group.groupNumericId.trimmed());
    }
    auto *metaLabel = new QLabel(metaText, rowWidget);
    metaLabel->setStyleSheet(QStringLiteral("color:#555555;"));
    metaLabel->setWordWrap(true);
    textLayout->addWidget(metaLabel);

    if (!group.notice.trimmed().isEmpty()) {
      auto *noticeLabel =
          new QLabel(QStringLiteral("公告: %1").arg(group.notice.trimmed()), rowWidget);
      noticeLabel->setStyleSheet(QStringLiteral("color:#777777;"));
      noticeLabel->setWordWrap(true);
      textLayout->addWidget(noticeLabel);
    }

    rowLayout->addLayout(textLayout, 1);

    auto *actionButton =
        new QPushButton(group.isMember ? QStringLiteral("进入群聊")
                                       : QStringLiteral("加入"),
                        rowWidget);
    actionButton->setFixedWidth(84);
    actionButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    actionButton->setProperty("group_index", i);
    actionButton->setEnabled(m_pendingJoinRequestId.isEmpty());
    actionButton->setStyleSheet(QStringLiteral(
        "QPushButton {"
        "  padding: 6px 14px;"
        "  border: none;"
        "  border-radius: 6px;"
        "  background: #2f80ed;"
        "  color: #ffffff;"
        "}"
        "QPushButton:hover { background: #1f6fd6; }"
        "QPushButton:pressed { background: #195fb7; }"
        "QPushButton:disabled { background: #9bbce8; color: #eef4ff; }"));
    connect(actionButton, &QPushButton::clicked, this,
            &SearchGroupDialog::onActionButtonClicked);
    rowLayout->addWidget(actionButton, 0, Qt::AlignTop);

    rowLayout->activate();
    rowWidget->adjustSize();
    QSize rowSize = rowWidget->sizeHint();
    const int minRowHeight = group.notice.trimmed().isEmpty() ? 72 : 96;
    if (rowSize.height() < minRowHeight) {
      rowSize.setHeight(minRowHeight);
    }
    item->setSizeHint(rowSize);
    item->setToolTip(group.notice.trimmed().isEmpty()
                         ? (group.groupNumericId.trimmed().isEmpty()
                                ? QStringLiteral("暂无群公告")
                                : QStringLiteral("群号: %1").arg(group.groupNumericId))
                         : group.notice.trimmed());
    if (group.isMember) {
      item->setForeground(QColor(QStringLiteral("#1d5f2f")));
    }
    m_resultListWidget->setItemWidget(item, rowWidget);
  }

  m_statusLabel->setText(QStringLiteral("搜索到 %1 个群聊结果").arg(groups.size()));
}

void SearchGroupDialog::clearResults(const QString &statusText) {
  m_groups.clear();
  if (m_resultListWidget) {
    m_resultListWidget->clear();
  }
  if (m_statusLabel) {
    m_statusLabel->setText(statusText);
  }
}

void SearchGroupDialog::setJoinLoading(bool loading, int index) {
  if (!m_resultListWidget) {
    return;
  }

  for (int i = 0; i < m_resultListWidget->count(); ++i) {
    QListWidgetItem *item = m_resultListWidget->item(i);
    QWidget *rowWidget = item ? m_resultListWidget->itemWidget(item) : nullptr;
    if (!rowWidget) {
      continue;
    }
    const auto buttons = rowWidget->findChildren<QPushButton *>();
    for (QPushButton *button : buttons) {
      if (!button) {
        continue;
      }
      const int buttonIndex = button->property("group_index").toInt();
      if (loading && buttonIndex == index) {
        button->setText(QStringLiteral("加入中..."));
      } else if (buttonIndex >= 0 && buttonIndex < m_groups.size()) {
        button->setText(m_groups.at(buttonIndex).isMember
                            ? QStringLiteral("进入群聊")
                            : QStringLiteral("加入"));
      }
      button->setEnabled(!loading);
    }
  }
  if (m_searchButton) {
    m_searchButton->setEnabled(!loading);
  }
}

QIcon SearchGroupDialog::defaultGroupIcon() const {
  return style() ? style()->standardIcon(QStyle::SP_DirIcon) : QIcon();
}
