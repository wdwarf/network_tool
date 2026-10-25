#ifndef TCPSERVERMODULE_H
#define TCPSERVERMODULE_H

#include <QHash>
#include <QJsonObject>
#include <QTcpServer>
#include <QWidget>

#include "Configurable.h"

class QComboBox;
class QLineEdit;
class QPushButton;
class QCheckBox;
class QTabWidget;
class ElidedLabel;
class QTcpSocket;
class TcpConnectionTab;
class ScriptBridge;
class ScriptPanel;
class LogEditor;

// TCP 服务端：监听端口，为每个接入的客户端创建独立页签（含 Events 事件页与
// Script 脚本页）。 全部连接共享一个服务器级脚本（send(data, peer) 定向、省略
// peer 广播）。
class TcpServerModule : public QWidget, public Configurable {
  Q_OBJECT
 public:
  explicit TcpServerModule(QWidget* parent = nullptr);

  QJsonObject saveState() const override;
  void loadState(const QJsonObject& obj) override;

 private slots:
  void onStartStop();
  void onNewConnection();
  void onTabCloseRequested(int index);
  void onConnectionClosed(TcpConnectionTab* tab);
  void onScriptSend(const QByteArray& data, const QString& peer);
  void onScriptLog(const QString& msg);
  void onKeepAliveToggled(bool checked);

 private:
  void logEvent(const QString& msg);
  // 对已接受的连接套接字应用/取消保活（enable=true 开启 SO_KEEPALIVE，false
  // 关闭）
  void applyKeepAlive(QTcpSocket* s, bool enable) const;

  QComboBox* m_listenIp = nullptr;
  QLineEdit* m_listenPort = nullptr;
  QCheckBox* m_keepalive = nullptr;  // TCP 保活开关（作用于每个接入连接）
  QCheckBox* m_eventsAutoScroll = nullptr;  // Events 日志自动滚动开关
  QPushButton* m_startBtn = nullptr;
  ElidedLabel* m_status = nullptr;
  QTabWidget* m_tabs = nullptr;
  LogEditor* m_events = nullptr;

  ScriptBridge* m_scriptBridge = nullptr;
  ScriptPanel* m_scriptPanel = nullptr;

  QTcpServer m_server;
  QHash<QTcpSocket*, TcpConnectionTab*> m_tabBySocket;
  QHash<QString, TcpConnectionTab*> m_tabByPeer;
};

#endif  // TCPSERVERMODULE_H
