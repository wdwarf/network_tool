#ifndef INSTANCECONTAINER_H
#define INSTANCECONTAINER_H

#include <QJsonObject>
#include <QWidget>
#include <functional>

#include "Configurable.h"

class QTabWidget;
class QEvent;

// 按类别承载多个模块实例的容器。
// 用一个常驻的 "+" 页签作为“添加”入口：点击它即新增一个实例页签。
// "+" 页签始终位于末尾且不可关闭，因此页签栏永不为空——避免 0 页签时
// 标签栏高度塌缩、Add 入口随之消失的问题。实例名按 “<基名> 1 / 2 / ...” 自增；
// 关闭实例即销毁其 widget（其成员 socket 随之析构，监听/连接自动断开）。
// "+" 虽是真实页签（占一个索引），但滚轮与左右方向键切换被拦截，仅在
// 实例页签范围内移动，绝不落到 "+"；点击 "+" 仍照常新增实例。
class InstanceContainer : public QWidget, public Configurable {
  Q_OBJECT
 public:
  // 工厂：创建一个该类别的模块实例（无父对象，由页签接管归属）
  using Factory = std::function<QWidget*()>;

  InstanceContainer(const QString& baseName, Factory factory,
                    QWidget* parent = nullptr);

  // 新增一个实例页签（供主窗口菜单/工具栏调用）
  void addInstance();
  // 关闭当前实例页签；当前无实例（仅剩 "+"）时返回 false
  bool closeCurrentInstance();

  // 序列化：导出所有实例页签的名字与其模块状态（不含 "+" 占位页签）
  QJsonObject saveState() const override;
  // 反序列化：清空现有实例，按保存的顺序与名字重建并恢复各模块字段
  void loadState(const QJsonObject& obj) override;

 private slots:
  void onTabBarClicked(int index);
  void onTabDoubleClicked(int index);
  void onCloseRequested(int index);

 protected:
  // 拦截页签栏的滚轮与左右方向键：切换仅在实例页签范围内进行，
  // 不让 QTabBar 默认导航落到常驻 "+" 占位页签（点击 "+" 仍正常加实例）。
  bool eventFilter(QObject* watched, QEvent* event) override;

 private:
  int plusIndex() const;         // "+" 页签始终在最后
  void ensurePlusNotClosable();  // 确保 "+" 页签不显示关闭按钮
  void relayoutAfterRemove();    // 移除页签后：重申 "+" 无关闭按钮，
                                 // 并在当前落到 "+" 时切回末尾实例
  void clearInstances();         // 删除全部实例页签（保留 "+"），重置计数器
  // 按方向 offset(±1) 切换到相邻实例页签；越界不动、不回绕，绝不落到 "+"
  void navigateWithinInstances(int offset);

  QString m_baseName;
  Factory m_factory;
  QTabWidget* m_tabs = nullptr;
  QWidget* m_placeholder = nullptr;  // "+" 页签内容（空状态提示）
  int m_counter = 0;
};

#endif  // INSTANCECONTAINER_H
