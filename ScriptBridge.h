#ifndef SCRIPTBRIDGE_H
#define SCRIPTBRIDGE_H

#include <QByteArray>
#include <QObject>
#include <QProcess>
#include <QString>

class QLocalServer;
class QLocalSocket;
class QTimer;

// 每个实例页持有的 Python 子进程桥接器:
// 启动 python.exe 运行内置 runner.py,通过命名管道(QLocalServer/QLocalSocket)
// 交换结构化二进制帧。 脚本崩溃只影响本实例;QProcess 信号异步,GUI 线程不被阻塞。
// 上层接口(emit*、信号)与原 JSON-lines 版本完全一致。
class ScriptBridge : public QObject {
  Q_OBJECT
 public:
  explicit ScriptBridge(QObject* parent = nullptr);
  ~ScriptBridge() override;

  bool isAvailable() const { return m_available; }
  QString pythonVersion() const { return m_pyver; }

  // 启动子进程并下发脚本;若已在运行则先停止再重启。失败返回 false。
  bool start(const QString& source);
  // 停止子进程:发 SHUTDOWN 帧 → 关 socket → 等 → kill → 关 server
  void stop();
  bool isRunning() const;

  // C++ → Python 事件(仅当 isRunning() 时真正发出)
  void emitRecv(const QByteArray& data, const QString& peer);
  void emitConnected(const QString& peer);
  void emitDisconnected(const QString& peer);
  // 模块启动/停止事件:在监听开始、绑定、加入组播、连接建立等起点发 emitStart;
  // 在停服、解绑、离开组播、断开连接等终点发 emitStop。无 peer
  // 语义(模块级生命周期)。
  void emitStart();
  void emitStop();

 signals:
  // Python → C++ 命令
  void sendRequested(const QByteArray& data, const QString& peer);
  void logRequested(const QString& msg);
  void statusChanged(const QString& text);
  void availabilityChanged(bool available);
  void runningChanged(bool running);

 private slots:
  void onNewConnection();
  void onSocketReadyRead();
  void onSocketDisconnected();
  void onConnectTimeout();
  void onStateChanged(QProcess::ProcessState newState);
  void onErrorOccurred(QProcess::ProcessError error);

 private:
  // 帧类型(C++ → Python)
  enum FrameType : quint8 {
    FT_LOAD = 1,
    FT_RECV = 2,
    FT_CONNECTED = 3,
    FT_DISCONNECTED = 4,
    FT_START = 5,
    FT_STOP_EVENT = 6,
    FT_SHUTDOWN = 7
  };
  // 帧类型(Python → C++)
  enum RxFrameType : quint8 {
    FT_STARTED = 16,
    FT_SEND = 17,
    FT_LOG = 18,
    FT_ERROR = 19
  };

  void writeFrame(quint8 type, const QByteArray& payload);
  void flushWriteBuffer();
  void handleFrame(quint8 type, const QByteArray& payload);
  bool locatePython();
  bool ensureRunnerFile();
  void removeRunnerFile();
  void setAvailable(bool available);
  QString pipeName() const;

  QProcess* m_proc = nullptr;
  QLocalServer* m_server = nullptr;
  QLocalSocket* m_sock = nullptr;
  QTimer* m_connectTimer = nullptr;  // QProcess 起后 3s 无连接则报错并 kill
  QString m_runnerPath;
  QString m_pythonExe;
  QString m_pyver;
  QByteArray m_writeBuf;  // socket 未连时暂存帧
  QByteArray m_readBuf;   // 跨 readyRead 的半帧
  bool m_available = false;
  bool m_stopping = false;  // 区分用户主动 stop 与意外退出
};

#endif  // SCRIPTBRIDGE_H
