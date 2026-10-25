#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QIcon>
#include <QMainWindow>
#include <QTranslator>
#include <QVector>

class QTabWidget;
class QToolBar;
class QAction;
class QActionGroup;
class QToolButton;
class QCloseEvent;
class QEvent;
class QJsonObject;
class InstanceContainer;

// 主窗口：顶层 6 个固定类别页签，每类一个 InstanceContainer 承载多个实例；
// 附菜单栏与工具栏，提供“新建实例 / 关闭实例 / 保存与加载配置 / 退出 /
// 切换工具栏 / 关于”。关闭时自动保存界面状态，打开时自动恢复（JSON 文件）。
class MainWindow : public QMainWindow {
  Q_OBJECT
 public:
  explicit MainWindow(QWidget* parent = nullptr);

  // 保存/加载界面配置到指定 JSON 文件（File 菜单“Save/Load Config”内部亦调用）
  bool saveConfig(const QString& path);
  bool loadConfig(const QString& path);

 protected:
  void closeEvent(QCloseEvent* e) override;
  void changeEvent(QEvent* e) override;
  bool nativeEvent(const QByteArray& eventType, void* message,
                   qintptr* result) override;

 private:
  void setupActions();       // 构建菜单栏与工具栏
  void refreshStyleIcons();  // 按当前 style 重新生成并套用所有 GUI 图标
  void refreshTitleBarTheme();
  void applyStyle(const QString& name);  // 切换应用 style 并刷新图标
  void addInstanceFor(int category);     // 在指定类别新增实例并切到该类别

  // 界面配置（JSON）的序列化与文件读写
  QJsonObject serialize() const;
  void deserialize(const QJsonObject& obj);
  QString defaultConfigPath() const;  // 自动保存/恢复所用的默认文件路径

  QTabWidget* m_tabs = nullptr;
  QVector<InstanceContainer*> m_containers;  // 下标与顶层页签一致
  QToolBar* m_toolbar = nullptr;
  QAction* m_toggleToolbarAct =
      nullptr;  // View 菜单“显示工具栏”，便于加载时同步状态

  // 需要随 style 重设图标的动作/控件
  QVector<QAction*> m_newActions;  // 6 个“新建实例”动作，各自配对应类别图标
  QAction* m_closeAct = nullptr;
  QAction* m_quitAct = nullptr;
  QAction* m_saveCfgAct = nullptr;
  QAction* m_loadCfgAct = nullptr;
  QAction* m_aboutAct = nullptr;
  QToolButton* m_newBtn = nullptr;       // 工具栏“新建”弹出按钮
  QVector<QIcon> m_categoryIcons;        // 6 个类别图标，Tab/新建菜单/工具栏共用
  QActionGroup* m_styleGroup = nullptr;  // View 菜单样式切换组
  QString m_styleName;                   // 当前已应用的 style 名（空=平台默认）
};

#endif  // MAINWINDOW_H
