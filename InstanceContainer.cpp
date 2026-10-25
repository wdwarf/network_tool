#include "InstanceContainer.h"

#include <QEvent>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonValue>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QTabBar>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QWheelEvent>

#include "Configurable.h"

InstanceContainer::InstanceContainer(const QString& baseName, Factory factory,
                                     QWidget* parent)
    : QWidget(parent), m_baseName(baseName), m_factory(std::move(factory)) {
  m_tabs = new QTabWidget;
  m_tabs->setTabsClosable(true);
  connect(m_tabs, &QTabWidget::tabBarClicked, this,
          &InstanceContainer::onTabBarClicked);
  connect(m_tabs, &QTabWidget::tabBarDoubleClicked, this,
          &InstanceContainer::onTabDoubleClicked);
  connect(m_tabs, &QTabWidget::tabCloseRequested, this,
          &InstanceContainer::onCloseRequested);
  // 拦截页签栏的滚轮/方向键切换，避免落到常驻 "+" 占位页签
  m_tabs->tabBar()->installEventFilter(this);

  // "+" 页签：常驻末尾，内容为空状态提示
  m_placeholder = new QWidget;
  auto* ph = new QVBoxLayout(m_placeholder);
  ph->addStretch();
  auto* lbl = new QLabel(tr("No instances. Click the \"+\" tab to add one."));
  lbl->setAlignment(Qt::AlignCenter);
  ph->addWidget(lbl);
  ph->addStretch();
  m_tabs->addTab(m_placeholder, QStringLiteral("+"));
  ensurePlusNotClosable();

  auto* lay = new QVBoxLayout(this);
  lay->setContentsMargins(0, 0, 0, 0);
  lay->addWidget(m_tabs);

  // 初始即创建一个实例，保持原有“打开即用”体验
  addInstance();
}

int InstanceContainer::plusIndex() const {
  return m_tabs->count() - 1;  // "+" 始终在最后
}

void InstanceContainer::ensurePlusNotClosable() {
  const int idx = plusIndex();
  if (idx < 0) return;
  // 关闭按钮由样式决定置于左侧或右侧，两侧都置空以彻底移除
  m_tabs->tabBar()->setTabButton(idx, QTabBar::LeftSide, nullptr);
  m_tabs->tabBar()->setTabButton(idx, QTabBar::RightSide, nullptr);
}

void InstanceContainer::relayoutAfterRemove() {
  ensurePlusNotClosable();
  // 关闭末尾实例后，Qt 可能把当前页签切到 "+"，导致界面停在空状态占位；
  // 此时若仍有实例，切回末尾实例。
  if (m_tabs->count() >= 2 && m_tabs->currentIndex() == plusIndex())
    m_tabs->setCurrentIndex(plusIndex() - 1);
}

bool InstanceContainer::eventFilter(QObject* watched, QEvent* event) {
  if (watched == m_tabs->tabBar()) {
    // 拦截滚轮与左右方向键的页签切换，避免 QTabBar 默认导航落到常驻 "+"
    // 占位页签；点击 "+" 仍走 tabBarClicked → addInstance，不受影响。
    if (event->type() == QEvent::Wheel) {
      auto* we = static_cast<QWheelEvent*>(event);
      // 与 QTabBar::wheelEvent 一致地选取主轴 delta：上滚(>0)→前一页签
      const int delta = qAbs(we->angleDelta().x()) > qAbs(we->angleDelta().y())
                            ? we->angleDelta().x()
                            : we->angleDelta().y();
      navigateWithinInstances(delta > 0 ? -1 : 1);
      return true;  // 吃掉事件，阻止 QTabBar 默认行为落到 "+"
    }
    if (event->type() == QEvent::KeyPress) {
      auto* ke = static_cast<QKeyEvent*>(event);
      // QTabBar 仅处理 Left/Right；本项目为 LTR 布局：Left→前一，Right→后一
      if (ke->key() == Qt::Key_Left || ke->key() == Qt::Key_Right) {
        navigateWithinInstances(ke->key() == Qt::Key_Left ? -1 : 1);
        return true;
      }
    }
  }
  return QWidget::eventFilter(watched, event);
}

void InstanceContainer::navigateWithinInstances(int offset) {
  const int plus = plusIndex();
  if (plus <= 0) return;  // 仅剩 "+"，无可切换的实例
  const int cur = m_tabs->currentIndex();
  if (cur == plus) return;  // 当前停在 "+"（无实例占位），保持不动
  const int target = cur + offset;
  // 仅在实例页签范围 [0, plus) 内移动；越界不动，绝不回绕到 "+"
  if (target >= 0 && target < plus) m_tabs->setCurrentIndex(target);
}

void InstanceContainer::addInstance() {
  QWidget* w = m_factory();
  if (!w) return;
  const QString name = QStringLiteral("%1 %2").arg(m_baseName).arg(++m_counter);
  // 插在 "+" 之前，使 "+" 始终保持在末尾
  m_tabs->insertTab(plusIndex(), w, name);
  m_tabs->setCurrentWidget(w);
  ensurePlusNotClosable();  // 插入后 "+" 仍在末尾，确保其无关闭按钮
}

bool InstanceContainer::closeCurrentInstance() {
  const int idx = m_tabs->currentIndex();
  if (idx < 0 || idx == plusIndex()) return false;
  QWidget* w = m_tabs->widget(idx);
  m_tabs->removeTab(idx);
  delete w;
  relayoutAfterRemove();
  return true;
}

void InstanceContainer::onTabBarClicked(int index) {
  if (index == plusIndex()) addInstance();  // 点击 "+" 即新增实例
}

void InstanceContainer::onTabDoubleClicked(int index) {
  if (index < 0 || index == plusIndex()) return;  // 不重命名 "+"
  bool ok = false;
  const QString old = m_tabs->tabText(index);
  const QString text =
      QInputDialog::getText(this, tr("Rename Instance"), tr("Tab name:"),
                            QLineEdit::Normal, old, &ok);
  if (!ok) return;  // 用户取消
  const QString trimmed = text.trimmed();
  if (trimmed.isEmpty()) return;  // 空名无效，保持原名
  m_tabs->setTabText(index, trimmed);
}

void InstanceContainer::onCloseRequested(int index) {
  if (index == plusIndex()) return;  // 不允许关闭 "+"
  QWidget* w = m_tabs->widget(index);
  m_tabs->removeTab(index);
  delete w;
  relayoutAfterRemove();
}

void InstanceContainer::clearInstances() {
  // 删除全部实例页签，仅保留末尾的 "+" 占位页签；"+" 始终在末尾，
  // 故反复删除第 0 个页签即可。
  while (m_tabs->count() > 1) {
    QWidget* w = m_tabs->widget(0);
    m_tabs->removeTab(0);
    delete w;
  }
  m_counter = 0;
  ensurePlusNotClosable();
}

// 取名字末尾的连续数字（如 "Server 3" → 3）；末尾无数字返回 -1
static int trailingNumber(const QString& s) {
  int i = s.size() - 1;
  while (i >= 0 && s.at(i).isDigit()) --i;
  if (i == s.size() - 1) return -1;  // 末尾无数位
  return s.mid(i + 1).toInt();
}

QJsonObject InstanceContainer::saveState() const {
  QJsonArray arr;
  // 遍历除 "+" 外的所有实例页签
  for (int i = 0; i < m_tabs->count() - 1; ++i) {
    QWidget* w = m_tabs->widget(i);
    QJsonObject inst;
    inst["name"] = m_tabs->tabText(i);
    if (auto* c = dynamic_cast<const Configurable*>(w))
      inst["state"] = c->saveState();
    arr.append(inst);
  }
  QJsonObject o;
  o["instances"] = arr;
  return o;
}

void InstanceContainer::loadState(const QJsonObject& obj) {
  clearInstances();
  const QJsonArray arr = obj.value("instances").toArray();
  int maxTrailing = 0;
  for (const QJsonValue& v : arr) {
    const QJsonObject inst = v.toObject();
    const QString name = inst.value("name").toString();
    QWidget* w = m_factory();
    if (!w) continue;
    // 插在 "+" 之前，使 "+" 始终保持在末尾
    m_tabs->insertTab(plusIndex(), w, name);
    if (auto* c = dynamic_cast<Configurable*>(w))
      c->loadState(inst.value("state").toObject());
    ensurePlusNotClosable();
    // 由已加载名字的末尾数字推算计数器，使后续新增不与已存在名字冲突
    const int n = trailingNumber(name);
    if (n > maxTrailing) maxTrailing = n;
  }
  m_counter = maxTrailing;
  // 有实例则切到第一个，否则停在 "+" 占位
  if (m_tabs->count() > 1)
    m_tabs->setCurrentIndex(0);
  else
    m_tabs->setCurrentIndex(plusIndex());
}
