#include "UdpClientModule.h"

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

UdpClientModule::UdpClientModule(QWidget* parent) : NetModule(parent) {
  auto* row = new QHBoxLayout;
  row->addWidget(new QLabel(tr("Target IP:")));
  m_targetIp = new QComboBox;
  m_targetIp->setEditable(true);
  m_targetIp->setMinimumWidth(140);
  DataUtils::fillTargetIpCombo(m_targetIp, QStringLiteral("127.0.0.1"));
  row->addWidget(m_targetIp);
  row->addWidget(new QLabel(tr("Target port:")));
  m_targetPort = new QLineEdit(QStringLiteral("8080"));
  m_targetPort->setMaximumWidth(80);
  row->addWidget(m_targetPort);
  row->addWidget(new QLabel(tr("Local port:")));
  m_localPort = new QLineEdit;
  m_localPort->setPlaceholderText(tr("auto"));
  m_localPort->setMaximumWidth(80);
  row->addWidget(m_localPort);
  m_openBtn = new QPushButton(tr("Open"));
  row->addWidget(m_openBtn);
  row->addStretch();
  configLayout()->addLayout(row);

  configLayout()->addWidget(new ElidedLabel(
      tr("Open binds a local port so you can receive replies. "
         "Sending works without Open (auto-allocates a source port).")));

  connect(m_openBtn, &QPushButton::clicked, this,
          &UdpClientModule::onOpenClose);
  connect(&m_socket, &QUdpSocket::readyRead, this,
          &UdpClientModule::onReadyRead);
  setSendEnabled(true);
}

void UdpClientModule::onOpenClose() {
  if (m_socket.state() == QAbstractSocket::BoundState) {
    m_socket.close();
    m_openBtn->setText(tr("Open"));
    m_localPort->setEnabled(true);
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
  if (!m_socket.bind(QHostAddress(QHostAddress::Any), localP)) {
    QMessageBox::warning(this, tr("Bind failed"), m_socket.errorString());
    return;
  }
  m_openBtn->setText(tr("Close"));
  m_localPort->setEnabled(false);
  logInfo(tr("Bound local port %1").arg(m_socket.localPort()));
  if (auto* b = scriptBridge(); b && b->isRunning()) b->emitStart();
}

void UdpClientModule::onReadyRead() {
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

void UdpClientModule::onSend(const QByteArray& data) {
  bool ok = false;
  const quint16 port = m_targetPort->text().toUShort(&ok);
  if (!ok || port == 0) {
    QMessageBox::warning(this, tr("Error"), tr("Invalid target port"));
    return;
  }
  const QHostAddress addr(m_targetIp->currentText());
  if (addr.isNull()) {
    QMessageBox::warning(this, tr("Error"), tr("Invalid target IP"));
    return;
  }
  m_socket.writeDatagram(data, addr, port);
}

QJsonObject UdpClientModule::saveState() const {
  QJsonObject o = NetModule::saveState();
  o["targetIp"] = m_targetIp->currentText();
  o["targetPort"] = m_targetPort->text();
  o["localPort"] = m_localPort->text();
  return o;
}

void UdpClientModule::loadState(const QJsonObject& o) {
  NetModule::loadState(o);
  if (o.contains("targetIp"))
    m_targetIp->setCurrentText(o.value("targetIp").toString());
  if (o.contains("targetPort"))
    m_targetPort->setText(o.value("targetPort").toString());
  if (o.contains("localPort"))
    m_localPort->setText(o.value("localPort").toString());
}
