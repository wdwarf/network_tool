#ifndef UDPSERVERMODULE_H
#define UDPSERVERMODULE_H

#include <QHash>
#include <QJsonObject>
#include <QUdpSocket>
#include <QWidget>

#include "Configurable.h"

class QCheckBox;
class QComboBox;
class QLineEdit;
class QPushButton;
class QTabWidget;
class ElidedLabel;
class UdpPeerTab;
class ScriptBridge;
class ScriptPanel;
class LogEditor;

// UDP 服务端：绑定本地端口，为每个对端(ip:port)创建独立页签（含 Events 与
// Script 页）。 全部对端共享一个服务器级脚本（send(data, peer) 定向、省略 peer
// 广播）。
class UdpServerModule : public QWidget, public Configurable {
  Q_OBJECT
 public:
  explicit UdpServerModule(QWidget* parent = nullptr);

  QJsonObject saveState() const override;
  void loadState(const QJsonObject& obj) override;

 private slots:
  void onBindUnbind();
  void onReadyRead();
  void onTabCloseRequested(int index);
  void onScriptSend(const QByteArray& data, const QString& peer);
  void onScriptLog(const QString& msg);

 private:
  void logEvent(const QString& msg);
  QString peerKey(const QHostAddress& addr, quint16 port) const;

  QComboBox* m_bindIp = nullptr;
  QLineEdit* m_bindPort = nullptr;
  QPushButton* m_bindBtn = nullptr;
  QCheckBox* m_eventsAutoScroll = nullptr;  // Events 日志自动滚动开关
  ElidedLabel* m_status = nullptr;
  QTabWidget* m_tabs = nullptr;
  LogEditor* m_events = nullptr;

  ScriptBridge* m_scriptBridge = nullptr;
  ScriptPanel* m_scriptPanel = nullptr;

  QUdpSocket m_socket;
  QHash<QString, UdpPeerTab*> m_tabsByPeer;
};

#endif  // UDPSERVERMODULE_H
