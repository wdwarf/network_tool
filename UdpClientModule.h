#ifndef UDPCLIENTMODULE_H
#define UDPCLIENTMODULE_H

#include <QUdpSocket>

#include "NetModule.h"

class QComboBox;
class QLineEdit;
class QPushButton;

// UDP 客户端：向目标发送数据报；Open 绑定本地端口后可接收回复。
class UdpClientModule : public NetModule {
  Q_OBJECT
 public:
  explicit UdpClientModule(QWidget* parent = nullptr);

  QJsonObject saveState() const override;
  void loadState(const QJsonObject& obj) override;

 protected:
  void onSend(const QByteArray& data) override;

 private slots:
  void onOpenClose();
  void onReadyRead();

 private:
  QComboBox* m_targetIp = nullptr;
  QLineEdit* m_targetPort = nullptr;
  QLineEdit* m_localPort = nullptr;
  QPushButton* m_openBtn = nullptr;
  QUdpSocket m_socket;
};

#endif  // UDPCLIENTMODULE_H
