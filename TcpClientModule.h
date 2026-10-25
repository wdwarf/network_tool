#ifndef TCPCLIENTMODULE_H
#define TCPCLIENTMODULE_H

#include <QTcpSocket>

#include "NetModule.h"

class QComboBox;
class QLineEdit;
class QPushButton;
class QCheckBox;

// TCP 客户端：连接远端 host:port，可指定本地端口，收发数据。
class TcpClientModule : public NetModule {
  Q_OBJECT
 public:
  explicit TcpClientModule(QWidget* parent = nullptr);

  QJsonObject saveState() const override;
  void loadState(const QJsonObject& obj) override;

 protected:
  void onSend(const QByteArray& data) override;

 private slots:
  void onConnectDisconnect();
  void onConnected();
  void onDisconnected();
  void onReadyRead();
  void onSocketError(QAbstractSocket::SocketError socketError);
  void onKeepAliveToggled(bool checked);

 private:
  void resetControls();
  // 对连接套接字应用/取消保活（enable=true 开启 SO_KEEPALIVE，false 关闭）
  void applyKeepAlive(bool enable) const;
  // 建立 m_socket 的信号/槽连接（构造及重建套接字后均调用）
  void connectSocketSignals();
  // 连接失败后 Qt 不会拆毁 bound 套接字引擎：cachedSocketDescriptor 仍非 -1、
  // 引擎错误仍为 ConnectionRefusedError，且 abort()/close() 因处于
  // UnconnectedState 而提前返回，无法清理。下次 bind() 会重新发射该陈旧错误
  // （表现为第二次连接先报 "Connection refused" 再连上）。此处在发起连接前
  // 原位重建 m_socket，以获得干净的套接字层。
  void resetSocketLayerHard();

  QComboBox* m_host = nullptr;
  QLineEdit* m_port = nullptr;
  QLineEdit* m_localPort = nullptr;
  QCheckBox* m_keepalive = nullptr;  // TCP 保活开关
  QPushButton* m_connBtn = nullptr;
  QTcpSocket* m_socket = nullptr;  // 指针：需在连接失败后原位重建以清除残留引擎
};

#endif  // TCPCLIENTMODULE_H
