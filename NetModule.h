#ifndef NETMODULE_H
#define NETMODULE_H

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QVector>
#include <QWidget>

#include "Configurable.h"

class QPlainTextEdit;
class QPushButton;
class QRadioButton;
class QButtonGroup;
class QCheckBox;
class QVBoxLayout;
class QSpinBox;
class QTimer;
class QTabWidget;
class ScriptBridge;
class ScriptPanel;
class LogEditor;
class SciFindBar;

// 一条接收/发送记录
struct ReceivedEntry {
  QByteArray data;
  QString direction;  // "RX" / "TX" / "INFO"
  QString peer;       // 对端 "ip:port" 或信息文本
  QString timestamp;  // 预格式化时间
  bool isInfo = false;
};

// 各网络模式页面的基类：提供公共的"接收区 + 发送区 + 文本/十六进制切换"。
// 子类在构造函数中向 configLayout() 添加自己的配置控件，
// 并重写 onSend() 实现真正的发送。
class NetModule : public QWidget, public Configurable {
  Q_OBJECT
 public:
  explicit NetModule(QWidget* parent = nullptr, bool enableScript = true);

  // 导出/恢复公共显示与发送区状态；子类重写时应先调用基类版本再追加自身字段
  QJsonObject saveState() const override;
  void loadState(const QJsonObject& obj) override;

 protected:
  // 把收到的数据追加到接收区。direction: "RX"/"TX"; peer: "ip:port"
  void appendReceived(const QByteArray& data, const QString& direction,
                      const QString& peer);

  // 追加一条状态信息
  void logInfo(const QString& message);

  // 设置发送按钮可用状态
  void setSendEnabled(bool enabled);

  // 子类重写：真正发送已解析好的数据
  virtual void onSend(const QByteArray& data) { Q_UNUSED(data); }

  // 子类可重写：清空接收区时额外清理（默认无操作）
  virtual void onClearReceived() {}

  // 连接事件：供 TCP 子类在连接建立/断开处调用，转发给脚本桥接器。
  // 无连接语义的模块（UDP/Broadcast/Multicast）不调用，故不触发。
  void notifyConnected(const QString& peer);
  void notifyDisconnected(const QString& peer);
  ScriptBridge* scriptBridge() const { return m_scriptBridge; }

  // 配置区布局，子类向其添加控件
  QVBoxLayout* configLayout() const;

 protected:
  LogEditor* m_receiveEdit = nullptr;
  QPlainTextEdit* m_sendEdit = nullptr;
  QPushButton* m_sendBtn = nullptr;

  QSpinBox* m_countSpin = nullptr;     // -1 = 无限（显示 ∞）
  QSpinBox* m_intervalSpin = nullptr;  // 单位 ms
  QTimer* m_timer = nullptr;           // 单次定时器，重排驱动循环
  bool m_sending = false;
  QByteArray m_loopData;  // 整轮固定载荷
  bool m_infinite = false;
  int m_remaining = 0;  // 首次之后还剩多少次

 private slots:
  void sendClicked();
  void clearReceivedClicked();
  void rerenderReceived();
  void onLoopTick();

 private:
  QString renderEntry(const ReceivedEntry& e) const;
  QString currentTimeStr() const;

  QVBoxLayout* m_configLayout = nullptr;
  QTabWidget* m_tabs = nullptr;  // 单连接模块：Data/Script 两页签容器
  QRadioButton* m_recvText = nullptr;
  QRadioButton* m_recvHex = nullptr;
  QRadioButton* m_sendText = nullptr;
  QRadioButton* m_sendHex = nullptr;
  QButtonGroup* m_recvGroup = nullptr;  // 接收区 Text/Hex 互斥组
  QButtonGroup* m_sendGroup = nullptr;  // 发送区 Text/Hex 互斥组
  QCheckBox* m_showTimestamp = nullptr;
  QCheckBox* m_showTx = nullptr;
  QCheckBox* m_autoScroll = nullptr;  // 接收区自动滚动开关（粘底：仅在底部时跟随）
  SciFindBar* m_recvFindBar = nullptr;  // 接收区查找条（findOnly）

  QVector<ReceivedEntry> m_entries;

  void startLoop(const QByteArray& data, int count, int interval);
  void doOneSend();
  void stopLoop();

  static const int kMaxEntries = 5000;

  ScriptBridge* m_scriptBridge = nullptr;
  ScriptPanel* m_scriptPanel = nullptr;
  bool m_enableScript = true;
};

#endif  // NETMODULE_H
