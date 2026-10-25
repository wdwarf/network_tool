#ifndef TCPCONNECTIONTAB_H
#define TCPCONNECTIONTAB_H

#include <QTcpSocket>

#include "NetModule.h"

class ElidedLabel;
class QPushButton;

// TCP 服务端单个客户端连接的页签：拥有并管理一个 QTcpSocket，
// 提供该连接独立的收发区与文本/十六进制切换。
class TcpConnectionTab : public NetModule {
  Q_OBJECT
 public:
  explicit TcpConnectionTab(QTcpSocket* socket, QWidget* parent = nullptr);
  ~TcpConnectionTab() override;

  QString peerText() const { return m_peer; }
  bool isOnline() const { return m_online; }
  // 暴露底层套接字，供服务端对在线连接即时应用保活等选项
  QTcpSocket* socket() const { return m_socket; }

  // 相同地址:端口的客户端重连时由服务端调用：接管新 socket，复用本页签。
  void reconnect(QTcpSocket* socket);

  // 供服务器单脚本定向/广播发送调用：仅当在线且已连接时写 socket。
  void scriptSend(const QByteArray& data);

 signals:
  void closed(TcpConnectionTab* tab);
  void dataReceived(const QByteArray& data, const QString& peer);
  void peerDisconnected(const QString& peer);

 protected:
  void onSend(const QByteArray& data) override;

 private slots:
  void onReadyRead();
  void onDisconnected();
  void onDisconnectClicked();

 private:
  QTcpSocket* m_socket = nullptr;
  QString m_peer;
  bool m_online = true;
  ElidedLabel* m_peerLabel = nullptr;
  QPushButton* m_discBtn = nullptr;
};

#endif  // TCPCONNECTIONTAB_H
