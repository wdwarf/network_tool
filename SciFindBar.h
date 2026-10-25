#ifndef SCIFINDBAR_H
#define SCIFINDBAR_H

#include <QWidget>

class QCheckBox;
class QLineEdit;
class QPushButton;
class ScintillaEdit;

// Scintilla 查找/替换工具条：与 LogEditor 的 find API 不同，这里直接
// 驱动任意 ScintillaEdit（含可编辑的代码编辑器），支持可选替换区。
// findOnly=true：仅查找（接收/日志类只读框）；
// findOnly=false：查找 + 替换/全部替换 + 区分大小写。
// 快捷键：Enter=下一个，Shift+Enter=上一个，Esc=隐藏本条。
class SciFindBar : public QWidget {
  Q_OBJECT
 public:
  explicit SciFindBar(bool findOnly, QWidget* parent = nullptr);

  // 绑定目标编辑器（不接管所有权）。每次显示前都应重新绑定。
  void attach(ScintillaEdit* editor);
  // 用当前选中文本预填查找框（选中为单行时）
  void prefillFromSelection();

 signals:
  // 用户按 Esc 或点击关闭：宿主应隐藏本条
  void closeRequested();

 public slots:
  // 聚焦查找框并全选，便于直接输入覆盖
  void activate();

 private slots:
  void onNext();
  void onPrev();
  void onReplace();
  void onReplaceAll();

 private:
  bool eventFilter(QObject* obj, QEvent* ev) override;

  ScintillaEdit* m_editor = nullptr;
  QLineEdit* m_findEdit = nullptr;
  QLineEdit* m_replaceEdit = nullptr;
  QPushButton* m_prevBtn = nullptr;
  QPushButton* m_nextBtn = nullptr;
  QPushButton* m_replaceBtn = nullptr;
  QPushButton* m_replaceAllBtn = nullptr;
  QCheckBox* m_caseBox = nullptr;
  bool m_findOnly;
};

#endif  // SCIFINDBAR_H
