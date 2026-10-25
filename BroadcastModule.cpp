// _WIN32 由编译器在 Windows 目标下预定义，与包含顺序无关；
// 必须在任何 Qt 头之前包含 winsock2.h，以避免与 windows.h 的符号冲突。
// Linux 下改用 POSIX 套接字头，提供 setsockopt/SOL_SOCKET/SO_BROADCAST 等符号。
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <netinet/in.h>
#include <sys/socket.h>
#endif

#include <QAbstractSocket>
#include <QComboBox>
#include <QHBoxLayout>
#include <QHostAddress>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>

#include "BroadcastModule.h"
#include "DataUtils.h"
#include "ElidedLabel.h"
#include "ScriptBridge.h"

BroadcastModule::BroadcastModule(QWidget* parent) : NetModule(parent) {
  auto* row = new QHBoxLayout;
  row->addWidget(new QLabel(tr("Local IP:")));
  m_localIp = new QComboBox;
  m_localIp->setEditable(true);
  m_localIp->setMinimumWidth(120);
  DataUtils::fillBindIpComboV4(m_localIp);
  row->addWidget(m_localIp);
  row->addWidget(new QLabel(tr("Local port:")));
  m_localPort = new QLineEdit;
  m_localPort->setPlaceholderText(tr("auto"));
  m_localPort->setMaximumWidth(80);
  row->addWidget(m_localPort);
  row->addWidget(new QLabel(tr("Broadcast address:")));
  m_bcastAddr = new QLineEdit(QStringLiteral("255.255.255.255"));
  row->addWidget(m_bcastAddr);
  row->addWidget(new QLabel(tr("Port:")));
  m_targetPort = new QLineEdit(QStringLiteral("8080"));
  m_targetPort->setMaximumWidth(80);
  row->addWidget(m_targetPort);
  m_openBtn = new QPushButton(tr("Open"));
  row->addWidget(m_openBtn);
  row->addStretch();
  configLayout()->addLayout(row);

  configLayout()->addWidget(new ElidedLabel(
      tr("Tip: 255.255.255.255 = limited broadcast. For directed "
         "broadcast use the "
         "subnet broadcast address (e.g. 192.168.1.255). Use Local IP to "
         "pick the "
         "outgoing interface. IPv6 has no broadcast; use the Multicast "
         "tab (ff02::1).")));

  connect(m_openBtn, &QPushButton::clicked, this,
          &BroadcastModule::onOpenClose);
  connect(&m_socket, &QUdpSocket::readyRead, this,
          &BroadcastModule::onReadyRead);
  setSendEnabled(true);
}

void BroadcastModule::enableBroadcast() {
  // Windows 与 Linux 都需对发送套接字开启 SO_BROADCAST，否则向广播地址
  // 发数据报会失败（Qt 5.14 未在枚举中暴露此选项，故调用原生 setsockopt）。
  const qintptr sd = m_socket.socketDescriptor();
  if (sd != -1) {
    int enable = 1;
#ifdef _WIN32
    ::setsockopt((SOCKET)sd, SOL_SOCKET, SO_BROADCAST,
                 reinterpret_cast<const char*>(&enable), sizeof(enable));
#else
    ::setsockopt(int(sd), SOL_SOCKET, SO_BROADCAST, &enable, sizeof(enable));
#endif
  }
}

QHostAddress BroadcastModule::selectedLocalAddr() const {
  // 统一用 DataUtils::selectedBindKey：行编辑文本与当前项文本一致时取其 data，
  // 否则按手输字面量。避免手输非列表内 IP 时 currentData() 仍返回 "any4"
  // 而误绑通配。
  const QString key = DataUtils::selectedBindKey(m_localIp);
  if (key == QStringLiteral("any4")) return QHostAddress(QHostAddress::AnyIPv4);
  return QHostAddress(key);
}

void BroadcastModule::onOpenClose() {
  if (m_socket.state() == QAbstractSocket::BoundState) {
    m_socket.close();
    m_openBtn->setText(tr("Open"));
    m_localPort->setEnabled(true);
    m_localIp->setEnabled(true);
    logInfo(tr("Closed"));
    if (auto* b = scriptBridge(); b && b->isRunning()) b->emitStop();
    return;
  }
  quint16 localP = 0;
  const QString lp = m_localPort->text().trimmed();
  if (!lp.isEmpty()) {
    bool ok = false;
    localP = lp.toUShort(&ok);
    if (!ok || localP == 0) {
      QMessageBox::warning(this, tr("Error"), tr("Invalid local port"));
      return;
    }
  }
  const QHostAddress bindAddr = selectedLocalAddr();
  if (bindAddr.isNull()) {
    QMessageBox::warning(this, tr("Error"), tr("Invalid local IP"));
    return;
  }
  if (!m_socket.bind(bindAddr, localP)) {
    QMessageBox::warning(this, tr("Bind failed"), m_socket.errorString());
    return;
  }
  enableBroadcast();
  m_openBtn->setText(tr("Close"));
  m_localPort->setEnabled(false);
  m_localIp->setEnabled(false);
  logInfo(tr("Bound %1:%2, broadcast enabled")
              .arg(bindAddr == QHostAddress::AnyIPv4 ? tr("any")
                                                     : bindAddr.toString())
              .arg(m_socket.localPort()));
  if (auto* b = scriptBridge(); b && b->isRunning()) b->emitStart();
}

void BroadcastModule::onReadyRead() {
  while (m_socket.hasPendingDatagrams()) {
    QByteArray datagram;
    datagram.resize(int(m_socket.pendingDatagramSize()));
    QHostAddress senderIP;
    quint16 senderPort = 0;
    m_socket.readDatagram(datagram.data(), datagram.size(), &senderIP,
                          &senderPort);
    appendReceived(
        datagram, QStringLiteral("RX"),
        QStringLiteral("%1:%2").arg(senderIP.toString()).arg(senderPort));
  }
}

void BroadcastModule::onSend(const QByteArray& data) {
  bool ok = false;
  const quint16 port = m_targetPort->text().toUShort(&ok);
  if (!ok || port == 0) {
    QMessageBox::warning(this, tr("Error"), tr("Invalid port"));
    return;
  }
  const QHostAddress addr(m_bcastAddr->text());
  if (addr.isNull()) {
    QMessageBox::warning(this, tr("Error"), tr("Invalid broadcast address"));
    return;
  }
  // 若尚未绑定，按当前选择的本地 IP 绑定一个端口以发送（限定地址族）
  if (m_socket.state() != QAbstractSocket::BoundState) {
    if (!m_socket.bind(selectedLocalAddr(), quint16(0))) {
      QMessageBox::warning(this, tr("Bind failed"), m_socket.errorString());
      return;
    }
  }
  enableBroadcast();
  m_socket.writeDatagram(data, addr, port);
}

QJsonObject BroadcastModule::saveState() const {
  QJsonObject o = NetModule::saveState();
  o["localIp"] = m_localIp->currentText();
  o["broadcastAddr"] = m_bcastAddr->text();
  o["targetPort"] = m_targetPort->text();
  o["localPort"] = m_localPort->text();
  return o;
}

void BroadcastModule::loadState(const QJsonObject& o) {
  NetModule::loadState(o);
  // 可编辑下拉框：必须用 setComboCurrentText 同步 currentIndex，
  // 否则 setCurrentText 只改行编辑文本，selectedLocalAddr() 仍读旧项 data。
  if (o.contains("localIp"))
    DataUtils::setComboCurrentText(m_localIp, o.value("localIp").toString());
  if (o.contains("broadcastAddr"))
    m_bcastAddr->setText(o.value("broadcastAddr").toString());
  if (o.contains("targetPort"))
    m_targetPort->setText(o.value("targetPort").toString());
  if (o.contains("localPort"))
    m_localPort->setText(o.value("localPort").toString());
}
