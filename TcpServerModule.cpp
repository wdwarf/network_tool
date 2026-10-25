// _WIN32 由编译器在 Windows 目标下预定义；必须在任何 Qt 头之前包含 winsock2.h，
// 以避免与 windows.h 的符号冲突。Linux 下改用 POSIX 套接字头，提供 setsockopt/
// SOL_SOCKET/SO_KEEPALIVE 等符号（Windows 分支由 winsock2.h 提供）。
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#endif

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
#include "TcpConnectionTab.h"
#include "TcpServerModule.h"

TcpServerModule::TcpServerModule(QWidget* parent) : QWidget(parent) {
  auto* topRow = new QHBoxLayout;
  topRow->addWidget(new QLabel(tr("Listen IP:")));
  m_listenIp = new QComboBox;
  m_listenIp->setEditable(true);
  DataUtils::fillBindIpCombo(m_listenIp);
  topRow->addWidget(m_listenIp);
  topRow->addWidget(new QLabel(tr("Port:")));
  m_listenPort = new QLineEdit(QStringLiteral("8080"));
  m_listenPort->setMaximumWidth(80);
  topRow->addWidget(m_listenPort);
  m_startBtn = new QPushButton(tr("Start"));
  topRow->addWidget(m_startBtn);
  // 保活开关：开启后对每个接入的连接套接字设置 SO_KEEPALIVE，
  // 用于探测客户端是否已掉线（默认关闭，避免无谓探测流量）。
  m_keepalive = new QCheckBox(tr("Keep-Alive"));
  topRow->addWidget(m_keepalive);
  // Events 日志自动滚动开关：ON=粘底跟随，OFF=视图静止
  // （LogEditor 默认 autoScroll_=true，且 m_events 在此之后才创建，
  // 故不在此调 setAutoScroll；构造默认已是 ON）
  m_eventsAutoScroll = new QCheckBox(tr("Auto-scroll"));
  m_eventsAutoScroll->setChecked(true);
  topRow->addWidget(m_eventsAutoScroll);
  topRow->addStretch();
  m_status = new ElidedLabel(tr("Not listening"));
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
  // 占位字符串改为默认代码内容：作为真实可编辑文本填入编辑器（含连接/断开回调示例）。
  m_scriptPanel->setSource(
      tr("def on_receive(data, peer):\n"
         "    if data == b\"PING\":\n"
         "        send(b\"PONG\", peer)\n"
         "    log(f\"recv {len(data)}B from {peer}\")\n"
         "\n"
         "def on_connect(peer):\n"
         "    log(f\"connected {peer}\")\n"
         "\n"
         "def on_disconnect(peer):\n"
         "    log(f\"gone {peer}\")\n"
         "\n"
         "def on_start():\n"
         "    log(\"server started\")\n"
         "\n"
         "def on_stop():\n"
         "    log(\"server stopped\")"));
  m_tabs->addTab(m_scriptPanel, tr("Script"));
  // Script 页签固定不可关闭：清除关闭按钮
  m_tabs->tabBar()->setTabButton(m_tabs->indexOf(m_scriptPanel),
                                 QTabBar::RightSide, nullptr);
  m_tabs->tabBar()->setTabButton(m_tabs->indexOf(m_scriptPanel),
                                 QTabBar::LeftSide, nullptr);
  connect(m_scriptPanel, &ScriptPanel::sendRequested, this,
          &TcpServerModule::onScriptSend);
  connect(m_scriptPanel, &ScriptPanel::logRequested, this,
          &TcpServerModule::onScriptLog);

  auto* lay = new QVBoxLayout(this);
  lay->addLayout(topRow);
  lay->addWidget(m_tabs, 1);

  connect(m_startBtn, &QPushButton::clicked, this,
          &TcpServerModule::onStartStop);
  connect(&m_server, &QTcpServer::newConnection, this,
          &TcpServerModule::onNewConnection);
  connect(m_tabs, &QTabWidget::tabCloseRequested, this,
          &TcpServerModule::onTabCloseRequested);
  // 保活开关切换时立即作用于所有在线连接（开启时探测、关闭时恢复默认）
  connect(m_keepalive, &QCheckBox::toggled, this,
          &TcpServerModule::onKeepAliveToggled);
  // Events 日志自动滚动切换
  connect(m_eventsAutoScroll, &QCheckBox::toggled, this,
          [this](bool on) { m_events->setAutoScroll(on); });
}

void TcpServerModule::logEvent(const QString& msg) {
  m_events->appendLog(LogEditor::LogLevel::Info, msg);
}

void TcpServerModule::applyKeepAlive(QTcpSocket* s, bool enable) const {
  // enable=true 开启 SO_KEEPALIVE，false 关闭；套接字无效或无描述符时直接跳过
  if (!s) return;
  const qintptr sd = s->socketDescriptor();
  if (sd == -1) return;
#ifdef _WIN32
  BOOL on = enable ? TRUE : FALSE;
  ::setsockopt((SOCKET)sd, SOL_SOCKET, SO_KEEPALIVE,
               reinterpret_cast<const char*>(&on), sizeof(on));
  if (enable) {
    tcp_keepalive ka;
    ka.onoff = 1;
    ka.keepalivetime = 10000;     // 空闲 10s 后开始探测
    ka.keepaliveinterval = 1000;  // 每次探测间隔 1s
    DWORD ret = 0;
    ::WSAIoctl((SOCKET)sd, SIO_KEEPALIVE_VALS, &ka, sizeof(ka), nullptr, 0,
               &ret, nullptr, nullptr);
  }
#else
  int on = enable ? 1 : 0;
  ::setsockopt(int(sd), SOL_SOCKET, SO_KEEPALIVE, &on, sizeof(on));
  if (enable) {
    // Linux 按套接字设定保活参数（与 Windows 一致：空闲 10s 探测、间隔 1s）。
    // 需 Linux 2.6.37+ 的 TCP_KEEPIDLE/TCP_KEEPINTVL/TCP_KEEPCNT 选项支持。
    int idle = 10;  // 空闲 10s 后开始探测
    int intvl = 1;  // 每次探测间隔 1s
    int cnt = 5;    // 探测 5 次无响应则判定断开
    ::setsockopt(int(sd), IPPROTO_TCP, TCP_KEEPIDLE, &idle, sizeof(idle));
    ::setsockopt(int(sd), IPPROTO_TCP, TCP_KEEPINTVL, &intvl, sizeof(intvl));
    ::setsockopt(int(sd), IPPROTO_TCP, TCP_KEEPCNT, &cnt, sizeof(cnt));
  }
#endif
}

void TcpServerModule::onKeepAliveToggled(bool checked) {
  // 对所有在线连接立即应用：开启时设保活，关闭时取消（恢复系统默认不探测）
  for (auto* tab : m_tabBySocket) {
    TcpConnectionTab* t = tab;
    if (t && t->isOnline()) applyKeepAlive(t->socket(), checked);
  }
}

void TcpServerModule::onStartStop() {
  if (m_server.isListening()) {
    m_server.close();
    // 通知脚本：所有在线连接将随停服断开（页签删除不触发 disconnected 信号）
    if (m_scriptBridge && m_scriptBridge->isRunning()) {
      for (auto* tab : m_tabByPeer)
        if (tab->isOnline()) m_scriptBridge->emitDisconnected(tab->peerText());
    }
    // 关闭全部连接页签（保留 Events 与 Script）
    while (m_tabs->count() > 2) {
      QWidget* w = m_tabs->widget(m_tabs->count() - 1);
      m_tabs->removeTab(m_tabs->count() - 1);
      delete w;
    }
    m_tabBySocket.clear();
    m_tabByPeer.clear();
    m_startBtn->setText(tr("Start"));
    m_listenIp->setEnabled(true);
    m_listenPort->setEnabled(true);
    m_status->setText(tr("Not listening"));
    logEvent(tr("Server stopped"));
    if (m_scriptBridge && m_scriptBridge->isRunning())
      m_scriptBridge->emitStop();
    return;
  }

  bool ok = false;
  const quint16 port = m_listenPort->text().toUShort(&ok);
  if (!ok || port == 0) {
    QMessageBox::warning(this, tr("Error"), tr("Invalid port"));
    return;
  }
  const QHostAddress addr = DataUtils::selectedBindAddr(m_listenIp);
  if (!m_server.listen(addr, port)) {
    QMessageBox::warning(this, tr("Listen failed"), m_server.errorString());
    return;
  }
  m_startBtn->setText(tr("Stop"));
  m_listenIp->setEnabled(false);
  m_listenPort->setEnabled(false);
  m_status->setText(
      tr("Listening on %1:%2 | 0 connections").arg(addr.toString()).arg(port));
  logEvent(tr("Listening on %1:%2").arg(addr.toString()).arg(port));
  if (m_scriptBridge && m_scriptBridge->isRunning())
    m_scriptBridge->emitStart();
}

void TcpServerModule::onNewConnection() {
  while (m_server.hasPendingConnections()) {
    QTcpSocket* s = m_server.nextPendingConnection();
    const QString peer = QStringLiteral("%1:%2")
                             .arg(s->peerAddress().toString())
                             .arg(s->peerPort());
    // 相同地址:端口的客户端重连时，复用原有页签（通常已离线）
    auto it = m_tabByPeer.find(peer);
    if (it != m_tabByPeer.end() && it.value() != nullptr) {
      TcpConnectionTab* tab = it.value();
      tab->reconnect(s);
      applyKeepAlive(s, m_keepalive && m_keepalive->isChecked());
      if (m_scriptBridge && m_scriptBridge->isRunning())
        m_scriptBridge->emitConnected(peer);
      m_tabBySocket.insert(s, tab);
      const int idx = m_tabs->indexOf(tab);
      if (idx >= 0) m_tabs->setTabText(idx, tab->peerText());  // 去掉 (offline)
      m_tabs->setCurrentWidget(tab);
      logEvent(tr("Client reconnected: %1").arg(peer));
      m_status->setText(
          tr("Listening | %1 connection(s)").arg(m_tabBySocket.size()));
      continue;
    }
    auto* tab = new TcpConnectionTab(s);
    applyKeepAlive(s, m_keepalive && m_keepalive->isChecked());
    m_tabBySocket.insert(s, tab);
    m_tabByPeer.insert(peer, tab);
    m_tabs->addTab(tab, tab->peerText());
    m_tabs->setCurrentWidget(tab);
    connect(tab, &TcpConnectionTab::closed, this,
            &TcpServerModule::onConnectionClosed);
    connect(tab, &TcpConnectionTab::dataReceived, this,
            [this](const QByteArray& d, const QString& p) {
              if (m_scriptBridge && m_scriptBridge->isRunning())
                m_scriptBridge->emitRecv(d, p);
            });
    connect(tab, &TcpConnectionTab::peerDisconnected, this,
            [this](const QString& p) {
              if (m_scriptBridge && m_scriptBridge->isRunning())
                m_scriptBridge->emitDisconnected(p);
            });
    if (m_scriptBridge && m_scriptBridge->isRunning())
      m_scriptBridge->emitConnected(peer);
    logEvent(tr("Client connected: %1").arg(peer));
    m_status->setText(
        tr("Listening | %1 connection(s)").arg(m_tabBySocket.size()));
  }
}

void TcpServerModule::onConnectionClosed(TcpConnectionTab* tab) {
  const int idx = m_tabs->indexOf(tab);
  if (idx >= 0)
    m_tabs->setTabText(idx, tab->peerText() + QStringLiteral(" (offline)"));
  for (auto it = m_tabBySocket.begin(); it != m_tabBySocket.end(); ++it) {
    if (it.value() == tab) {
      m_tabBySocket.erase(it);
      break;
    }
  }
  m_status->setText(
      tr("Listening | %1 connection(s)").arg(m_tabBySocket.size()));
}

void TcpServerModule::onTabCloseRequested(int index) {
  if (index <= 1) return;  // 保护 Events(0) 与 Script(1)
  QWidget* w = m_tabs->widget(index);
  auto* tab = qobject_cast<TcpConnectionTab*>(w);
  if (tab) {
    if (tab->isOnline() && m_scriptBridge && m_scriptBridge->isRunning())
      m_scriptBridge->emitDisconnected(tab->peerText());
    for (auto it = m_tabBySocket.begin(); it != m_tabBySocket.end(); ++it) {
      if (it.value() == tab) {
        m_tabBySocket.erase(it);
        break;
      }
    }
    m_tabByPeer.remove(tab->peerText());  // 关闭页签时移除对端索引，避免悬空
  }
  m_tabs->removeTab(index);
  delete w;
  m_status->setText(
      tr("Listening | %1 connection(s)").arg(m_tabBySocket.size()));
}

QJsonObject TcpServerModule::saveState() const {
  QJsonObject o;
  o["listenIp"] = m_listenIp->currentText();
  o["listenPort"] = m_listenPort->text();
  if (m_keepalive) o["keepalive"] = m_keepalive->isChecked();
  if (m_eventsAutoScroll) o["eventsAutoScroll"] = m_eventsAutoScroll->isChecked();
  if (m_scriptPanel) {
    o["scriptSource"] = m_scriptPanel->source();
    o["scriptEnabled"] = m_scriptPanel->wasEnabled();
  }
  return o;
}

void TcpServerModule::loadState(const QJsonObject& o) {
  // 可编辑下拉框：必须同步 currentIndex，否则 setCurrentText 只改行编辑文本，
  // selectedBindAddr() 仍读旧项 currentData()，保存的监听 IP 启动时未生效。
  if (o.contains("listenIp"))
    DataUtils::setComboCurrentText(m_listenIp, o.value("listenIp").toString());
  if (o.contains("listenPort"))
    m_listenPort->setText(o.value("listenPort").toString());
  if (o.contains("keepalive") && m_keepalive)
    m_keepalive->setChecked(o.value("keepalive").toBool());
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

void TcpServerModule::onScriptSend(const QByteArray& data,
                                   const QString& peer) {
  if (peer.isEmpty()) {
    // 广播：发给所有在线连接
    for (auto* tab : m_tabByPeer)
      if (tab->isOnline()) tab->scriptSend(data);
  } else {
    auto* tab = m_tabByPeer.value(peer, nullptr);
    if (tab && tab->isOnline())
      tab->scriptSend(data);
    else
      logEvent(tr("[script] no connection %1").arg(peer));
  }
}

void TcpServerModule::onScriptLog(const QString& msg) {
  logEvent(QStringLiteral("[script] ") + msg);
}
