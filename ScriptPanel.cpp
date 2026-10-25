#include "ScriptPanel.h"

#include <Scintilla.h>
#include <ScintillaEdit.h>

#include <QColor>
#include <QEvent>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QPalette>
#include <QShortcut>
#include <QStyle>
#include <QVBoxLayout>

#include "LexillaSetup.h"
#include "SciFindBar.h"
#include "ScriptBridge.h"

// SciLexer.h：SCE_P_*（Python 词法风格常量，Lexilla 提供）
#include "SciLexer.h"

// 把单色标准图标按目标颜色重新着色：先用 SourceIn
// 用颜色填满不透明区域，得到纯色图标。 用于让运行图标显绿、停止图标显红（标准
// SP_MediaPlay/SP_MediaStop 默认为系统单色）。
static QIcon recolorStandardIcon(const QStyle* style, QStyle::StandardPixmap sp,
                                 const QColor& color) {
  const QIcon src = style->standardIcon(sp);
  QIcon out;
  for (const QSize& sz : src.availableSizes()) {
    const QPixmap pm = src.pixmap(sz);
    QPixmap colored(pm.size());
    colored.setDevicePixelRatio(pm.devicePixelRatio());
    colored.fill(Qt::transparent);
    QPainter p(&colored);
    p.drawPixmap(0, 0, pm);
    p.setCompositionMode(QPainter::CompositionMode_SourceIn);
    p.fillRect(colored.rect(), color);
    p.end();
    out.addPixmap(colored);
  }
  return out;
}

// QColor -> Scintilla 颜色：Scintilla 用 0xBBGGRR（低字节为红），
// 与 Qt 的 0xRRGGBB 通道顺序相反，须按 BGR 打包。
static sptr_t sciColor(const QColor& c) {
  return (c.red() & 0xff) | ((c.green() & 0xff) << 8) |
         ((c.blue() & 0xff) << 16);
}

ScriptPanel::ScriptPanel(ScriptBridge* bridge, bool collapsible,
                         QWidget* parent)
    : QWidget(parent), m_bridge(bridge) {
  // 运行/停止合并为单按钮：未运行显示绿色播放图标并启动脚本；运行中显示红色停止图标并停止脚本。
  // 颜色与标准图标的系统主题无关——recolorStandardIcon 用 SourceIn 强制重着色。
  m_runStopBtn = new QPushButton;
  m_runStopBtn->setIcon(recolorStandardIcon(
      style(), QStyle::SP_MediaPlay, QColor(0x2E, 0x7D, 0x32)));  // 绿：运行
  m_runStopBtn->setToolTip(tr("Run script"));
  m_status = new QLabel(tr("Python: ?"));
  m_status->setStyleSheet(QStringLiteral("color: Gray;"));

  m_edit = new ScintillaEdit;
  m_edit->setFrameShape(QFrame::Shape::StyledPanel);
  m_edit->installEventFilter(this);
  setupEditor();
  // 占位字符串改为默认代码内容：作为真实可编辑文本填入编辑器，而非灰色提示。
  m_edit->setText(tr("def on_receive(data, peer):\n"
                     "    if data == b\"PING\":\n"
                     "        send(b\"PONG\")\n"
                     "    log(f\"recv {len(data)}B from {peer}\")\n"
                     "\n"
                     "def on_start():\n"
                     "    log(\"started\")\n"
                     "\n"
                     "def on_stop():\n"
                     "    log(\"stopped\")")
                      .toUtf8()
                      .constData());
  if (collapsible) m_edit->setMaximumHeight(120);
  // 非折叠（服务器 Script 页签）时不限高，让编辑器填满页签

  // 查找/替换条：默认隐藏，由 Find 按钮或 Ctrl+F 唤出
  m_findBar = new SciFindBar(false);
  m_findBar->attach(m_edit);
  m_findBar->hide();
  connect(m_findBar, &SciFindBar::closeRequested, m_findBar, &QWidget::hide);

  m_findBtn = new QPushButton(tr("Find"));
  m_findBtn->setToolTip(tr("Find/Replace (Ctrl+F)"));
  m_findBtn->setCheckable(true);
  connect(m_findBtn, &QPushButton::toggled, this, [this](bool on) {
    if (on)
      m_findBar->activate();
    else
      m_findBar->hide();
  });
  connect(m_findBar, &SciFindBar::closeRequested, this,
          [this] { m_findBtn->setChecked(false); });
  // Ctrl+F 唤出查找条（与按钮同一入口，切换按钮状态即可）
  auto* findShortcut = new QShortcut(QKeySequence(Qt::CTRL + Qt::Key_F), this);
  connect(findShortcut, &QShortcut::activated, this,
          [this] { m_findBtn->setChecked(true); });

  auto* ctl = new QHBoxLayout;
  ctl->addWidget(m_runStopBtn);
  ctl->addWidget(m_findBtn);
  ctl->addStretch();
  ctl->addWidget(m_status);

  auto* content = new QWidget;
  auto* cl = new QVBoxLayout(content);
  cl->setContentsMargins(0, 0, 0, 0);
  cl->setSpacing(2);
  cl->addLayout(ctl);
  cl->addWidget(m_edit);
  cl->addWidget(m_findBar);

  auto* lay = new QVBoxLayout(this);
  lay->setContentsMargins(0, 0, 0, 0);
  lay->setSpacing(2);

  if (collapsible) {
    // 折叠态：标题按钮切换内容可见性（单连接模块沿用）
    auto* title = new QPushButton(tr("Script ▾"));
    title->setCheckable(true);
    title->setFlat(true);
    lay->addWidget(title);
    lay->addWidget(content);
    content->setVisible(false);
    connect(title, &QPushButton::toggled, this, [content, title](bool on) {
      content->setVisible(on);
      title->setText(on ? tr("Script ▴") : tr("Script ▾"));
    });
  } else {
    // 非折叠态：服务器 Script 页签，内容常驻、无标题按钮
    lay->addWidget(content);
  }

  connect(m_runStopBtn, &QPushButton::clicked, this,
          &ScriptPanel::onRunStopClicked);

  connect(m_bridge, &ScriptBridge::statusChanged, this,
          &ScriptPanel::onStatusChanged);
  connect(m_bridge, &ScriptBridge::availabilityChanged, this,
          &ScriptPanel::onAvailabilityChanged);
  connect(m_bridge, &ScriptBridge::sendRequested, this,
          &ScriptPanel::sendRequested);
  connect(m_bridge, &ScriptBridge::logRequested, this,
          &ScriptPanel::logRequested);
  connect(m_bridge, &ScriptBridge::runningChanged, this,
          &ScriptPanel::onRunningChanged);

  onAvailabilityChanged(m_bridge->isAvailable());
}

void ScriptPanel::setupEditor() {
  // Python 词法器（Scintilla 5 起词法器拆到 Lexilla，需经
  // SCI_SETILEXER 注入）。ILexer5* 由 Scintilla 接管生命周期。
  if (Scintilla::ILexer5* lex = LexillaSetup::createPythonLexer())
    m_edit->setILexer(reinterpret_cast<sptr_t>(lex));
  m_edit->setCodePage(SC_CP_UTF8);
  // 行号边距：按最大行号位数估算宽度，避免逐行测量
  m_edit->setMarginTypeN(0, SC_MARGIN_NUMBER);
  m_edit->setMarginWidthN(0, 16 + 8 * 3);
  // 制表相关：Python 惯用 4 空格缩进；回车缩进由 SCI_NEWLINE 的
  // 自动缩进逻辑处理（Scintilla 默认保持上一行缩进）
  m_edit->setTabWidth(4);
  m_edit->setIndent(4);
  m_edit->setUseTabs(false);
  m_edit->setTabIndents(true);
  m_edit->setBackSpaceUnIndents(true);
  // 自动换行会破坏 Python 缩进视觉，保持不换行 + 尾随空白裁剪
  m_edit->setWrapMode(0);
  applyPythonStyles();
  // Python 3 关键字表（keyword set 0）：Lexilla 的 Python 词法器不内置
  // 关键字，需显式注入，识别到关键字时才着 SCE_P_WORD 色。
  m_edit->setKeyWords(
      0, "False None True and as assert async await break class continue def "
         "del elif else except finally for from global if import in is "
         "lambda nonlocal not or pass raise return try while with yield");
}

void ScriptPanel::applyPythonStyles() {
  // 字体先落到 STYLE_DEFAULT，再 STYLECLEARALL 让其余风格继承
  const bool darkTheme =
      m_edit->palette().color(QPalette::Base).lightness() < 128;
  const QColor background = darkTheme ? QColor(0x1E, 0x1E, 0x1E)
                                     : QColor(0xFF, 0xFF, 0xFF);
  const QColor normalText = darkTheme ? QColor(0xE6, 0xE6, 0xE6)
                                     : QColor(0x00, 0x00, 0x00);
  const QColor commentText = darkTheme ? QColor(0x7F, 0xC7, 0xA4)
                                      : QColor(0x00, 0x80, 0x00);
  const QColor literalText = darkTheme ? QColor(0xFF, 0xB4, 0x8A)
                                       : QColor(0xA3, 0x15, 0x15);
  const QColor keywordText = darkTheme ? QColor(0x8F, 0xC7, 0xFF)
                                       : QColor(0x00, 0x00, 0xFF);
  const QColor classText = darkTheme ? QColor(0x4E, 0xC9, 0xB0)
                                     : QColor(0x26, 0x7F, 0x99);
  const QColor functionText = darkTheme ? QColor(0xDC, 0xDC, 0xAA)
                                        : QColor(0x79, 0x5E, 0x26);
  const QColor decoratorText = darkTheme ? QColor(0xD7, 0xBA, 0x7D)
                                         : QColor(0x80, 0x00, 0x80);

  QByteArray fontName = QFont("Consolas").family().toUtf8();
  m_edit->styleSetFont(STYLE_DEFAULT, fontName.constData());
  m_edit->styleSetSize(STYLE_DEFAULT, 10);
  m_edit->styleSetFore(STYLE_DEFAULT, sciColor(normalText));
  m_edit->styleClearAll();

  // 配色参考常见 IDE 主题：注释绿、字符串/数字紫红、关键字蓝、
  // 类名青、函数名藏蓝、装饰器灰。颜色一律经 sciColor() 按 BGR
  // 打包（Scintilla 低字节为红，直接写 0xRRGGBB 会红蓝颠倒）。
  m_edit->styleSetFore(SCE_P_DEFAULT, sciColor(normalText));
  m_edit->styleSetFore(SCE_P_COMMENTLINE, sciColor(commentText));
  m_edit->styleSetFore(SCE_P_COMMENTBLOCK, sciColor(commentText));
  m_edit->styleSetFore(SCE_P_NUMBER, sciColor(literalText));
  m_edit->styleSetFore(SCE_P_STRING, sciColor(literalText));
  m_edit->styleSetFore(SCE_P_CHARACTER, sciColor(literalText));
  m_edit->styleSetFore(SCE_P_TRIPLE, sciColor(literalText));
  m_edit->styleSetFore(SCE_P_TRIPLEDOUBLE, sciColor(literalText));
  m_edit->styleSetFore(SCE_P_FSTRING, sciColor(literalText));
  m_edit->styleSetFore(SCE_P_FCHARACTER, sciColor(literalText));
  m_edit->styleSetFore(SCE_P_FTRIPLE, sciColor(literalText));
  m_edit->styleSetFore(SCE_P_FTRIPLEDOUBLE, sciColor(literalText));
  m_edit->styleSetFore(SCE_P_WORD, sciColor(keywordText));
  m_edit->styleSetFore(SCE_P_CLASSNAME, sciColor(classText));
  m_edit->styleSetFore(SCE_P_DEFNAME, sciColor(functionText));
  m_edit->styleSetFore(SCE_P_OPERATOR, sciColor(normalText));
  m_edit->styleSetFore(SCE_P_IDENTIFIER, sciColor(normalText));
  m_edit->styleSetFore(SCE_P_DECORATOR, sciColor(decoratorText));
  m_edit->styleSetItalic(SCE_P_COMMENTLINE, 1);
  m_edit->styleSetItalic(SCE_P_COMMENTBLOCK, 1);
  applyEditorPalette();
}

void ScriptPanel::applyEditorPalette() {
  const bool darkTheme =
      m_edit->palette().color(QPalette::Base).lightness() < 128;
  const QColor background = darkTheme ? QColor(0x1E, 0x1E, 0x1E)
                                     : QColor(0xFF, 0xFF, 0xFF);
  const QColor text = darkTheme ? QColor(0xD4, 0xD4, 0xD4)
                                : QColor(0x00, 0x00, 0x00);
  const QColor lineNumberBackground =
      darkTheme ? QColor(0x25, 0x25, 0x26) : QColor(0xF3, 0xF3, 0xF3);
  const QColor lineNumberText =
      darkTheme ? QColor(0x85, 0x85, 0x85) : QColor(0x23, 0x78, 0x93);
  const QColor selectionBackground =
      darkTheme ? QColor(0x26, 0x4F, 0x78) : QColor(0xAD, 0xD6, 0xFF);
  // The translucent highlight is blended with the editor background. Choose
  // the text color from that background rather than from the raw highlight
  // color, otherwise a light theme can produce white text on light blue.
  const QColor selectionText = darkTheme ? Qt::white : Qt::black;
  m_edit->styleSetBack(STYLE_DEFAULT, sciColor(background));
  m_edit->styleSetFore(STYLE_DEFAULT, sciColor(text));
  for (int style = 0; style < 32; ++style)
    m_edit->styleSetBack(style, sciColor(background));
  m_edit->styleSetBack(STYLE_LINENUMBER, sciColor(lineNumberBackground));
  m_edit->styleSetFore(STYLE_LINENUMBER, sciColor(lineNumberText));

  // Match LogEditor: draw an opaque selection below the text and explicitly
  // provide its foreground color.
  m_edit->send(SCI_SETSELECTIONLAYER, SC_LAYER_BASE);
  m_edit->send(SCI_SETSELALPHA, 96, 0);
  m_edit->send(SCI_SETSELBACK, 1, sciColor(selectionBackground));
  m_edit->send(SCI_SETSELFORE, 1, sciColor(selectionText));
}

bool ScriptPanel::eventFilter(QObject* watched, QEvent* event) {
  if (watched == m_edit &&
      (event->type() == QEvent::PaletteChange ||
       event->type() == QEvent::ApplicationPaletteChange)) {
    applyPythonStyles();
  }
  return QWidget::eventFilter(watched, event);
}

void ScriptPanel::onRunStopClicked() {
  if (m_bridge->isRunning()) {
    m_bridge->stop();
    return;
  }
  // Run = 重载当前编辑器文本并启动子进程；start()
  // 内部已处理"已在运行则先停再启"。
  if (!m_bridge->start(source())) {
    // 启动失败（找不到解释器/写临时 runner 失败）：回到未运行态
    m_runStopBtn->setIcon(recolorStandardIcon(style(), QStyle::SP_MediaPlay,
                                              QColor(0x2E, 0x7D, 0x32)));
    m_runStopBtn->setToolTip(tr("Run script"));
    m_lastEnabled = false;
  }
}

void ScriptPanel::onRunningChanged(bool running) {
  if (running) {
    // 运行中：红色停止图标，并禁用编辑器防止修改正在执行的脚本
    m_runStopBtn->setIcon(recolorStandardIcon(
        style(), QStyle::SP_MediaStop, QColor(0xC6, 0x28, 0x28)));  // 红：停止
    m_runStopBtn->setToolTip(tr("Stop script"));
    m_edit->setReadOnly(true);
    m_lastEnabled = true;
  } else {
    // 进程未运行（崩溃或主动停止）：图标复原为绿色播放，编辑器恢复可编辑
    m_runStopBtn->setIcon(recolorStandardIcon(style(), QStyle::SP_MediaPlay,
                                              QColor(0x2E, 0x7D, 0x32)));
    m_runStopBtn->setToolTip(tr("Run script"));
    m_edit->setReadOnly(false);
    m_lastEnabled = false;
  }
}

void ScriptPanel::onStatusChanged(const QString& text) {
  m_status->setText(text);
}

void ScriptPanel::onAvailabilityChanged(bool available) {
  // 仅更新状态行；Run 按钮可用性由运行态主导，不受 Python 是否就绪限制
  m_status->setText(available ? tr("Python: ready") : tr("Python not found"));
}

QString ScriptPanel::source() const {
  // getText(length+1) 返回含结尾 NUL 的缓冲；去掉最后一个 NUL 即正文
  const sptr_t len = m_edit->length();
  QByteArray raw = m_edit->getText(len + 1);
  if (!raw.isEmpty() && raw.endsWith('\0')) raw.chop(1);
  return QString::fromUtf8(raw);
}

void ScriptPanel::setSource(const QString& text) {
  m_edit->setText(text.toUtf8().constData());
}

void ScriptPanel::setWasEnabled(bool e) {
  // 仅恢复意图标志，不自动起进程（按设计：恢复文本，不自动启动）。
  m_lastEnabled = e;
}
