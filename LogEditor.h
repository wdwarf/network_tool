#ifndef LOGEDITOR_H
#define LOGEDITOR_H

#include <QColor>
#include <QFont>
#include <QString>
#include <QVector>

#include <ScintillaEdit.h>

typedef intptr_t sptr_t;

// "能记日志的编辑器"：Scintilla 控件 + 按级别着色/行数上限/暂停缓冲/
// 查找。工具栏（LogFindBar）不在此类中，由 LogView 组合。
class LogEditor : public ScintillaEdit {
  Q_OBJECT
public:
  enum class LogLevel { Trace, Debug, Info, Warn, Error, Fatal };

  explicit LogEditor(QWidget* parent = nullptr);

  void appendLog(LogLevel level, const QString& text);   // GUI thread only

  void setMaxLineCount(int lines);
  int  maxLineCount() const;
  void setAutoScroll(bool on);
  bool autoScroll() const;
  void setWordWrap(bool on);
  void setTimestampEnabled(bool on);
  bool timestampEnabled() const;
  void setColorForLevel(LogLevel lv, const QColor& c);
  QColor colorForLevel(LogLevel lv) const;
  void setEditorFont(const QFont& f);

  int  lineCount() const;  // content lines (excludes trailing empty line)

  void clear();
  void setPaused(bool paused);
  bool isPaused() const;

  // 查找：textChanged 时调 applyFind（高亮全部并跳到首个匹配，空串
  // 只清高亮）；Next/Prev 按钮调 findNext/findPrev（环绕，无匹配返回
  // false 且不动光标）。
  void applyFind(const QString& text);
  bool findNext(const QString& needle);
  bool findPrev(const QString& needle);

signals:
  void pausedChanged(bool paused);

private:
  void setupEditor();
  void applyStyles();
  void applyPalette();
  void appendToEditor(LogLevel level, const QString& text);
  void trimIfNeeded();
  bool isAtBottom() const;
  bool findMatch(const QString& needle, bool forward, sptr_t& outStart,
                 sptr_t& outEnd);
  int  sciColor(const QColor& c) const;
  int  styleForLevel(LogLevel lv) const;
  int  contentLineCount() const;
  void changeEvent(QEvent* event) override;

  int  maxLines_ = 5000;
  bool autoScroll_ = true;
  bool paused_ = false;
  bool timestampEnabled_ = true;
  QColor levelColors_[6];
  bool customLevelColors_[6] = {};

  struct BufferedLine { LogLevel level; QString text; };
  QVector<BufferedLine> buffer_;
};

#endif // LOGEDITOR_H
