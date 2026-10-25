#include "ScriptBridge.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocalServer>
#include <QLocalSocket>
#include <QStandardPaths>
#include <QTimer>
#include <QCoreApplication>

// 单帧 payload 上限,防损坏长度字段导致巨量分配
static constexpr qint64 kMaxPayload = 64 * 1024 * 1024;

// 小端整数写入 helper(追加到 QByteArray)
static void appendU32LE(QByteArray& buf, quint32 v) {
  buf.append(char(v & 0xff));
  buf.append(char((v >> 8) & 0xff));
  buf.append(char((v >> 16) & 0xff));
  buf.append(char((v >> 24) & 0xff));
}
static void appendU16LE(QByteArray& buf, quint16 v) {
  buf.append(char(v & 0xff));
  buf.append(char((v >> 8) & 0xff));
}
static quint32 readU32LE(const QByteArray& b, int off) {
  return quint32(quint8(b[off])) | (quint32(quint8(b[off + 1])) << 8) |
         (quint32(quint8(b[off + 2])) << 16) |
         (quint32(quint8(b[off + 3])) << 24);
}
static quint16 readU16LE(const QByteArray& b, int off) {
  return quint16(quint8(b[off])) | (quint16(quint8(b[off + 1])) << 8);
}

ScriptBridge::ScriptBridge(QObject* parent) : QObject(parent) {
  setAvailable(locatePython());
}

ScriptBridge::~ScriptBridge() {
  if (m_proc) {
    m_proc->disconnect(this);  // 析构期间禁止回调进半析构对象
    stop();
  } else {
    if (m_connectTimer) { delete m_connectTimer; m_connectTimer = nullptr; }
    if (m_server) { m_server->close(); delete m_server; m_server = nullptr; }
    removeRunnerFile();
  }
}

bool ScriptBridge::isRunning() const {
  return m_proc && m_proc->state() == QProcess::Running;
}

void ScriptBridge::setAvailable(bool available) {
  if (m_available == available) return;
  m_available = available;
  emit availabilityChanged(available);
}

QString ScriptBridge::pipeName() const {
  // 与临时 runner 文件名同源(本对象指针十六进制),保证多实例不冲突
  return QStringLiteral("network_tool_script_%1_%2")
      .arg(QCoreApplication::applicationPid())
      .arg(reinterpret_cast<quintptr>(this), 0, 16);
}

bool ScriptBridge::locatePython() {
  if (!m_pythonExe.isEmpty() && QFileInfo(m_pythonExe).isExecutable())
    return true;
  const QStringList names{"python", "py", "python3"};
  for (const QString& name : names) {
    const QString cand = QStandardPaths::findExecutable(name);
    if (cand.isEmpty()) continue;
    if (cand.contains(QStringLiteral("WindowsApps"), Qt::CaseInsensitive))
      continue;  // 跳过 Windows Store 空壳
    m_pythonExe = cand;
    return true;
  }
  return false;
}

bool ScriptBridge::ensureRunnerFile() {
  QFile res(QStringLiteral(":/script/runner.py"));
  if (!res.open(QIODevice::ReadOnly | QIODevice::Text)) {
    emit statusChanged(tr("Internal error: runner.py missing"));
    return false;
  }
  const QByteArray data = res.readAll();
  res.close();
  const QString path =
      QDir::temp().filePath(QStringLiteral("network_tool_runner_%1.py")
                                .arg(reinterpret_cast<quintptr>(this), 0, 16));
  QFile f(path);
  if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    emit statusChanged(tr("Cannot write runner: %1").arg(f.errorString()));
    return false;
  }
  if (f.write(data) != data.size()) {
    emit statusChanged(tr("Cannot write runner (short write)"));
    return false;
  }
  f.close();
  m_runnerPath = path;
  return true;
}

bool ScriptBridge::start(const QString& source) {
  if (!locatePython()) {
    setAvailable(false);
    emit statusChanged(tr("Python not found"));
    return false;
  }
  setAvailable(true);

  if (m_proc) stop();  // 先停旧进程再重启
  if (!ensureRunnerFile()) {
    removeRunnerFile();
    return false;
  }

  // 命名管道服务端:先清理可能残留(Linux socket 文件),再 listen
  const QString name = pipeName();
  m_server = new QLocalServer(this);
  m_server->removeServer(name);
  if (!m_server->listen(name)) {
    emit statusChanged(tr("Cannot listen pipe: %1").arg(m_server->errorString()));
    delete m_server;
    m_server = nullptr;
    removeRunnerFile();
    return false;
  }
  connect(m_server, &QLocalServer::newConnection, this, &ScriptBridge::onNewConnection);

  m_proc = new QProcess(this);
  m_proc->setProcessChannelMode(QProcess::SeparateChannels);
  // 用户脚本可能直接写 fd 1(C 扩展 printf、os.write(1,...)、库 banner),
  // 若不排空会填满管道阻塞解释器。stdout 不承载协议帧,排空即丢弃。
  connect(m_proc, &QProcess::readyReadStandardOutput, this, [this]() {
    m_proc->readAllStandardOutput();
  });
  connect(m_proc, &QProcess::readyReadStandardError, this, [this]() {
    // stderr 仅作诊断文本,不进协议帧
    const QByteArray err = m_proc->readAllStandardError();
    if (!err.isEmpty())
      emit statusChanged(tr("Script stderr: %1").arg(QString::fromUtf8(err).trimmed()));
  });
  connect(m_proc, &QProcess::stateChanged, this, &ScriptBridge::onStateChanged);
  connect(m_proc, &QProcess::errorOccurred, this, &ScriptBridge::onErrorOccurred);

  m_pyver.clear();
  m_readBuf.clear();
  m_writeBuf.clear();
  m_stopping = false;

  // 命令行传完整管道名(Windows \\.\pipe\..., Linux 文件路径),runner 直连
  m_proc->start(m_pythonExe, {QStringLiteral("-u"), m_runnerPath,
                              m_server->fullServerName()});

  // 连接超时守护:QProcess 起后 3s 内无 newConnection 视为 runner 未连上
  m_connectTimer = new QTimer(this);
  m_connectTimer->setSingleShot(true);
  connect(m_connectTimer, &QTimer::timeout, this, &ScriptBridge::onConnectTimeout);
  m_connectTimer->start(3000);

  // 下发脚本(socket 未连,帧进 m_writeBuf,连入后冲刷)
  QByteArray payload;
  appendU32LE(payload, quint32(source.toUtf8().size()));
  payload += source.toUtf8();
  writeFrame(FT_LOAD, payload);
  emit statusChanged(tr("Starting Python..."));
  return true;
}

void ScriptBridge::stop() {
  if (!m_proc) {
    // server 可能已建但进程未起(理论):一并清
    if (m_connectTimer) { delete m_connectTimer; m_connectTimer = nullptr; }
    if (m_server) { m_server->close(); delete m_server; m_server = nullptr; }
    removeRunnerFile();
    return;
  }
  m_stopping = true;
  if (m_proc->state() != QProcess::NotRunning) {
    if (m_sock && m_sock->state() == QLocalSocket::ConnectedState) {
      writeFrame(FT_SHUTDOWN, QByteArray());
      m_sock->flush();
      m_sock->disconnectFromServer();
    } else if (m_sock) {
      // socket 未连:先关掉,让 runner 在 _raw_read 收到 EOF 自然退出,避免硬 kill
      m_sock->disconnectFromServer();
    }
    if (!m_proc->waitForFinished(500)) {
      m_proc->kill();
      m_proc->waitForFinished(500);
    }
  }
  m_stopping = false;
  if (m_connectTimer) { delete m_connectTimer; m_connectTimer = nullptr; }
  if (m_sock) { m_sock->disconnect(this); delete m_sock; m_sock = nullptr; }
  if (m_server) { m_server->close(); delete m_server; m_server = nullptr; }
  delete m_proc;
  m_proc = nullptr;
  m_writeBuf.clear();
  m_readBuf.clear();
  removeRunnerFile();
  emit statusChanged(tr("Stopped"));
  emit runningChanged(false);
}

void ScriptBridge::removeRunnerFile() {
  if (!m_runnerPath.isEmpty()) {
    QFile::remove(m_runnerPath);
    m_runnerPath.clear();
  }
}

void ScriptBridge::writeFrame(quint8 type, const QByteArray& payload) {
  if (m_sock && m_sock->state() == QLocalSocket::ConnectedState) {
    QByteArray frame;
    frame.append(char(type));
    appendU32LE(frame, quint32(payload.size()));
    frame += payload;
    m_sock->write(frame);
  } else {
    // 进程已起但 socket 未连:暂存,连入后冲刷
    m_writeBuf.append(char(type));
    appendU32LE(m_writeBuf, quint32(payload.size()));
    m_writeBuf += payload;
  }
}

void ScriptBridge::flushWriteBuffer() {
  if (!m_sock || m_writeBuf.isEmpty() ||
      m_sock->state() != QLocalSocket::ConnectedState)
    return;
  m_sock->write(m_writeBuf);
  m_writeBuf.clear();
}

void ScriptBridge::emitRecv(const QByteArray& data, const QString& peer) {
  if (!isRunning()) return;
  const QByteArray peerUtf8 = peer.toUtf8();
  Q_ASSERT(peerUtf8.size() <= 0xFFFF);  // peer 形如 ip:port,远小于 64KB;越界说明上游异常
  QByteArray p;
  appendU32LE(p, quint32(data.size()));
  p += data;
  appendU16LE(p, quint16(peerUtf8.size()));
  p += peerUtf8;
  writeFrame(FT_RECV, p);
}

void ScriptBridge::emitConnected(const QString& peer) {
  if (!isRunning()) return;
  const QByteArray peerUtf8 = peer.toUtf8();
  Q_ASSERT(peerUtf8.size() <= 0xFFFF);
  QByteArray p;
  appendU16LE(p, quint16(peerUtf8.size()));
  p += peerUtf8;
  writeFrame(FT_CONNECTED, p);
}

void ScriptBridge::emitDisconnected(const QString& peer) {
  if (!isRunning()) return;
  const QByteArray peerUtf8 = peer.toUtf8();
  Q_ASSERT(peerUtf8.size() <= 0xFFFF);
  QByteArray p;
  appendU16LE(p, quint16(peerUtf8.size()));
  p += peerUtf8;
  writeFrame(FT_DISCONNECTED, p);
}

void ScriptBridge::emitStart() {
  if (!isRunning()) return;
  writeFrame(FT_START, QByteArray());
}

void ScriptBridge::emitStop() {
  if (!isRunning()) return;
  writeFrame(FT_STOP_EVENT, QByteArray());
}

void ScriptBridge::onNewConnection() {
  // 每实例仅接受首个 runner 连接;多余的连接丢弃,避免两路帧交错破坏缓冲同步
  if (m_sock) {
    if (QLocalSocket* extra = m_server->nextPendingConnection()) extra->deleteLater();
    return;
  }
  m_sock = m_server->nextPendingConnection();
  if (!m_sock) return;
  if (m_connectTimer) { m_connectTimer->stop(); delete m_connectTimer; m_connectTimer = nullptr; }
  connect(m_sock, &QLocalSocket::readyRead, this, &ScriptBridge::onSocketReadyRead);
  connect(m_sock, &QLocalSocket::disconnected, this, &ScriptBridge::onSocketDisconnected);
  flushWriteBuffer();  // 把启动期间缓冲的 LOAD 等帧冲下去
}

void ScriptBridge::onConnectTimeout() {
  // QProcess 起后 3s 仍无 runner 连入:报错并终止进程
  emit statusChanged(tr("Python did not connect"));
  if (m_connectTimer) { delete m_connectTimer; m_connectTimer = nullptr; }
  if (m_proc && m_proc->state() != QProcess::NotRunning) {
    m_stopping = true;
    m_proc->kill();
    m_proc->waitForFinished(500);
    m_stopping = false;
  }
  if (m_sock) { m_sock->disconnect(this); delete m_sock; m_sock = nullptr; }
  if (m_server) { m_server->close(); delete m_server; m_server = nullptr; }
  m_writeBuf.clear();
  m_readBuf.clear();
  m_pyver.clear();  // 清掉上一轮残留的 Python 版本,避免状态行张冠李戴
  removeRunnerFile();
  emit runningChanged(false);
}

void ScriptBridge::onSocketReadyRead() {
  m_readBuf += m_sock->readAll();
  forever {
    if (m_readBuf.size() < 5) break;
    const quint32 len = readU32LE(m_readBuf, 1);
    if (len > kMaxPayload) {
      emit statusChanged(tr("Bad frame from script (len %1)").arg(len));
      m_readBuf.clear();  // 坏帧:丢弃缓冲,不断开
      break;
    }
    if (m_readBuf.size() < qint64(5 + len)) break;  // 半帧,等下次
    const quint8 type = quint8(m_readBuf[0]);
    const QByteArray payload = m_readBuf.mid(5, len);
    m_readBuf = m_readBuf.mid(5 + len);
    handleFrame(type, payload);
  }
}

void ScriptBridge::onSocketDisconnected() {
  // socket 断开不一定是进程退出;真正状态由 onStateChanged 统一收口
}

void ScriptBridge::handleFrame(quint8 type, const QByteArray& payload) {
  // 越界保护助手:从 payload 偏移处取定长整数,不足或偏移为负返回 false 并报坏帧。
  // 偏移可能被损坏的长度字段污染成负值,故显式判 off<0;容量比较用 qint64,
  // 避免 quint32 长度(可达 0xFFFFFFFF)窄化成负 int 后绕过 > payload.size() 判定。
  auto readCheckedU32 = [&](int& off, quint32& out) -> bool {
    if (off < 0 || qint64(off) + 4 > payload.size()) return false;
    out = readU32LE(payload, off);
    off += 4;
    return true;
  };
  auto readCheckedU16 = [&](int& off, quint16& out) -> bool {
    if (off < 0 || qint64(off) + 2 > payload.size()) return false;
    out = readU16LE(payload, off);
    off += 2;
    return true;
  };
  // 取带长度前缀的 UTF-8 字符串,声明长度越界返回 false。
  auto takeStr = [&](int& off, int width, QString& out) -> bool {
    quint32 n = 0;
    if (width == 2) {
      quint16 n16 = 0;
      if (!readCheckedU16(off, n16)) return false;
      n = n16;
    } else {
      if (!readCheckedU32(off, n)) return false;
    }
    if (qint64(off) + qint64(n) > payload.size()) return false;
    out = QString::fromUtf8(payload.mid(off, int(n)));
    off += int(n);
    return true;
  };

  int off = 0;
  switch (type) {
    case FT_STARTED: {  // STARTED [u16 ver]
      QString ver;
      if (!takeStr(off, 2, ver)) { emit statusChanged(tr("Bad frame from script")); break; }
      m_pyver = ver;
      emit statusChanged(tr("Running (Python %1)").arg(m_pyver));
      break;
    }
    case FT_SEND: {  // SEND [u32 data][data][u16 peer][peer]
      quint32 dl = 0;
      if (!readCheckedU32(off, dl)) { emit statusChanged(tr("Bad frame from script")); break; }
      if (qint64(off) + qint64(dl) > payload.size()) { emit statusChanged(tr("Bad frame from script")); break; }
      const QByteArray data = payload.mid(off, int(dl));
      off += int(dl);
      QString peer;
      if (!takeStr(off, 2, peer)) { emit statusChanged(tr("Bad frame from script")); break; }
      emit sendRequested(data, peer);
      break;
    }
    case FT_LOG: {  // LOG [u32 msg]
      QString msg;
      if (!takeStr(off, 4, msg)) { emit statusChanged(tr("Bad frame from script")); break; }
      emit logRequested(msg);
      break;
    }
    case FT_ERROR: {  // ERROR [u32 msg][u32 tb]
      QString msg, tb;
      if (!takeStr(off, 4, msg) || !takeStr(off, 4, tb)) {
        emit statusChanged(tr("Bad frame from script"));
        break;
      }
      emit statusChanged(tr("Script error: %1").arg(msg));
      emit logRequested(tb.isEmpty() ? msg : msg + QStringLiteral("\n") + tb);
      break;
    }
    default:
      emit statusChanged(tr("Unknown frame type %1").arg(type));
  }
}

void ScriptBridge::onStateChanged(QProcess::ProcessState newState) {
  if (newState == QProcess::Running) {
    emit runningChanged(true);
  } else if (newState == QProcess::NotRunning) {
    if (!m_stopping)
      emit statusChanged(tr("Script process exited (code %1)")
                             .arg(m_proc ? m_proc->exitCode() : -1));
    emit runningChanged(false);
  }
}

void ScriptBridge::onErrorOccurred(QProcess::ProcessError error) {
  if (error == QProcess::FailedToStart) {
    emit statusChanged(tr("Failed to start Python: %1")
                           .arg(m_proc ? m_proc->errorString() : tr("unknown")));
    // FailedToStart 后 stateChanged(NotRunning) 也会到,但此时无进程可等;
    // 立即拆掉连接超时计时器与 runner 文件,避免 3s 后再补一条误导状态
    if (m_connectTimer) { delete m_connectTimer; m_connectTimer = nullptr; }
    removeRunnerFile();
  }
}
