#include "UdpPeerTab.h"

#include <QHBoxLayout>
#include <QUdpSocket>

#include "ElidedLabel.h"

UdpPeerTab::UdpPeerTab(QUdpSocket* sock, const QHostAddress& peer, quint16 port,
                       QWidget* parent)
    : NetModule(parent, false),
      m_sock(sock),
      m_peerAddr(peer),
      m_peerPort(port) {
  m_peer = QStringLiteral("%1:%2").arg(peer.toString()).arg(port);
  auto* bar = new QHBoxLayout;
  m_peerLabel = new ElidedLabel(tr("Peer: %1").arg(m_peer));
  bar->addWidget(m_peerLabel);
  bar->addStretch();
  configLayout()->addLayout(bar);
  logInfo(tr("Peer discovered: %1").arg(m_peer));
  setSendEnabled(true);
}

void UdpPeerTab::deliver(const QByteArray& data) {
  appendReceived(data, QStringLiteral("RX"), m_peer);
}

void UdpPeerTab::onSend(const QByteArray& data) {
  if (m_sock) m_sock->writeDatagram(data, m_peerAddr, m_peerPort);
}

void UdpPeerTab::scriptSend(const QByteArray& data) {
  if (m_sock) m_sock->writeDatagram(data, m_peerAddr, m_peerPort);
}
