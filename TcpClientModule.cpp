// _WIN32 由编译器在 Windows 目标下预定义；必须在任何 Qt 头之前包含 winsock2.h，
// 以避免与 windows.h 的符号冲突。Linux 下改用 POSIX 套接字头，提供 setsockopt/
// SOL_SOCKET/SO_KEEPALIVE/SO_LINGER/linger 等符号（Windows 分支由 winsock2.h
// 提供）。
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#endif

#include <QAbstractSocket>
#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QHostAddress>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>

#include "DataUtils.h"
#include "ScriptBridge.h"
#include "TcpClientModule.h"

TcpClientModule::TcpClientModule(QWidget* parent) : NetModule(parent) {
  auto* row = new QHBoxLayout;
  row->addWidget(new QLabel(tr("Host:")));
  m_host = new QComboBox;
  m_host->setEditable(true);
  m_host->setMinimumWidth(140);
  DataUtils::fillTargetIpCombo(m_host, QStringLiteral("127.0.0.1"));
  row->addWidget(m_host);
  row->addWidget(new QLabel(tr("Port:")));
  m_port = new QLineEdit(QStringLiteral("8080"));
  m_port->setMaximumWidth(80);
  row->addWidget(m_port);
  row->addWidget(new QLabel(tr("Local port:")));
  m_localPort = new QLineEdit;
  m_localPort->setPlaceholderText(tr("auto"));
  m_localPort->setMaximumWidth(80);
  row->addWidget(m_localPort);
  m_connBtn = new QPushButton(tr("Connect"));
  row->addWidget(m_connBtn);
  // 保活开关：开启后对连接套接字设置 SO_KEEPALIVE（Windows 下并用
  // SIO_KEEPALIVE_VALS 调整保活时间/间隔），用于探测对端是否存活。
  m_keepalive = new QCheckBox(tr("Keep-Alive"));
  row->addWidget(m_keepalive);
  row->addStretch();
  configLayout()->addLayout(row);

  connect(m_connBtn, &QPushButton::clicked, this,
          &TcpClientModule::onConnectDisconnect);
  m_socket = new QTcpSocket(this);
  connectSocketSignals();
  // 保活开关切换时立即作用于已建立的连接（未连接时仅记录状态，连接成功时再应用）
  connect(m_keepalive, &QCheckBox::toggled, this,
          &TcpClientModule::onKeepAliveToggled);
}

void TcpClientModule::connectSocketSignals() {
  connect(m_socket, &QTcpSocket::connected, this, &TcpClientModule::onConnected);
  connect(m_socket, &QTcpSocket::disconnected, this,
          &TcpClientModule::onDisconnected);
  connect(m_socket, &QTcpSocket::readyRead, this, &TcpClientModule::onReadyRead);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
  connect(m_socket, &QAbstractSocket::errorOccurred, this,
          &TcpClientModule::onSocketError);
#else
  // Qt 5.14 中 error 与 error() getter 同名，用 QOverload 选定信号重载
  connect(m_socket,
          QOverload<QAbstractSocket::SocketError>::of(&QAbstractSocket::error),
          this, &TcpClientModule::onSocketError);
#endif
}

void TcpClientModule::resetSocketLayerHard() {
  // 处于连接/查找态时正常 abort() 即可走 close→resetSocketLayer 清理；
  // 真正的污染发生在 UnconnectedState 下残留的 bound 引擎——此时 abort()/
  // close()/disconnectFromHost() 均提前返回，故必须物理销毁对象重建。
  if (m_socket->state() != QAbstractSocket::UnconnectedState) {
    m_socket->abort();
    return;
  }
  delete m_socket;
  m_socket = new QTcpSocket(this);
  connectSocketSignals();
}

void TcpClientModule::applyKeepAlive(bool enable) const {
  const qintptr sd = m_socket->socketDescriptor();
  if (sd == -1) return;
#ifdef _WIN32
  BOOL on = enable ? TRUE : FALSE;
  ::setsockopt((SOCKET)sd, SOL_SOCKET, SO_KEEPALIVE,
               reinterpret_cast<const char*>(&on), sizeof(on));
  if (enable) {
    // 打开总开关后再用 SIO_KEEPALIVE_VALS 设定保活探测间隔
    // （2 小时太长，此处改为 10 秒空闲、1 秒间隔、5 次探测）。
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

void TcpClientModule::onKeepAliveToggled(bool checked) {
  // 仅在已连接时即时生效；未连接时开关状态已保存，下次连接时 onConnected
  // 会按当前值应用
  if (m_socket->state() == QAbstractSocket::ConnectedState)
    applyKeepAlive(checked);
}

void TcpClientModule::resetControls() {
  m_connBtn->setText(tr("Connect"));
  setSendEnabled(false);
  m_host->setEnabled(true);
  m_port->setEnabled(true);
  m_localPort->setEnabled(true);
  m_connBtn->setEnabled(true);
}

void TcpClientModule::onConnectDisconnect() {
  if (m_socket->state() == QAbstractSocket::ConnectedState ||
      m_socket->state() == QAbstractSocket::ConnectingState ||
      m_socket->state() == QAbstractSocket::HostLookupState) {
    m_socket->abort();
    resetControls();
    return;
  }

  m_host->setEnabled(false);
  m_port->setEnabled(false);
  m_localPort->setEnabled(false);

  bool ok = false;
  const quint16 port = m_port->text().toUShort(&ok);
  if (!ok || port == 0) {
    QMessageBox::warning(this, tr("Error"), tr("Invalid port"));
    return;
  }
  // 上一次连接若失败，Qt 不会拆毁 bound 套接字引擎：cachedSocketDescriptor
  // 仍非 -1、引擎错误仍为 ConnectionRefusedError，而 abort()/close() 因处于
  // UnconnectedState 提前返回无法清理。若直接复用，bind() 会重新发射该陈旧
  // 错误，表现为"第二次连接先报 Connection refused 再连上"。发起新连接前
  // 原位重建套接字以获得干净套接字层。
  resetSocketLayerHard();
  // 可选绑定本地端口（否则由系统分配）；ReuseAddressHint 允许端口重用，
  // 以便断开后用同一本地端口重连（绕过 TIME_WAIT 占用）。
  const QString lp = m_localPort->text().trimmed();
  if (!lp.isEmpty()) {
    bool ok2 = false;
    const quint16 localP = lp.toUShort(&ok2);
    if (ok2 && localP != 0)
      m_socket->bind(QHostAddress(QHostAddress::Any), localP,
                     QAbstractSocket::ReuseAddressHint);
  }
  logInfo(tr("Connecting to %1:%2...").arg(m_host->currentText()).arg(port));
  m_connBtn->setText(tr("Connecting..."));
  m_socket->connectToHost(m_host->currentText(), port);
}

void TcpClientModule::onConnected() {
  m_connBtn->setText(tr("Disconnect"));
  // 连接建立后应用 SO_LINGER（供同端口重连），并按当前开关状态应用保活
  const qintptr sd = m_socket->socketDescriptor();
  if (sd != -1) {
#ifdef _WIN32
    // SO_LINGER{on=1, linger=0}：断开时 abort()/close 发 RST 而非 FIN，
    // 避免本地端口进入 TIME_WAIT，从而允许断开后立即用同一本地端口重连。
    linger l;
    l.l_onoff = 1;
    l.l_linger = 0;
    ::setsockopt((SOCKET)sd, SOL_SOCKET, SO_LINGER,
                 reinterpret_cast<const char*>(&l), sizeof(l));
#else
    // Linux 同样设置 SO_LINGER{1,0} 使 close 发 RST，规避 TIME_WAIT，
    // 允许同四元组断开后立即重连。
    linger l;
    l.l_onoff = 1;
    l.l_linger = 0;
    ::setsockopt(int(sd), SOL_SOCKET, SO_LINGER, &l, sizeof(l));
#endif
    if (m_keepalive && m_keepalive->isChecked()) applyKeepAlive(true);
  }
  logInfo(
      tr("Connected to %1:%2").arg(m_host->currentText()).arg(m_port->text()));
  notifyConnected(
      QStringLiteral("%1:%2").arg(m_host->currentText()).arg(m_port->text()));
  // 连接建立即视为模块启动：触发 on_start（与 on_connect
  // 同期，语义为模块级生命周期）
  if (auto* b = scriptBridge(); b && b->isRunning()) b->emitStart();
  setSendEnabled(true);
  m_host->setEnabled(false);
  m_port->setEnabled(false);
  m_localPort->setEnabled(false);
}

void TcpClientModule::onDisconnected() {
  logInfo(tr("Disconnected"));
  notifyDisconnected(
      QStringLiteral("%1:%2").arg(m_host->currentText()).arg(m_port->text()));
  if (auto* b = scriptBridge(); b && b->isRunning()) b->emitStop();
  resetControls();
}

void TcpClientModule::onSocketError(
    QAbstractSocket::SocketError /*socketError*/) {
  logInfo(tr("Socket error: %1").arg(m_socket->errorString()));
  // 连接未成功（仍在 Connecting/HostLookup，或已退回
  // Unconnected）时重置按钮/字段， 使其回到 "Connect" 态。注意 errorOccurred
  // 先于 stateChanged(Unconnected) 触发， 故不能用 == UnconnectedState
  // 判定，否则连接失败时按钮会卡在 "Disconnect"。
  if (m_socket->state() != QAbstractSocket::ConnectedState) resetControls();
}

void TcpClientModule::onReadyRead() {
  const QByteArray data = m_socket->readAll();
  if (!data.isEmpty())
    appendReceived(
        data, QStringLiteral("RX"),
        QStringLiteral("%1:%2").arg(m_host->currentText()).arg(m_port->text()));
}

void TcpClientModule::onSend(const QByteArray& data) { m_socket->write(data); }

QJsonObject TcpClientModule::saveState() const {
  // 在公共显示/发送区状态之上追加连接参数
  QJsonObject o = NetModule::saveState();
  o["host"] = m_host->currentText();
  o["port"] = m_port->text();
  o["localPort"] = m_localPort->text();
  if (m_keepalive) o["keepalive"] = m_keepalive->isChecked();
  return o;
}

void TcpClientModule::loadState(const QJsonObject& o) {
  NetModule::loadState(o);
  if (o.contains("host")) m_host->setCurrentText(o.value("host").toString());
  if (o.contains("port")) m_port->setText(o.value("port").toString());
  if (o.contains("localPort"))
    m_localPort->setText(o.value("localPort").toString());
  if (o.contains("keepalive") && m_keepalive)
    m_keepalive->setChecked(o.value("keepalive").toBool());
}
