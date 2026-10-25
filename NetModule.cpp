#include "NetModule.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QShortcut>
#include <QSpinBox>
#include <QSplitter>
#include <QTabBar>
#include <QTabWidget>
#include <QTime>
#include <QTimer>
#include <QVBoxLayout>

#include <Scintilla.h>

#include "DataUtils.h"
#include "LogEditor.h"
#include "SciFindBar.h"
#include "ScriptBridge.h"
#include "ScriptPanel.h"

NetModule::NetModule(QWidget* parent, bool enableScript)
    : QWidget(parent), m_enableScript(enableScript) {
  m_configLayout = new QVBoxLayout;
  m_configLayout->setContentsMargins(0, 0, 0, 0);

  auto* mainLayout = new QVBoxLayout(this);
  mainLayout->addLayout(m_configLayout);

  // 数据收发区与脚本区分置两个页签：Data 为收发，Script 为脚本编辑/运行。
  m_tabs = new QTabWidget;
  m_tabs->setTabsClosable(false);  // Data/Script 为固定页签，不可关闭
  mainLayout->addWidget(m_tabs, 1);

  // ---- Data 页签：接收区 + 发送区，用 QSplitter 上下分割 ----
  // 仅两个数据框（m_receiveEdit / m_sendEdit）参与拉伸（各自 stretch=1），
  // 其间的标题行与控件行 stretch=0 保持固定高度——拖动分隔条只改变数据框大小。
  auto* splitter = new QSplitter(Qt::Vertical);
  splitter->setChildrenCollapsible(false);  // 不允许拖到 0 高度
  splitter->setHandleWidth(6);

  // ===== 接收区（splitter 上半部）=====
  auto* recvWidget = new QWidget;
  auto* recvLayout = new QVBoxLayout(recvWidget);
  recvLayout->setContentsMargins(0, 0, 0, 0);
  recvLayout->setSpacing(2);

  // 接收区标题行（固定）
  auto* recvRow = new QHBoxLayout;
  recvRow->addWidget(new QLabel(tr("Receive:")));
  recvRow->addStretch();
  auto* findRecvBtn = new QPushButton(tr("Find"));
  findRecvBtn->setToolTip(tr("Find in received data (Ctrl+F)"));
  findRecvBtn->setCheckable(true);
  recvRow->addWidget(findRecvBtn);
  auto* clearRecvBtn = new QPushButton(tr("Clear"));
  recvRow->addWidget(clearRecvBtn);
  recvLayout->addLayout(recvRow);

  m_receiveEdit = new LogEditor;
  // renderEntry() 已按 Timestamp 复选框自行拼时间戳，关闭 LogEditor
  // 自带时间戳，避免出现两份时间。
  m_receiveEdit->setTimestampEnabled(false);
  m_receiveEdit->setMinimumHeight(80);     // 保留最小可读高度，避免被压扁
  recvLayout->addWidget(m_receiveEdit, 1);

  // 接收区查找条（仅查找）：由 Find 按钮切换显示，Esc 关闭
  m_recvFindBar = new SciFindBar(true);
  m_recvFindBar->attach(m_receiveEdit);
  m_recvFindBar->hide();
  recvLayout->addWidget(m_recvFindBar);
  connect(findRecvBtn, &QPushButton::toggled, this, [this](bool on) {
    if (on)
      m_recvFindBar->activate();
    else
      m_recvFindBar->hide();
  });
  connect(m_recvFindBar, &SciFindBar::closeRequested, this,
          [this, findRecvBtn] {
            m_recvFindBar->hide();
            findRecvBtn->setChecked(false);
          });

  // Ctrl+F 唤出接收区查找条（与 Find 按钮同一入口）
  auto* findShortcut = new QShortcut(QKeySequence(Qt::CTRL + Qt::Key_F), this);
  connect(findShortcut, &QShortcut::activated, this,
          [this, findRecvBtn] { findRecvBtn->setChecked(true); });

  // 显示模式行（固定）
  auto* dispRow = new QHBoxLayout;
  dispRow->addWidget(new QLabel(tr("Display:")));
  m_recvText = new QRadioButton(tr("Text"));
  m_recvHex = new QRadioButton(tr("Hex"));
  m_recvText->setChecked(true);
  dispRow->addWidget(m_recvText);
  dispRow->addWidget(m_recvHex);
  // 接收区 Text/Hex 用按钮组强制互斥（同一组内的两个 Radio
  // 互斥，避免误触发"全不选"）
  m_recvGroup = new QButtonGroup(this);
  m_recvGroup->setExclusive(true);
  m_recvGroup->addButton(m_recvText);
  m_recvGroup->addButton(m_recvHex);
  dispRow->addSpacing(20);
  m_showTimestamp = new QCheckBox(tr("Timestamp"));
  m_showTimestamp->setChecked(true);
  m_showTx = new QCheckBox(tr("Show TX"));
  m_showTx->setChecked(true);
  dispRow->addWidget(m_showTimestamp);
  dispRow->addWidget(m_showTx);
  // 自动滚动开关：ON=粘底跟随（在底部时新数据跟随滚动），OFF=视图静止
  m_autoScroll = new QCheckBox(tr("Auto-scroll"));
  m_autoScroll->setChecked(true);
  m_receiveEdit->setAutoScroll(true);
  dispRow->addWidget(m_autoScroll);
  dispRow->addStretch();
  recvLayout->addLayout(dispRow);

  splitter->addWidget(recvWidget);

  // ===== 发送区（splitter 下半部）=====
  auto* sendWidget = new QWidget;
  auto* sendLayout = new QVBoxLayout(sendWidget);
  sendLayout->setContentsMargins(0, 0, 0, 0);
  sendLayout->setSpacing(2);

  // 发送区标题行（固定）
  auto* sendRow = new QHBoxLayout;
  sendRow->addWidget(new QLabel(tr("Send:")));
  sendRow->addStretch();
  auto* clearSendBtn = new QPushButton(tr("Clear"));
  sendRow->addWidget(clearSendBtn);
  sendLayout->addLayout(sendRow);

  m_sendEdit = new QPlainTextEdit;
  m_sendEdit->setMinimumHeight(60);  // 保留最小可编辑高度
  // 不再 setMaximumHeight：由 splitter 自由调整大小
  sendLayout->addWidget(m_sendEdit, 1);

  auto* sendCtlRow = new QHBoxLayout;
  sendCtlRow->addWidget(new QLabel(tr("Send as:")));
  m_sendText = new QRadioButton(tr("Text"));
  m_sendHex = new QRadioButton(tr("Hex"));
  m_sendText->setChecked(true);
  sendCtlRow->addWidget(m_sendText);
  sendCtlRow->addWidget(m_sendHex);
  // 发送区 Text/Hex 用按钮组强制互斥（与接收区独立，互不影响）
  m_sendGroup = new QButtonGroup(this);
  m_sendGroup->setExclusive(true);
  m_sendGroup->addButton(m_sendText);
  m_sendGroup->addButton(m_sendHex);
  sendCtlRow->addSpacing(20);
  sendCtlRow->addWidget(new QLabel(tr("Count:")));
  m_countSpin = new QSpinBox;
  m_countSpin->setRange(-1, 9999999);
  m_countSpin->setValue(1);
  m_countSpin->setSpecialValueText(QStringLiteral("∞"));  // ∞
  m_countSpin->setMaximumWidth(90);
  sendCtlRow->addWidget(m_countSpin);
  sendCtlRow->addWidget(new QLabel(tr("Interval:")));
  m_intervalSpin = new QSpinBox;
  m_intervalSpin->setRange(0, 9999999);
  m_intervalSpin->setValue(1000);
  m_intervalSpin->setSuffix(tr(" ms"));
  m_intervalSpin->setMaximumWidth(110);
  sendCtlRow->addWidget(m_intervalSpin);
  sendCtlRow->addStretch();
  m_sendBtn = new QPushButton(tr("Send"));
  m_sendBtn->setEnabled(false);
  sendCtlRow->addWidget(m_sendBtn);
  sendLayout->addLayout(sendCtlRow);

  splitter->addWidget(sendWidget);

  // 初始比例：接收区占大部分（3:1）；setStretchFactor 控制后续窗口缩放时的分配
  splitter->setStretchFactor(0, 3);
  splitter->setStretchFactor(1, 1);
  splitter->setSizes({400, 120});

  m_tabs->addTab(splitter, tr("Data"));

  setSendEnabled(false);

  connect(m_sendBtn, &QPushButton::clicked, this, &NetModule::sendClicked);
  connect(clearRecvBtn, &QPushButton::clicked, this,
          &NetModule::clearReceivedClicked);
  connect(clearSendBtn, &QPushButton::clicked, this,
          [this] { m_sendEdit->clear(); });
  connect(m_recvText, &QRadioButton::toggled, this,
          &NetModule::rerenderReceived);
  connect(m_recvHex, &QRadioButton::toggled, this,
          &NetModule::rerenderReceived);
  connect(m_showTimestamp, &QCheckBox::toggled, this,
          &NetModule::rerenderReceived);
  connect(m_showTx, &QCheckBox::toggled, this, &NetModule::rerenderReceived);
  // 切换自动滚动：直接作用于 LogEditor 的粘底滚动引擎
  connect(m_autoScroll, &QCheckBox::toggled, this,
          [this](bool on) { m_receiveEdit->setAutoScroll(on); });

  m_timer = new QTimer(this);
  m_timer->setSingleShot(true);
  connect(m_timer, &QTimer::timeout, this, &NetModule::onLoopTick);

  // ---- Script 页签：脚本编辑/运行（单连接模块启用） ----
  // 服务器模块在自身 "Script" 页签持有独立 bridge，不走 NetModule 脚本路径。
  if (m_enableScript) {
    m_scriptBridge = new ScriptBridge(this);
    m_scriptPanel =
        new ScriptPanel(m_scriptBridge, false, this);  // 常驻填满页签
    m_tabs->addTab(m_scriptPanel, tr("Script"));
    // 清除 Script 页签关闭按钮（setTabsClosable(false) 已无按钮，双保险）
    m_tabs->tabBar()->setTabButton(m_tabs->indexOf(m_scriptPanel),
                                   QTabBar::LeftSide, nullptr);
    m_tabs->tabBar()->setTabButton(m_tabs->indexOf(m_scriptPanel),
                                   QTabBar::RightSide, nullptr);
    connect(m_scriptPanel, &ScriptPanel::sendRequested, this,
            [this](const QByteArray& data, const QString& peer) {
              Q_UNUSED(peer);  // 单连接模块：peer 无意义，沿用唯一对端发送
              if (m_showTx->isChecked())
                appendReceived(data, QStringLiteral("TX"), QString());
              onSend(data);
            });
    connect(m_scriptPanel, &ScriptPanel::logRequested, this,
            [this](const QString& msg) {
              logInfo(QStringLiteral("[script] ") + msg);
            });
  }
}

QVBoxLayout* NetModule::configLayout() const { return m_configLayout; }

void NetModule::appendReceived(const QByteArray& data, const QString& direction,
                               const QString& peer) {
  ReceivedEntry e;
  e.data = data;
  e.direction = direction;
  e.peer = peer;
  e.timestamp = currentTimeStr();
  e.isInfo = false;
  m_entries.append(e);
  while (m_entries.size() > kMaxEntries) m_entries.removeFirst();

  if (direction == QStringLiteral("RX") && m_scriptBridge &&
      m_scriptBridge->isRunning())
    m_scriptBridge->emitRecv(data, peer);

  m_receiveEdit->appendLog(direction == QStringLiteral("RX")
                               ? LogEditor::LogLevel::Info
                               : LogEditor::LogLevel::Trace,
                           renderEntry(e));
}

void NetModule::logInfo(const QString& message) {
  ReceivedEntry e;
  e.direction = QStringLiteral("INFO");
  e.peer = message;
  e.timestamp = currentTimeStr();
  e.isInfo = true;
  m_entries.append(e);
  while (m_entries.size() > kMaxEntries) m_entries.removeFirst();

  m_receiveEdit->appendLog(LogEditor::LogLevel::Warn, renderEntry(e));
}

void NetModule::rerenderReceived() {
  // clear() 会丢失滚动位置：重建前记录首可见行，OFF 时恢复以保持阅读位置
  const auto firstVisible = m_receiveEdit->send(SCI_GETFIRSTVISIBLELINE);
  m_receiveEdit->clear();
  for (const auto& e : m_entries) {
    const QString line = renderEntry(e);
    if (!line.isEmpty())
      m_receiveEdit->appendLog(e.isInfo ? LogEditor::LogLevel::Warn
                                        : (e.direction ==
                                                   QStringLiteral("RX")
                                               ? LogEditor::LogLevel::Info
                                               : LogEditor::LogLevel::Trace),
                               line);
  }
  // 重渲染后：ON 跳到底部（跟随最新数据），OFF 恢复首可见行（保持阅读位置）
  if (m_autoScroll->isChecked())
    m_receiveEdit->send(SCI_GOTOPOS, m_receiveEdit->send(SCI_GETTEXTLENGTH));
  else
    m_receiveEdit->send(SCI_SETFIRSTVISIBLELINE, firstVisible);
}

QString NetModule::renderEntry(const ReceivedEntry& e) const {
  // 关闭 TX 显示时跳过 TX 条目
  if (e.direction == QStringLiteral("TX") && !m_showTx->isChecked())
    return QString();

  QString line;
  if (m_showTimestamp->isChecked() && !e.timestamp.isEmpty())
    line += QStringLiteral("[%1] ").arg(e.timestamp);

  if (e.isInfo) return line + e.peer;

  if (!e.direction.isEmpty()) line += e.direction + QLatin1Char(' ');
  if (!e.peer.isEmpty()) line += e.peer + QStringLiteral(" : ");
  line += DataUtils::toDisplay(e.data, m_recvHex->isChecked());
  return line;
}

QString NetModule::currentTimeStr() const {
  return QTime::currentTime().toString(QStringLiteral("hh:mm:ss.zzz"));
}

void NetModule::sendClicked() {
  if (m_sending) {  // 按钮当前为 Stop：点击即中止循环
    stopLoop();
    return;
  }
  const QString text = m_sendEdit->toPlainText();
  bool ok = false;
  QString err;
  const QByteArray data =
      DataUtils::fromInput(text, m_sendHex->isChecked(), &ok, &err);
  if (!ok) {
    QMessageBox::warning(this, tr("Send error"), err);
    return;
  }
  const int count = m_countSpin->value();
  const int interval = m_intervalSpin->value();
  if (count == 0) return;  // 0 次：不发送、不循环
  if (count == 1) {        // 单发：与原行为一致，不起定时器
    if (m_showTx->isChecked())
      appendReceived(data, QStringLiteral("TX"), QString());
    onSend(data);
    return;
  }
  startLoop(data, count, interval);
}

void NetModule::startLoop(const QByteArray& data, int count, int interval) {
  m_loopData = data;
  m_infinite = (count < 0);
  m_remaining = m_infinite ? 0 : count;  // 共需发送 count 次（含首次）
  m_sending = true;
  m_sendBtn->setText(tr("Stop"));
  m_countSpin->setEnabled(false);  // 总数已提交，循环中不可改
  doOneSend();                     // 立即发第 1 次
  if (!m_sending) return;  // onSend 同步触发 stopLoop（如断连）：已停，勿再排
  if (m_infinite || m_remaining > 0)
    m_timer->start(interval > 0 ? interval : 0);
  else
    stopLoop();
}

void NetModule::doOneSend() {
  if (m_showTx->isChecked())
    appendReceived(m_loopData, QStringLiteral("TX"), QString());
  onSend(m_loopData);
  if (!m_infinite) --m_remaining;
}

void NetModule::onLoopTick() {
  if (!m_sending) return;
  if (!m_infinite && m_remaining <= 0) {
    stopLoop();
    return;
  }
  doOneSend();
  if (!m_sending) return;  // onSend 同步触发 stopLoop：已停，勿再排
  const int interval = m_intervalSpin->value();  // 每次读最新值，支持实时调速
  if (m_infinite || m_remaining > 0)
    m_timer->start(interval > 0 ? interval : 0);
  else
    stopLoop();
}

void NetModule::stopLoop() {
  m_sending = false;
  m_timer->stop();
  m_sendBtn->setText(tr("Send"));
  m_countSpin->setEnabled(true);
}

void NetModule::clearReceivedClicked() {
  m_entries.clear();
  m_receiveEdit->clear();
  onClearReceived();
}

void NetModule::setSendEnabled(bool enabled) {
  if (!enabled && m_sending) stopLoop();  // 断连等场景：先中止循环
  m_sendBtn->setEnabled(enabled);
}

QJsonObject NetModule::saveState() const {
  // 公共显示/发送区状态：收发十六进制开关、时间戳、显示 TX、发送框文本
  QJsonObject o;
  o["recvHex"] = m_recvHex->isChecked();
  o["sendHex"] = m_sendHex->isChecked();
  o["showTimestamp"] = m_showTimestamp->isChecked();
  o["showTx"] = m_showTx->isChecked();
  o["autoScroll"] = m_autoScroll->isChecked();
  o["sendText"] = m_sendEdit->toPlainText();
  o["sendCount"] = m_countSpin->value();
  o["sendInterval"] = m_intervalSpin->value();
  if (m_scriptPanel) {
    o["scriptSource"] = m_scriptPanel->source();
    o["scriptEnabled"] = m_scriptPanel->wasEnabled();
  }
  return o;
}

void NetModule::loadState(const QJsonObject& o) {
  // 仅在键存在时恢复，避免用默认值覆盖未持久化的字段
  if (o.contains("recvHex")) m_recvHex->setChecked(o.value("recvHex").toBool());
  if (o.contains("sendHex")) m_sendHex->setChecked(o.value("sendHex").toBool());
  if (o.contains("showTimestamp"))
    m_showTimestamp->setChecked(o.value("showTimestamp").toBool());
  if (o.contains("showTx")) m_showTx->setChecked(o.value("showTx").toBool());
  // 恢复自动滚动：显式同步 LogEditor，不依赖 setChecked 仅在变化时发信号
  if (o.contains("autoScroll")) {
    const bool on = o.value("autoScroll").toBool();
    m_autoScroll->setChecked(on);
    m_receiveEdit->setAutoScroll(on);
  }
  if (o.contains("sendText"))
    m_sendEdit->setPlainText(o.value("sendText").toString());
  if (o.contains("sendCount"))
    m_countSpin->setValue(o.value("sendCount").toInt(1));
  if (o.contains("sendInterval"))
    m_intervalSpin->setValue(o.value("sendInterval").toInt(1000));
  if (o.contains("scriptSource") && m_scriptPanel)
    m_scriptPanel->setSource(o.value("scriptSource").toString());
  if (o.contains("scriptEnabled") && m_scriptPanel)
    m_scriptPanel->setWasEnabled(o.value("scriptEnabled").toBool());
  // 切换显示模式后重渲染已缓存的接收记录。
  // 注意：上面 setChecked 触发的重渲染可能用恢复前的旧 autoScroll 值，
  // 本次收尾重渲染以恢复后的值执行，是 OFF 时滚动位置正确的关键，勿删。
  rerenderReceived();
}

void NetModule::notifyConnected(const QString& peer) {
  if (m_scriptBridge && m_scriptBridge->isRunning())
    m_scriptBridge->emitConnected(peer);
}

void NetModule::notifyDisconnected(const QString& peer) {
  if (m_scriptBridge && m_scriptBridge->isRunning())
    m_scriptBridge->emitDisconnected(peer);
}
