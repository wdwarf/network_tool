#include "TcpConnectionTab.h"

#include <QAbstractSocket>
#include <QHBoxLayout>
#include <QHostAddress>
#include <QPushButton>

#include "ElidedLabel.h"

TcpConnectionTab::TcpConnectionTab(QTcpSocket* socket, QWidget* parent)
    : NetModule(parent, false), m_socket(socket) {
  m_socket->setParent(this);  // 接管 socket 所有权
  m_peer = QStringLiteral("%1:%2")
               .arg(m_socket->peerAddress().toString())
               .arg(m_socket->peerPort());

  auto* bar = new QHBoxLayout;
  m_peerLabel = new ElidedLabel(tr("Peer: %1").arg(m_peer));
  bar->addWidget(m_peerLabel);
  bar->addStretch();
  m_discBtn = new QPushButton(tr("Disconnect"));
  bar->addWidget(m_discBtn);
  configLayout()->addLayout(bar);

  connect(m_socket, &QTcpSocket::readyRead, this,
          &TcpConnectionTab::onReadyRead);
  connect(m_socket, &QTcpSocket::disconnected, this,
          &TcpConnectionTab::onDisconnected);
  connect(m_discBtn, &QPushButton::clicked, this,
          &TcpConnectionTab::onDisconnectClicked);

  logInfo(tr("Connected from %1").arg(m_peer));
  setSendEnabled(true);
}

TcpConnectionTab::~TcpConnectionTab() {
  if (m_socket)
    m_socket->disconnect(this);  // 析构期间禁止 socket 信号回调本对象
  // m_socket 为子对象，随本页签析构被销毁（同时关闭连接）
}

void TcpConnectionTab::reconnect(QTcpSocket* socket) {
  if (m_socket) {
    // 旧 socket 若仍连接，手动补发一次 disconnect 事件：
    // 直接 delete 不会触发 disconnected 信号，脚本会漏掉 on_disconnect。
    if (m_socket->state() != QAbstractSocket::UnconnectedState)
      emit peerDisconnected(m_peer);
    m_socket->disconnect(this);  // 断开旧 socket 与本对象的信号连接
    delete m_socket;             // 旧连接已断开，安全销毁旧 socket
  }
  m_socket = socket;
  m_socket->setParent(this);  // 接管新 socket 所有权
  m_peer = QStringLiteral("%1:%2")
               .arg(m_socket->peerAddress().toString())
               .arg(m_socket->peerPort());

  connect(m_socket, &QTcpSocket::readyRead, this,
          &TcpConnectionTab::onReadyRead);
  connect(m_socket, &QTcpSocket::disconnected, this,
          &TcpConnectionTab::onDisconnected);

  m_online = true;
  setSendEnabled(true);
  if (m_discBtn) m_discBtn->setEnabled(true);
  m_peerLabel->setText(tr("Peer: %1").arg(m_peer));
  logInfo(tr("Reconnected from %1").arg(m_peer));

  // 连接已建立但缓冲区可能已有数据（客户端连上即发送），补读一次
  if (m_socket->bytesAvailable()) onReadyRead();
}

void TcpConnectionTab::onReadyRead() {
  const QByteArray data = m_socket->readAll();
  if (!data.isEmpty()) {
    appendReceived(data, QStringLiteral("RX"), m_peer);
    emit dataReceived(data, m_peer);
  }
}

void TcpConnectionTab::onDisconnected() {
  m_online = false;
  setSendEnabled(false);
  m_discBtn->setEnabled(false);
  logInfo(tr("Disconnected by peer"));
  emit peerDisconnected(m_peer);
  emit closed(this);
}

void TcpConnectionTab::onDisconnectClicked() {
  if (m_socket && m_socket->state() != QAbstractSocket::UnconnectedState)
    m_socket->disconnectFromHost();
}

void TcpConnectionTab::onSend(const QByteArray& data) {
  if (m_online && m_socket &&
      m_socket->state() == QAbstractSocket::ConnectedState)
    m_socket->write(data);
}

void TcpConnectionTab::scriptSend(const QByteArray& data) {
  if (m_online && m_socket &&
      m_socket->state() == QAbstractSocket::ConnectedState)
    m_socket->write(data);
}
