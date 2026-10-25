#ifndef SCRIPTPANEL_H
#define SCRIPTPANEL_H

#include <QWidget>

class QPushButton;
class QLabel;
class ScriptBridge;
class SciFindBar;
class ScintillaEdit;

// 实例页底部的脚本区：Scintilla 代码编辑器 + 运行/停止 + 状态行 +
// 查找/替换工具条。collapsible=true（单连接模块）为可折叠小条；
// false（服务器 Script 页签）为常驻填满布局。
// 持有外部传入的 ScriptBridge（由上层创建并拥有）。
class ScriptPanel : public QWidget {
  Q_OBJECT
 public:
  explicit ScriptPanel(ScriptBridge* bridge, bool collapsible = true,
                       QWidget* parent = nullptr);

  QString source() const;
  void setSource(const QString& text);
  bool wasEnabled() const { return m_lastEnabled; }
  void setWasEnabled(bool e);

 signals:
  void sendRequested(const QByteArray& data,
                     const QString& peer);  // 透传 bridge
  void logRequested(const QString& msg);    // 透传 bridge

 private slots:
  void onRunStopClicked();
  void onStatusChanged(const QString& text);
  void onAvailabilityChanged(bool available);
  void onRunningChanged(bool running);

 private:
  void setupEditor();      // Scintilla 基础设置 + Python 词法器/着色
  void applyPythonStyles();  // SCE_P_* 各风格配色与字体
  void applyEditorPalette();
  bool eventFilter(QObject* watched, QEvent* event) override;

  ScriptBridge* m_bridge;
  ScintillaEdit* m_edit;
  SciFindBar* m_findBar;   // 查找/替换条（findOnly=false）
  QPushButton* m_findBtn;  // 显示/隐藏查找条
  QPushButton* m_runStopBtn;  // 运行/停止合并按钮（图标随运行态切换）
  QLabel* m_status;
  bool m_lastEnabled = false;  // 上一次是否处于运行态（持久化用）
};

#endif  // SCRIPTPANEL_H
