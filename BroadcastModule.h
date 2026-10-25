#ifndef BROADCASTMODULE_H
#define BROADCASTMODULE_H

#include <QUdpSocket>

#include "NetModule.h"

class QComboBox;
class QLineEdit;
class QPushButton;

// 广播：发送广播数据报（IPv4）。255.255.255.255 为受限广播，
// 也可填子网定向广播地址（如 192.168.1.255）。
class BroadcastModule : public NetModule {
  Q_OBJECT
 public:
  explicit BroadcastModule(QWidget* parent = nullptr);

  QJsonObject saveState() const override;
  void loadState(const QJsonObject& obj) override;

 protected:
  void onSend(const QByteArray& data) override;

 private slots:
  void onOpenClose();
  void onReadyRead();

 private:
  // 通过原生 setsockopt 开启 SO_BROADCAST（Qt 5.14 未在枚举中暴露此选项）
  void enableBroadcast();
  // 取当前选择的本地绑定地址；"any4" 归一为 AnyIPv4
  QHostAddress selectedLocalAddr() const;

  QComboBox* m_localIp = nullptr;  // 本地绑定 IP（指定发出网卡/地址）
  QLineEdit* m_bcastAddr = nullptr;
  QLineEdit* m_targetPort = nullptr;
  QLineEdit* m_localPort = nullptr;
  QPushButton* m_openBtn = nullptr;
  QUdpSocket m_socket;
};

#endif  // BROADCASTMODULE_H
