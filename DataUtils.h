#ifndef DATAUTILS_H
#define DATAUTILS_H

#include <QByteArray>
#include <QHostAddress>
#include <QNetworkInterface>
#include <QString>

class QComboBox;

// 数据显示/输入与地址选择的公共工具函数
class DataUtils {
 public:
  // 把字节数组格式化为显示文本。hex=true 返回空格分隔的十六进制（如 "48 65 6C
  // 6C 6F"）
  static QString toDisplay(const QByteArray& data, bool hex);

  // 把用户输入解析为字节数据。hex=true 按十六进制解析（容许空格/换行/0x 前缀）
  // 解析失败时 *ok=false 并填写 error
  static QByteArray fromInput(const QString& text, bool hex, bool* ok = nullptr,
                              QString* error = nullptr);

  // 填充"绑定 IP"下拉框：Any IPv4 / Any IPv6 / Any(dual) / 本机回环 /
  // 本机所有地址
  static void fillBindIpCombo(QComboBox* combo);

  // 仅填充 IPv4 绑定地址：Any IPv4 / 127.0.0.1 / 本机所有 IPv4 地址（广播专用）
  static void fillBindIpComboV4(QComboBox* combo);

  // 填充"目标/主机 IP"可编辑下拉框：回环 + 本机所有地址，当前项设为 defaultText
  static void fillTargetIpCombo(QComboBox* combo, const QString& defaultText);

  // 取下拉框当前选择对应的绑定地址（支持手输字面量）
  static QHostAddress selectedBindAddr(QComboBox* combo);

  // 解析绑定 IP 下拉框当前选择的"键"：行编辑文本与当前项文本一致时取其 data，
  // 否则以手输文本作为字面量键。避免可编辑框 setCurrentText 只改文本不改索引，
  // 导致 currentData() 仍返回旧索引的数据。
  static QString selectedBindKey(QComboBox* combo);

  // 恢复可编辑下拉框的当前文本：命中已有项则选中（同步 currentIndex），
  // 否则作为新项插入（data=字面量）再选中。比 setCurrentText 更可靠——
  // 后者仅 setEditText，不更新索引，会使后续 currentData() 返回错误值。
  static void setComboCurrentText(QComboBox* combo, const QString& text);

  // 由键（"any4"/"any6"/"any" 或 IP 字面量）得到绑定地址
  static QHostAddress bindAddrFromKey(const QString& key);

  // 由本机单播地址定位其所属网卡；未匹配返回无效接口（isValid()==false）
  static QNetworkInterface interfaceForAddress(const QHostAddress& addr);
};

#endif  // DATAUTILS_H
