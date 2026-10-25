#include "MulticastModule.h"

#include <QAbstractSocket>
#include <QComboBox>
#include <QHBoxLayout>
#include <QHostAddress>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>

#include "DataUtils.h"
#include "ElidedLabel.h"
#include "ScriptBridge.h"

MulticastModule::MulticastModule(QWidget* parent) : NetModule(parent) {
  auto* row = new QHBoxLayout;
  row->addWidget(new QLabel(tr("Group:")));
  m_groupIp = new QComboBox;
  m_groupIp->setEditable(true);
  m_groupIp->addItem(QStringLiteral("239.0.0.1"));
  m_groupIp->addItem(QStringLiteral("224.0.0.1"));
  m_groupIp->addItem(QStringLiteral("ff02::1"));
  m_groupIp->addItem(QStringLiteral("ff02::2"));
  row->addWidget(m_groupIp);
  row->addWidget(new QLabel(tr("Port:")));
  m_groupPort = new QLineEdit(QStringLiteral("8080"));
  m_groupPort->setMaximumWidth(80);
  row->addWidget(m_groupPort);
  row->addWidget(new QLabel(tr("Local IP:")));
  m_localIp = new QComboBox;
  m_localIp->setEditable(true);
  m_localIp->setMinimumWidth(120);
  DataUtils::fillBindIpCombo(m_localIp);
  row->addWidget(m_localIp);
  row->addWidget(new QLabel(tr("TTL:")));
  m_ttl = new QLineEdit(QStringLiteral("1"));
  m_ttl->setMaximumWidth(40);
  row->addWidget(m_ttl);
  m_joinBtn = new QPushButton(tr("Join"));
  row->addWidget(m_joinBtn);
  row->addStretch();
  configLayout()->addLayout(row);

  configLayout()->addWidget(new ElidedLabel(
      tr("Tip: IPv4 groups are 224.0.0.0/4 (e.g. 239.0.0.1). IPv6 groups are "
         "ff00::/8 (e.g. ff02::1 = all nodes). TTL=1 stays on the local link. "
         "Local IP chooses the interface to join/send on.")));

  connect(m_joinBtn, &QPushButton::clicked, this,
          &MulticastModule::onJoinLeave);
  connect(&m_socket, &QUdpSocket::readyRead, this,
          &MulticastModule::onReadyRead);
  setSendEnabled(true);
}

QHostAddress MulticastModule::bindAddrForGroup(
    const QHostAddress& group, QNetworkInterface* ifaceOut) const {
  // 取下拉框当前选择的本地地址（"any4"/"any6"/"any" 或具体 IP）
  const QString key = DataUtils::selectedBindKey(m_localIp);

  // 若选了具体本地地址：绑定到该地址，并定位其网卡用于按接口加入组播
  if (key != QStringLiteral("any4") && key != QStringLiteral("any6") &&
      key != QStringLiteral("any")) {
    const QHostAddress specific(key);
    if (ifaceOut) *ifaceOut = DataUtils::interfaceForAddress(specific);
    return (group.protocol() == QAbstractSocket::IPv4Protocol)
               ? (specific.protocol() == QAbstractSocket::IPv4Protocol
                      ? specific
                      : QHostAddress(QHostAddress::AnyIPv4))
               : (specific.protocol() == QAbstractSocket::IPv6Protocol
                      ? specific
                      : QHostAddress(QHostAddress::AnyIPv6));
  }

  // 通配项：按组地址族绑定，网卡交由系统默认
  if (ifaceOut) *ifaceOut = QNetworkInterface();
  return (group.protocol() == QAbstractSocket::IPv4Protocol)
             ? QHostAddress(QHostAddress::AnyIPv4)
             : QHostAddress(QHostAddress::AnyIPv6);
}

void MulticastModule::onJoinLeave() {
  if (m_socket.state() == QAbstractSocket::BoundState) {
    if (!m_group.isNull()) m_socket.leaveMulticastGroup(m_group);
    m_socket.close();
    m_joinBtn->setText(tr("Join"));
    m_groupIp->setEnabled(true);
    m_groupPort->setEnabled(true);
    m_ttl->setEnabled(true);
    m_localIp->setEnabled(true);
    logInfo(tr("Left group"));
    if (auto* b = scriptBridge(); b && b->isRunning()) b->emitStop();
    return;
  }

  bool ok = false;
  const quint16 port = m_groupPort->text().toUShort(&ok);
  if (!ok || port == 0) {
    QMessageBox::warning(this, tr("Error"), tr("Invalid port"));
    return;
  }
  const QHostAddress group(m_groupIp->currentText());
  if (group.isNull() || (group.protocol() != QAbstractSocket::IPv4Protocol &&
                         group.protocol() != QAbstractSocket::IPv6Protocol)) {
    QMessageBox::warning(this, tr("Error"), tr("Invalid group address"));
    return;
  }

  QNetworkInterface iface;
  const QHostAddress bindAddr = bindAddrForGroup(group, &iface);
  if (!m_socket.bind(bindAddr, port)) {
    QMessageBox::warning(this, tr("Bind failed"), m_socket.errorString());
    return;
  }

  bool ttlOk = false;
  const int ttlVal = m_ttl->text().toInt(&ttlOk);
  if (ttlOk && ttlVal > 0)
    m_socket.setSocketOption(QAbstractSocket::MulticastTtlOption, ttlVal);

  // 指定了具体本地 IP 时按该网卡加入；否则交由系统默认接口
  const bool joined = iface.isValid()
                          ? m_socket.joinMulticastGroup(group, iface)
                          : m_socket.joinMulticastGroup(group);
  if (!joined) {
    QMessageBox::warning(this, tr("Join failed"), m_socket.errorString());
    m_socket.close();
    return;
  }
  m_group = group;
  m_joinBtn->setText(tr("Leave"));
  m_groupIp->setEnabled(false);
  m_groupPort->setEnabled(false);
  m_ttl->setEnabled(false);
  m_localIp->setEnabled(false);
  logInfo(tr("Joined group %1 on port %2").arg(group.toString()).arg(port));
  if (auto* b = scriptBridge(); b && b->isRunning()) b->emitStart();
}

void MulticastModule::onReadyRead() {
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

void MulticastModule::onSend(const QByteArray& data) {
  if (m_group.isNull()) {
    logInfo(tr("Join a group first"));
    return;
  }
  bool ok = false;
  const quint16 port = m_groupPort->text().toUShort(&ok);
  if (!ok || port == 0) {
    QMessageBox::warning(this, tr("Error"), tr("Invalid port"));
    return;
  }
  m_socket.writeDatagram(data, m_group, port);
}

QJsonObject MulticastModule::saveState() const {
  QJsonObject o = NetModule::saveState();
  o["group"] = m_groupIp->currentText();
  o["groupPort"] = m_groupPort->text();
  o["ttl"] = m_ttl->text();
  if (m_localIp) o["localIp"] = m_localIp->currentText();
  return o;
}

void MulticastModule::loadState(const QJsonObject& o) {
  NetModule::loadState(o);
  // 组地址为可编辑下拉框：setCurrentText 命中已有项则选中，否则填入行编辑
  if (o.contains("group"))
    m_groupIp->setCurrentText(o.value("group").toString());
  if (o.contains("groupPort"))
    m_groupPort->setText(o.value("groupPort").toString());
  if (o.contains("ttl")) m_ttl->setText(o.value("ttl").toString());
  if (o.contains("localIp") && m_localIp)
    // 可编辑下拉框：必须同步 currentIndex，否则 setCurrentText 只改行编辑文本，
    // bindAddrForGroup() 仍读旧项 currentData()，导致保存的本地 IP 未生效。
    DataUtils::setComboCurrentText(m_localIp, o.value("localIp").toString());
}
