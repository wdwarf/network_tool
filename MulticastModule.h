#ifndef MULTICASTMODULE_H
#define MULTICASTMODULE_H

#include <QHostAddress>
#include <QNetworkInterface>
#include <QUdpSocket>

#include "NetModule.h"

class QComboBox;
class QLineEdit;
class QPushButton;

// 组播：加入/离开组播组（IPv4 224.0.0.0/4 或 IPv6 ff00::/8），收发组播数据。
class MulticastModule : public NetModule {
  Q_OBJECT
 public:
  explicit MulticastModule(QWidget* parent = nullptr);

  QJsonObject saveState() const override;
  void loadState(const QJsonObject& obj) override;

 protected:
  void onSend(const QByteArray& data) override;

 private slots:
  void onJoinLeave();
  void onReadyRead();

 private:
  // 按组地址族与所选本地 IP 取绑定地址族；具体本地 IP 时同时定位其网卡
  QHostAddress bindAddrForGroup(const QHostAddress& group,
                                QNetworkInterface* ifaceOut = nullptr) const;

  QComboBox* m_groupIp = nullptr;
  QLineEdit* m_groupPort = nullptr;
  QLineEdit* m_ttl = nullptr;
  QComboBox* m_localIp = nullptr;  // 本地绑定 IP（决定加入哪个网卡/地址族）
  QPushButton* m_joinBtn = nullptr;
  QUdpSocket m_socket;
  QHostAddress m_group;
};

#endif  // MULTICASTMODULE_H
