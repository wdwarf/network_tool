#ifndef ELIDEDLABEL_H
#define ELIDEDLABEL_H

#include <QLabel>

// 超出可用宽度时自动省略（默认右侧 …）的状态/提示标签，完整文本作为 ToolTip
// 显示。 实现要点：保持 QLabel 的文本为完整内容，使 sizeHint
// 始终为完整文本宽度； 仅重写 minimumSizeHint 让布局可压缩、重写 paintEvent
// 按当前宽度绘制省略后的文本。 这样可避免“设短文本→sizeHint
// 变小→布局进一步压缩→再省略”的反馈塌缩，
// 也不会因完整文本宽度撑大窗口的最小尺寸而阻碍缩放。
class ElidedLabel : public QLabel {
 public:
  explicit ElidedLabel(QWidget* parent = nullptr);
  explicit ElidedLabel(const QString& text, QWidget* parent = nullptr);

  // QLabel::setText 非虚函数，此处为隐藏重写；仅当通过 ElidedLabel* 调用时生效
  // （相关成员须声明为 ElidedLabel*，而非 QLabel*）。
  void setText(const QString& text);

  QString fullText() const { return m_fullText; }
  void setElideMode(Qt::TextElideMode mode);

  QSize minimumSizeHint() const override;

 protected:
  void paintEvent(QPaintEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;

 private:
  QString m_fullText;
  Qt::TextElideMode m_mode = Qt::ElideRight;
};

#endif  // ELIDEDLABEL_H
