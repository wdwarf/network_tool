#include "DataUtils.h"

#include <QAbstractSocket>
#include <QComboBox>
#include <QNetworkInterface>
#include <QObject>
#include <QRegularExpression>

QString DataUtils::toDisplay(const QByteArray& data, bool hex) {
  if (hex) {
    const QByteArray hexStr = data.toHex();
    QString result;
    result.reserve(hexStr.size() + hexStr.size() / 2 + 1);
    for (int i = 0; i < hexStr.size(); i += 2) {
      if (i > 0) result.append(QLatin1Char(' '));
      result.append(QString::fromLatin1(hexStr.mid(i, 2)));
    }
    return result;
  }
  // 优先按 Utf-8 解码；若出现替换字符则回退到 Latin-1（避免乱码框）
  QString text = QString::fromUtf8(data);
  if (text.contains(QChar::ReplacementCharacter))
    text = QString::fromLatin1(data);
  return text;
}

QByteArray DataUtils::fromInput(const QString& text, bool hex, bool* ok,
                                QString* error) {
  if (ok) *ok = false;
  if (hex) {
    QString cleaned = text;
    cleaned.remove(QLatin1Char(' '));
    cleaned.remove(QLatin1Char('\t'));
    cleaned.remove(QLatin1Char('\r'));
    cleaned.remove(QLatin1Char('\n'));
    cleaned.remove(QStringLiteral("0x"), Qt::CaseInsensitive);

    if (cleaned.size() % 2 != 0) {
      if (error) *error = QObject::tr("Hex input has an odd number of digits");
      return {};
    }
    static const QRegularExpression re(QStringLiteral("^[0-9A-Fa-f]*$"));
    if (!re.match(cleaned).hasMatch()) {
      if (error) *error = QObject::tr("Hex input contains non-hex characters");
      return {};
    }
    if (ok) *ok = true;
    return QByteArray::fromHex(cleaned.toLatin1());
  }
  if (ok) *ok = true;
  return text.toUtf8();
}

void DataUtils::fillBindIpCombo(QComboBox* combo) {
  combo->addItem(QObject::tr("Any IPv4"), QStringLiteral("any4"));
  combo->addItem(QObject::tr("Any IPv6"), QStringLiteral("any6"));
  combo->addItem(QObject::tr("Any (dual)"), QStringLiteral("any"));
  combo->addItem(QStringLiteral("127.0.0.1"), QStringLiteral("127.0.0.1"));
  combo->addItem(QStringLiteral("::1"), QStringLiteral("::1"));
  for (const auto& a : QNetworkInterface::allAddresses())
    combo->addItem(a.toString(), a.toString());
  combo->setCurrentIndex(0);
}

void DataUtils::fillBindIpComboV4(QComboBox* combo) {
  // 仅 IPv4：受限/定向广播在 IPv6 下不存在，故广播地址族固定为 IPv4
  combo->addItem(QObject::tr("Any IPv4"), QStringLiteral("any4"));
  combo->addItem(QStringLiteral("127.0.0.1"), QStringLiteral("127.0.0.1"));
  for (const auto& a : QNetworkInterface::allAddresses())
    if (a.protocol() == QAbstractSocket::IPv4Protocol && !a.isLoopback())
      combo->addItem(a.toString(), a.toString());
  combo->setCurrentIndex(0);
}

void DataUtils::fillTargetIpCombo(QComboBox* combo,
                                  const QString& defaultText) {
  // 回环与本机所有地址作为候选项；用户仍可在可编辑框手输任意目标
  combo->addItem(QStringLiteral("127.0.0.1"));
  combo->addItem(QStringLiteral("::1"));
  for (const auto& a : QNetworkInterface::allAddresses())
    combo->addItem(a.toString());
  // 默认项：若与某条目相同则选中它，否则作为可编辑文本填入并插到首位
  int idx = combo->findText(defaultText);
  if (idx >= 0) {
    combo->setCurrentIndex(idx);
  } else {
    combo->insertItem(0, defaultText);
    combo->setCurrentIndex(0);
  }
}

QHostAddress DataUtils::bindAddrFromKey(const QString& key) {
  if (key == QStringLiteral("any4")) return QHostAddress::AnyIPv4;
  if (key == QStringLiteral("any6")) return QHostAddress::AnyIPv6;
  if (key == QStringLiteral("any")) return QHostAddress::Any;
  return QHostAddress(key);
}

QString DataUtils::selectedBindKey(QComboBox* combo) {
  // 可编辑下拉框的行编辑文本可能与当前选中项文本不一致（手输字面量、或被
  // setCurrentText 只改了文本没动索引）。仅当二者一致时才信任当前项的 data
  // （如 "any4"/具体 IP）；否则以行编辑文本作为字面量键。这样既能正确识别
  // 通配项，也能让手输的任意 IP 生效——旧的 "currentData() 为空才回退"
  // 写法在 currentData() 恰好非空时会误用旧索引的数据。
  const QString text = combo->currentText();
  const int idx = combo->currentIndex();
  if (idx >= 0 && combo->itemText(idx) == text) {
    const QString data = combo->itemData(idx).toString();
    if (!data.isEmpty()) return data;
  }
  return text;
}

void DataUtils::setComboCurrentText(QComboBox* combo, const QString& text) {
  // 命中已有项则选中它（同步 currentIndex/currentData），否则插为新项再选中。
  // 不能用 setCurrentText：对可编辑框它等价于 setEditText，只改行编辑文本，
  // 不动 currentIndex——后续 currentData() 仍返回旧索引的数据，致使保存的
  // 本地 IP 显示正确但绑定用的是旧项（如 AnyIPv4），即"启动时未生效"。
  const int idx = combo->findText(text);
  if (idx >= 0) {
    combo->setCurrentIndex(idx);
    return;
  }
  // 未命中已有项：把字面量作为新项插入（data=字面量本身）并选中，
  // 保证 currentData() 非空且与文本一致。
  combo->insertItem(0, text, text);
  combo->setCurrentIndex(0);
}

QHostAddress DataUtils::selectedBindAddr(QComboBox* combo) {
  return bindAddrFromKey(selectedBindKey(combo));
}

QNetworkInterface DataUtils::interfaceForAddress(const QHostAddress& addr) {
  // 在本机所有网卡中查找拥有该地址的接口；用于组播按指定网卡加入
  for (const auto& iface : QNetworkInterface::allInterfaces())
    for (const auto& entry : iface.addressEntries())
      if (entry.ip() == addr) return iface;
  return QNetworkInterface();
}
