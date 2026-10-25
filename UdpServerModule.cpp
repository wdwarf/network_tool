#include "UdpServerModule.h"

#include <QAbstractSocket>
#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QHostAddress>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTabBar>
#include <QTabWidget>
#include <QVBoxLayout>

#include "DataUtils.h"
#include "ElidedLabel.h"
#include "LogEditor.h"
#include "ScriptBridge.h"
#include "ScriptPanel.h"
#include "UdpPeerTab.h"

UdpServerModule::UdpServerModule(QWidget* parent) : QWidget(parent) {
  auto* topRow = new QHBoxLayout;
  topRow->addWidget(new QLabel(tr("Bind IP:")));
  m_bindIp = new QComboBox;
  m_bindIp->setEditable(true);
  DataUtils::fillBindIpCombo(m_bindIp);
  topRow->addWidget(m_bindIp);
  topRow->addWidget(new QLabel(tr("Port:")));
  m_bindPort = new QLineEdit(QStringLiteral("8080"));
  m_bindPort->setMaximumWidth(80);
  topRow->addWidget(m_bindPort);
  m_bindBtn = new QPushButton(tr("Bind"));
  topRow->addWidget(m_bindBtn);
  // Events 日志自动滚动开关：ON=粘底跟随，OFF=视图静止
  // （LogEditor 默认 autoScroll_=true，且 m_events 在此之后才创建，
  // 故不在此调 setAutoScroll；构造默认已是 ON）
  m_eventsAutoScroll = new QCheckBox(tr("Auto-scroll"));
  m_eventsAutoScroll->setChecked(true);
  topRow->addWidget(m_eventsAutoScroll);
  topRow->addStretch();
  m_status = new ElidedLabel(tr("Not bound"));
  topRow->addWidget(m_status);

  m_tabs = new QTabWidget;
  m_tabs->setTabsClosable(true);
  m_events = new LogEditor;
  m_tabs->addTab(m_events, tr("Events"));
  // Events 与 Script 页签固定不可关闭：清除关闭按钮
  m_tabs->tabBar()->setTabButton(0, QTabBar::RightSide, nullptr);
  m_tabs->tabBar()->setTabButton(0, QTabBar::LeftSide, nullptr);

  m_scriptBridge = new ScriptBridge(this);
  m_scriptPanel = new ScriptPanel(m_scriptBridge, false, this);
  // 占位字符串改为默认代码内容：作为真实可编辑文本填入编辑器。
  m_scriptPanel->setSource(
      tr("def on_receive(data, peer):\n"
         "    if data == b\"PING\":\n"
         "        send(b\"PONG\", peer)\n"
         "    log(f\"recv {len(data)}B from {peer}\")\n"
         "\n"
         "def on_start():\n"
         "    log(\"server bound\")\n"
         "\n"
         "def on_stop():\n"
         "    log(\"server unbound\")"));
  m_tabs->addTab(m_scriptPanel, tr("Script"));
  // Script 页签固定不可关闭：清除关闭按钮
  m_tabs->tabBar()->setTabButton(m_tabs->indexOf(m_scriptPanel),
                                 QTabBar::RightSide, nullptr);
  m_tabs->tabBar()->setTabButton(m_tabs->indexOf(m_scriptPanel),
                                 QTabBar::LeftSide, nullptr);
  connect(m_scriptPanel, &ScriptPanel::sendRequested, this,
          &UdpServerModule::onScriptSend);
  connect(m_scriptPanel, &ScriptPanel::logRequested, this,
          &UdpServerModule::onScriptLog);

  auto* lay = new QVBoxLayout(this);
  lay->addLayout(topRow);
  lay->addWidget(m_tabs, 1);

  connect(m_bindBtn, &QPushButton::clicked, this,
          &UdpServerModule::onBindUnbind);
  connect(&m_socket, &QUdpSocket::readyRead, this,
          &UdpServerModule::onReadyRead);
  // Events 日志自动滚动切换
  connect(m_eventsAutoScroll, &QCheckBox::toggled, this,
          [this](bool on) { m_events->setAutoScroll(on); });
  connect(m_tabs, &QTabWidget::tabCloseRequested, this,
          &UdpServerModule::onTabCloseRequested);
}

QString UdpServerModule::peerKey(const QHostAddress& addr, quint16 port) const {
  // 用 '|' 分隔，避免 IPv6 地址中的冒号造成歧义
  return addr.toString() + QLatin1Char('|') + QString::number(port);
}

void UdpServerModule::logEvent(const QString& msg) {
  m_events->appendLog(LogEditor::LogLevel::Info, msg);
}

void UdpServerModule::onBindUnbind() {
  if (m_socket.state() == QAbstractSocket::BoundState) {
    m_socket.close();
    while (m_tabs->count() > 2) {  // 保留 Events 与 Script
      QWidget* w = m_tabs->widget(m_tabs->count() - 1);
      m_tabs->removeTab(m_tabs->count() - 1);
      delete w;
    }
    m_tabsByPeer.clear();
    m_bindBtn->setText(tr("Bind"));
    m_bindIp->setEnabled(true);
    m_bindPort->setEnabled(true);
    m_status->setText(tr("Not bound"));
    logEvent(tr("Unbound"));
    if (m_scriptBridge && m_scriptBridge->isRunning())
      m_scriptBridge->emitStop();
    return;
  }
  bool ok = false;
  const quint16 port = m_bindPort->text().toUShort(&ok);
  if (!ok || port == 0) {
    QMessageBox::warning(this, tr("Error"), tr("Invalid port"));
    return;
  }
  const QHostAddress addr = DataUtils::selectedBindAddr(m_bindIp);
  if (!m_socket.bind(addr, port)) {
    QMessageBox::warning(this, tr("Bind failed"), m_socket.errorString());
    return;
  }
  m_bindBtn->setText(tr("Unbind"));
  m_bindIp->setEnabled(false);
  m_bindPort->setEnabled(false);
  m_status->setText(
      tr("Bound on %1:%2 | 0 peers").arg(addr.toString()).arg(port));
  logEvent(tr("Bound on %1:%2").arg(addr.toString()).arg(port));
  if (m_scriptBridge && m_scriptBridge->isRunning())
    m_scriptBridge->emitStart();
}

void UdpServerModule::onReadyRead() {
  while (m_socket.hasPendingDatagrams()) {
    QByteArray datagram;
    datagram.resize(int(m_socket.pendingDatagramSize()));
    QHostAddress senderIP;
    quint16 senderPort = 0;
    m_socket.readDatagram(datagram.data(), datagram.size(), &senderIP,
                          &senderPort);

    const QString key = peerKey(senderIP, senderPort);
    UdpPeerTab* tab = m_tabsByPeer.value(key, nullptr);
    if (!tab) {
      tab = new UdpPeerTab(&m_socket, senderIP, senderPort);
      m_tabsByPeer.insert(key, tab);
      m_tabs->addTab(tab, tab->peerText());
      m_tabs->setCurrentWidget(tab);
      logEvent(tr("Peer discovered: %1").arg(tab->peerText()));
      m_status->setText(tr("Bound | %1 peer(s)").arg(m_tabsByPeer.size()));
    }
    tab->deliver(datagram);
    if (m_scriptBridge && m_scriptBridge->isRunning())
      m_scriptBridge->emitRecv(datagram, tab->peerText());
  }
}

void UdpServerModule::onTabCloseRequested(int index) {
  if (index <= 1) return;  // 保护 Events(0) 与 Script(1)
  QWidget* w = m_tabs->widget(index);
  auto* tab = qobject_cast<UdpPeerTab*>(w);
  if (tab) {
    for (auto it = m_tabsByPeer.begin(); it != m_tabsByPeer.end(); ++it) {
      if (it.value() == tab) {
        m_tabsByPeer.erase(it);
        break;
      }
    }
  }
  m_tabs->removeTab(index);
  delete w;
  m_status->setText(tr("Bound | %1 peer(s)").arg(m_tabsByPeer.size()));
}

QJsonObject UdpServerModule::saveState() const {
  QJsonObject o;
  o["bindIp"] = m_bindIp->currentText();
  o["bindPort"] = m_bindPort->text();
  if (m_eventsAutoScroll) o["eventsAutoScroll"] = m_eventsAutoScroll->isChecked();
  if (m_scriptPanel) {
    o["scriptSource"] = m_scriptPanel->source();
    o["scriptEnabled"] = m_scriptPanel->wasEnabled();
  }
  return o;
}

void UdpServerModule::loadState(const QJsonObject& o) {
  // 可编辑下拉框：必须同步 currentIndex，否则 setCurrentText 只改行编辑文本，
  // selectedBindAddr() 仍读旧项 currentData()，保存的绑定 IP 启动时未生效。
  if (o.contains("bindIp"))
    DataUtils::setComboCurrentText(m_bindIp, o.value("bindIp").toString());
  if (o.contains("bindPort"))
    m_bindPort->setText(o.value("bindPort").toString());
  // 恢复 Events 自动滚动：显式同步 LogEditor，不依赖 setChecked 仅在变化时发信号
  if (o.contains("eventsAutoScroll") && m_eventsAutoScroll) {
    const bool on = o.value("eventsAutoScroll").toBool();
    m_eventsAutoScroll->setChecked(on);
    m_events->setAutoScroll(on);
  }
  if (o.contains("scriptSource") && m_scriptPanel)
    m_scriptPanel->setSource(o.value("scriptSource").toString());
  if (o.contains("scriptEnabled") && m_scriptPanel)
    m_scriptPanel->setWasEnabled(o.value("scriptEnabled").toBool());
}

void UdpServerModule::onScriptSend(const QByteArray& data,
                                   const QString& peer) {
  if (peer.isEmpty()) {
    // 广播：发给所有已知对端
    for (auto* tab : m_tabsByPeer) tab->scriptSend(data);
  } else {
    // 定向：按展示名 ip:port 线性查找（对端数量小，避免维护第二张索引表）
    UdpPeerTab* target = nullptr;
    for (auto* tab : m_tabsByPeer)
      if (tab->peerText() == peer) {
        target = tab;
        break;
      }
    if (target)
      target->scriptSend(data);
    else
      logEvent(tr("[script] no peer %1").arg(peer));
  }
}

void UdpServerModule::onScriptLog(const QString& msg) {
  logEvent(QStringLiteral("[script] ") + msg);
}
