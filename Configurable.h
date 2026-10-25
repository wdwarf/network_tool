#ifndef CONFIGURABLE_H
#define CONFIGURABLE_H

#include <QJsonObject>

// 可配置接口：模块实现 saveState/loadState 以支持界面配置的保存与加载。
// InstanceContainer 与 MainWindow 通过此接口统一序列化各类模块
// （NetModule 派生类与 QWidget 直接派生的服务端模块基类不同，故用接口统一）。
class Configurable {
 public:
  virtual ~Configurable() = default;

  // 导出本实例的可持久化界面状态（输入框值、显示模式等，不含运行态连接/日志）
  virtual QJsonObject saveState() const = 0;

  // 从保存的状态恢复界面字段（不自动启动监听/连接，仅恢复可见输入）
  virtual void loadState(const QJsonObject& obj) = 0;
};

#endif  // CONFIGURABLE_H
