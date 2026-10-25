#ifndef UDPPEERTAB_H
#define UDPPEERTAB_H

#include <QHostAddress>

#include "NetModule.h"

class QUdpSocket;
class ElidedLabel;

// UDP 服务端单个对端的页签：持有共享 QUdpSocket 指针与对端地址。
// 收到的数据由服务端经 deliver() 推送进来；发送时向该对端写数据报。
class UdpPeerTab : public NetModule {
  Q_OBJECT
 public:
  UdpPeerTab(QUdpSocket* sock, const QHostAddress& peer, quint16 port,
             QWidget* parent = nullptr);

  QString peerText() const { return m_peer; }

  // 服务端收到数据报时调用，把数据投递到本页签接收区
  void deliver(const QByteArray& data);

  // 供服务器单脚本定向/广播发送调用：向本对端写数据报。
  void scriptSend(const QByteArray& data);

 protected:
  void onSend(const QByteArray& data) override;

 private:
  QUdpSocket* m_sock = nullptr;
  QHostAddress m_peerAddr;
  quint16 m_peerPort = 0;
  QString m_peer;
  ElidedLabel* m_peerLabel = nullptr;
};

#endif  // UDPPEERTAB_H
