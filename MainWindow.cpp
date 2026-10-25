#include "MainWindow.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QIcon>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QSettings>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QKeySequence>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QStyle>
#include <QStyleFactory>
#include <QTabWidget>
#include <QToolBar>
#include <QToolButton>
#include <QSvgRenderer>

#ifdef _WIN32
#include <windows.h>
#include <dwmapi.h>
#endif

#include "BroadcastModule.h"
#include "InstanceContainer.h"
#include "MulticastModule.h"
#include "TcpClientModule.h"
#include "TcpServerModule.h"
#include "UdpClientModule.h"
#include "UdpServerModule.h"

static QIcon themedSvgIcon(const QString& path, const QColor& color) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) return {};

  QByteArray svg = file.readAll();
  svg.replace("currentColor", color.name(QColor::HexRgb).toUtf8());
  QSvgRenderer renderer(svg);
  if (!renderer.isValid()) return {};

  QIcon icon;
  for (const int size : {16, 20, 24, 32}) {
    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    renderer.render(&painter, QRectF(0, 0, size, size));
    icon.addPixmap(pixmap);
  }
  return icon;
}

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
  m_tabs = new QTabWidget;

  // 每个类别一个 InstanceContainer；m_containers 下标与顶层页签一一对应。
  // 各模块自身把 socket 作为值成员持有，互不影响，故可多开。
  m_containers.append(
      new InstanceContainer(tr("Server"), [] { return new TcpServerModule; }));
  m_containers.append(
      new InstanceContainer(tr("Client"), [] { return new TcpClientModule; }));
  m_containers.append(
      new InstanceContainer(tr("Server"), [] { return new UdpServerModule; }));
  m_containers.append(
      new InstanceContainer(tr("Client"), [] { return new UdpClientModule; }));
  m_containers.append(new InstanceContainer(
      tr("Broadcast"), [] { return new BroadcastModule; }));
  m_containers.append(new InstanceContainer(
      tr("Multicast"), [] { return new MulticastModule; }));

  const QString labels[] = {tr("TCP Server"), tr("TCP Client"),
                            tr("UDP Server"), tr("UDP Client"),
                            tr("Broadcast"),  tr("Multicast")};
  for (int i = 0; i < m_containers.size(); ++i)
    m_tabs->addTab(m_containers[i], labels[i]);

  setCentralWidget(m_tabs);
  setWindowTitle(tr("Network Test Tool"));
  resize(800, 480);

  setupActions();
  refreshTitleBarTheme();

  // 打开时自动恢复上次界面状态（默认配置文件不存在则保持默认单实例）
  loadConfig(defaultConfigPath());
}

void MainWindow::addInstanceFor(int category) {
  if (category < 0 || category >= m_containers.size()) return;
  m_containers[category]->addInstance();
  m_tabs->setCurrentIndex(category);  // 切到该类别，便于立即使用新实例
}

void MainWindow::setupActions() {
  // 图标在 refreshStyleIcons() 中按当前 style 统一套用，便于运行期切换 style
  // 后刷新。

  // 6 个共享的“新建实例”动作：同时加入 File>New 子菜单与工具栏 New 弹出菜单
  for (int i = 0; i < m_containers.size(); ++i) {
    auto* a = new QAction(m_tabs->tabText(i), this);
    connect(a, &QAction::triggered, this, [this, i] { addInstanceFor(i); });
    m_newActions.append(a);
  }

  m_closeAct = new QAction(tr("&Close Instance"), this);
  m_closeAct->setShortcut(Qt::CTRL | Qt::Key_W);
  m_closeAct->setToolTip(tr("Close the active instance tab"));
  connect(m_closeAct, &QAction::triggered, this, [this] {
    const int i = m_tabs->currentIndex();
    if (i >= 0) m_containers[i]->closeCurrentInstance();
  });

  m_quitAct = new QAction(tr("&Quit"), this);
  m_quitAct->setShortcut(Qt::CTRL | Qt::Key_Q);
  connect(m_quitAct, &QAction::triggered, this, &QWidget::close);

  // 手动保存/加载界面配置（选择 JSON 文件）
  m_saveCfgAct = new QAction(tr("&Save Config"), this);
  m_saveCfgAct->setShortcut(Qt::CTRL | Qt::Key_S);
  m_saveCfgAct->setToolTip(tr("Save current UI configuration to a JSON file"));
  connect(m_saveCfgAct, &QAction::triggered, this, [this] {
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Save Config"), QStringLiteral("network_tool.json"),
        tr("JSON (*.json);;All files (*)"));
    if (path.isEmpty()) return;
    if (!saveConfig(path))
      QMessageBox::warning(this, tr("Save failed"),
                           tr("Could not write %1").arg(path));
  });

  m_loadCfgAct = new QAction(tr("&Load Config"), this);
  m_loadCfgAct->setShortcut(Qt::CTRL | Qt::Key_O);
  m_loadCfgAct->setToolTip(tr("Load a UI configuration from a JSON file"));
  connect(m_loadCfgAct, &QAction::triggered, this, [this] {
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Load Config"), QString(), tr("JSON (*.json);;All files (*)"));
    if (path.isEmpty()) return;
    if (!loadConfig(path))
      QMessageBox::warning(this, tr("Load failed"),
                           tr("Could not read %1").arg(path));
  });

  // ---- File 菜单 ----
  auto* fileMenu = menuBar()->addMenu(tr("&File"));
  auto* newMenu = fileMenu->addMenu(tr("&New"));
  for (auto* a : m_newActions) newMenu->addAction(a);
  fileMenu->addSeparator();
  fileMenu->addAction(m_closeAct);
  fileMenu->addSeparator();
  fileMenu->addAction(m_saveCfgAct);
  fileMenu->addAction(m_loadCfgAct);
  fileMenu->addSeparator();
  fileMenu->addAction(m_quitAct);

  // ---- 工具栏（先建，供 View 菜单引用）----
  m_toolbar = addToolBar(tr("Main"));
  m_toolbar->setObjectName(QStringLiteral("MainToolBar"));
  m_toolbar->setMovable(false);
  m_toolbar->setVisible(false);

  m_newBtn = new QToolButton;
  m_newBtn->setToolTip(tr("Add a new instance"));
  m_newBtn->setPopupMode(QToolButton::InstantPopup);
  auto* btnMenu = new QMenu(m_newBtn);
  for (auto* a : m_newActions) btnMenu->addAction(a);
  m_newBtn->setMenu(btnMenu);
  m_toolbar->addWidget(m_newBtn);
  m_toolbar->addSeparator();
  m_toolbar->addAction(m_saveCfgAct);
  m_toolbar->addAction(m_loadCfgAct);

  // ---- View 菜单 ----
  auto* viewMenu = menuBar()->addMenu(tr("&View"));
  m_toggleToolbarAct = new QAction(tr("Show &Toolbar"), this);
  m_toggleToolbarAct->setCheckable(true);
  // 默认未勾选，与工具栏默认隐藏一致；实际状态由 loadConfig 恢复
  connect(m_toggleToolbarAct, &QAction::toggled, this, [this](bool on) {
    if (m_toolbar) m_toolbar->setVisible(on);
  });
  viewMenu->addAction(m_toggleToolbarAct);
  viewMenu->addSeparator();

  // 样式切换：切换后由 applyStyle 重设图标，使标准 GUI 图标随 style 实时变化
  auto* styleMenu = viewMenu->addMenu(tr("St&yle"));
  m_styleGroup = new QActionGroup(this);
  const QString styleNames[] = {QStringLiteral("Fusion"),
                                QStringLiteral("Windows"),
                                QStringLiteral("WindowsVista")};
  for (const QString& name : QStyleFactory::keys()) {
    auto* a = new QAction(name, this);
    a->setCheckable(true);
    a->setActionGroup(m_styleGroup);
    connect(a, &QAction::triggered, this, [this, name] { applyStyle(name); });
    styleMenu->addAction(a);
  }
  // 勾选与当前已应用 style 匹配的项（构造期 m_styleName 为空 → 全不选）
  for (auto* a : m_styleGroup->actions())
    a->setChecked(a->text() == m_styleName);

  // ---- Help 菜单 ----
  auto* helpMenu = menuBar()->addMenu(tr("&Help"));
  auto* aboutAct = new QAction(tr("&About"), this);
  m_aboutAct = aboutAct;
  connect(aboutAct, &QAction::triggered, this, [this] {
    QMessageBox::about(this, tr("About"),
                       tr("<b>Network Test Tool</b><br>Multi-instance TCP/UDP "
                          "testing.<br>Qt %1")
                           .arg(QLatin1String(qVersion())));
  });
  helpMenu->addAction(aboutAct);

  // 所有动作/控件已就绪，按当前 style 统一套用 GUI 图标
  // （运行期切 style 后由 applyStyle 再调一次以刷新）
  refreshStyleIcons();
}

void MainWindow::refreshStyleIcons() {
  // 图标源从 QStyle::standardIcon 换为自定义 SVG（currentColor 随主题变色）。
  // 类别图标供 Tab 页签 / 新建实例菜单 / 工具栏弹出菜单共用；命令图标供
  // 菜单栏与工具栏命令按钮使用。切换 style 后重调本函数即可按新调色板取色。

  const bool darkTheme = palette().color(QPalette::Base).lightness() < 128;
  const QColor iconColor = darkTheme ? Qt::white : Qt::black;
  const QString catPaths[] = {
      QStringLiteral(":/resources/cat-tcp-server.svg"),
      QStringLiteral(":/resources/cat-tcp-client.svg"),
      QStringLiteral(":/resources/cat-udp-server.svg"),
      QStringLiteral(":/resources/cat-udp-client.svg"),
      QStringLiteral(":/resources/cat-broadcast.svg"),
      QStringLiteral(":/resources/cat-multicast.svg")};
  m_categoryIcons.clear();
  for (const QString& p : catPaths)
    m_categoryIcons.append(themedSvgIcon(p, iconColor));

  const QIcon newIcon =
      themedSvgIcon(QStringLiteral(":/resources/cmd-new.svg"), iconColor);
  const QIcon closeIcon =
      themedSvgIcon(QStringLiteral(":/resources/cmd-close.svg"), iconColor);
  const QIcon quitIcon =
      themedSvgIcon(QStringLiteral(":/resources/cmd-quit.svg"), iconColor);
  const QIcon saveIcon =
      themedSvgIcon(QStringLiteral(":/resources/cmd-save.svg"), iconColor);
  const QIcon loadIcon =
      themedSvgIcon(QStringLiteral(":/resources/cmd-load.svg"), iconColor);
  const QIcon aboutIcon =
      themedSvgIcon(QStringLiteral(":/resources/cmd-about.svg"), iconColor);

  // 6 个“新建实例”动作各自配对应类别图标（不再共享同一个 newIcon）
  for (int i = 0; i < m_newActions.size() && i < m_categoryIcons.size(); ++i)
    m_newActions[i]->setIcon(m_categoryIcons[i]);
  if (m_closeAct) m_closeAct->setIcon(closeIcon);
  if (m_quitAct) m_quitAct->setIcon(quitIcon);
  if (m_saveCfgAct) m_saveCfgAct->setIcon(saveIcon);
  if (m_loadCfgAct) m_loadCfgAct->setIcon(loadIcon);
  if (m_aboutAct) m_aboutAct->setIcon(aboutIcon);
  if (m_newBtn) m_newBtn->setIcon(newIcon);

  // Tab 页签配类别图标；切 style 后重设以触发 currentColor 重新取色
  for (int i = 0; i < m_tabs->count() && i < m_categoryIcons.size(); ++i)
    m_tabs->setTabIcon(i, m_categoryIcons[i]);
}

void MainWindow::changeEvent(QEvent* e) {
  QMainWindow::changeEvent(e);
  if (e->type() == QEvent::PaletteChange ||
      e->type() == QEvent::ApplicationPaletteChange) {
    refreshStyleIcons();
    refreshTitleBarTheme();
  }
}

void MainWindow::refreshTitleBarTheme() {
#ifdef _WIN32
  const QSettings personalize(
      QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize"),
      QSettings::NativeFormat);
  const bool dark =
      personalize.value(QStringLiteral("AppsUseLightTheme"), 1).toInt() == 0;
  const HWND hwnd = reinterpret_cast<HWND>(winId());

  const DWORD value = dark ? 1u : 0u;
  // Windows 10 1809+ uses attribute 19. Some builds expose the older value
  // 20 instead; never set both because the second call can override the first.
  if (FAILED(DwmSetWindowAttribute(hwnd, 19, &value, sizeof(value))))
    DwmSetWindowAttribute(hwnd, 20, &value, sizeof(value));
#endif
}

bool MainWindow::nativeEvent(const QByteArray& eventType, void* message,
                             qintptr* result) {
#ifdef _WIN32
  if (eventType == "windows_generic_MSG" && message) {
    const MSG* msg = static_cast<const MSG*>(message);
    if (msg->message == WM_SETTINGCHANGE ||
        msg->message == WM_THEMECHANGED ||
        msg->message == WM_DWMCOLORIZATIONCOLORCHANGED) {
      refreshTitleBarTheme();
    }
  }
#else
  Q_UNUSED(eventType);
  Q_UNUSED(message);
  Q_UNUSED(result);
#endif
  return QMainWindow::nativeEvent(eventType, message, result);
}

void MainWindow::applyStyle(const QString& name) {
  if (QStyle* s = QStyleFactory::create(name)) {
    m_styleName = name;
    QApplication::setStyle(s);
    refreshStyleIcons();  // 让标准 GUI 图标随新 style 实时刷新
    // 同步样式菜单勾选
    if (m_styleGroup) {
      for (auto* a : m_styleGroup->actions()) a->setChecked(a->text() == name);
    }
  }
}

QJsonObject MainWindow::serialize() const {
  QJsonObject o;
  o["version"] = 1;
  o["currentCategory"] = m_tabs->currentIndex();
  o["width"] = width();
  o["height"] = height();
  o["toolbarVisible"] =
      m_toggleToolbarAct ? m_toggleToolbarAct->isChecked() : true;
  o["style"] = m_styleName;

  // 6 个类别各自的实例列表（名字 + 模块状态）
  QJsonArray cats;
  for (InstanceContainer* c : m_containers) cats.append(c->saveState());
  o["categories"] = cats;
  return o;
}

void MainWindow::deserialize(const QJsonObject& obj) {
  const QJsonArray cats = obj.value("categories").toArray();
  for (int i = 0; i < m_containers.size() && i < cats.size(); ++i)
    m_containers[i]->loadState(cats.at(i).toObject());

  if (obj.contains("width") && obj.contains("height"))
    resize(obj.value("width").toInt(width()),
           obj.value("height").toInt(height()));
  if (obj.contains("toolbarVisible") && m_toggleToolbarAct) {
    // 经由动作切换，保证菜单勾选状态与工具栏可见性一致；
    // setChecked 仅在状态真正变化时发射 toggled，故先直接同步工具栏，
    // 再用带信号的方式置位动作，二者必有一致。
    const bool vis = obj.value("toolbarVisible").toBool(true);
    if (m_toolbar) m_toolbar->setVisible(vis);
    m_toggleToolbarAct->setChecked(vis);
  }
  if (obj.contains("style") && !obj.value("style").toString().isEmpty())
    applyStyle(obj.value("style").toString());
  const int cur = obj.value("currentCategory").toInt(-1);
  if (cur >= 0 && cur < m_containers.size()) m_tabs->setCurrentIndex(cur);
}

QString MainWindow::defaultConfigPath() const {
  // 默认配置文件放在可执行文件目录下，便于查找与手动编辑
  return QDir(QCoreApplication::applicationDirPath())
      .filePath(QStringLiteral("network_tool.json"));
}

bool MainWindow::saveConfig(const QString& path) {
  QFile f(path);
  if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
  const QByteArray data =
      QJsonDocument(serialize()).toJson(QJsonDocument::Indented);
  return f.write(data) == data.size();
}

bool MainWindow::loadConfig(const QString& path) {
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly)) return false;
  QJsonParseError err;
  const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
  if (err.error != QJsonParseError::NoError || !doc.isObject()) return false;
  deserialize(doc.object());
  return true;
}

void MainWindow::closeEvent(QCloseEvent* e) {
  // 关闭时静默自动保存当前界面状态
  saveConfig(defaultConfigPath());
  QMainWindow::closeEvent(e);
}
