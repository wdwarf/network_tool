#include "ElidedLabel.h"

#include <QPaintEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QStyleOption>

ElidedLabel::ElidedLabel(QWidget* parent) : QLabel(parent) {
  setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
  setText(QString());
}

ElidedLabel::ElidedLabel(const QString& text, QWidget* parent)
    : QLabel(parent) {
  setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
  setText(text);
}

void ElidedLabel::setText(const QString& text) {
  m_fullText = text;
  QLabel::setText(text);  // 保持 QLabel 文本，使 sizeHint 始终反映完整宽度
  setToolTip(text);       // 悬停显示完整文本
  update();               // 触发重绘以重新省略
}

void ElidedLabel::setElideMode(Qt::TextElideMode mode) {
  m_mode = mode;
  update();
}

QSize ElidedLabel::minimumSizeHint() const {
  // 允许布局把本标签压缩到远小于完整文本宽度，从而触发省略
  const int w = fontMetrics().horizontalAdvance(QStringLiteral("…")) + 8;
  return QSize(w, QLabel::minimumSizeHint().height());
}

void ElidedLabel::resizeEvent(QResizeEvent* event) {
  QLabel::resizeEvent(event);
  update();  // 宽度变化后重新绘制以重新省略
}

void ElidedLabel::paintEvent(QPaintEvent* event) {
  Q_UNUSED(event);
  QStyleOption opt;
  opt.initFrom(this);
  QPainter p(this);
  p.setPen(opt.palette.windowText().color());
  const QRect r = contentsRect();
  const QString elided =
      fontMetrics().elidedText(m_fullText, m_mode, r.width());
  p.drawText(r, alignment() | Qt::TextSingleLine, elided);
}
