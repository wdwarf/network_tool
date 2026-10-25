#include "SciFindBar.h"

#include "LogEditor.h"

#include <Scintilla.h>
#include <ScintillaEdit.h>

#include <QCheckBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>

namespace {
// 查找高亮用的指示器编号：LogEditor 内部用 0 号，这里避开用 1 号
constexpr sptr_t kHighlightIndicator = 1;

// Scintilla 搜索标志：区分大小写 = SCFIND_MATCHCASE，否则 0（大小写不敏感）
int searchFlags(bool caseSensitive) {
  return caseSensitive ? SCFIND_MATCHCASE : 0;
}

// 判断编辑器当前选中文本是否等于查找串（区分大小写时精确比较，
// 否则大小写不敏感比较），供 Replace 使用
bool selectionMatches(const QString& sel, const QString& needle,
                      bool caseSensitive) {
  if (caseSensitive) return sel == needle;
  return sel.compare(needle, Qt::CaseInsensitive) == 0;
}

// 在任意 ScintillaEdit 上做一次“下一个/上一个”查找（含环绕）。
// LogEditor 的 findNext/findPrev 不带大小写开关，这里统一自己实现：
// 前向从 caret+1 搜到文末，失败再从头搜；后向对称。
// 命中则选中匹配并滚动到可视区，返回 true。
bool findInEditor(ScintillaEdit* ed, const QString& text, bool forward,
                  int flags) {
  const QByteArray bytes = text.toUtf8();
  if (!ed || bytes.isEmpty()) return false;
  const sptr_t docLen = ed->length();
  if (docLen == 0) return false;
  const sptr_t caret = ed->currentPos();
  const auto len = static_cast<uptr_t>(bytes.size());

  // 在 [a,b] 区间内搜索；a>b 表示反向搜索（Scintilla 约定）
  auto search = [&](sptr_t a, sptr_t b) {
    ed->setTargetRange(a, b);
    return ed->searchInTarget(len, bytes.constData());
  };

  sptr_t pos = -1;
  ed->setSearchFlags(flags);
  if (forward) {
    sptr_t start = caret + 1 > docLen ? docLen : caret + 1;
    pos = search(start, docLen);
    if (pos == -1) pos = search(0, docLen);  // 环绕
  } else {
    sptr_t start = caret - 1 < 0 ? 0 : caret - 1;
    pos = search(start, 0);
    if (pos == -1) pos = search(docLen, 0);  // 环绕
  }
  if (pos == -1) return false;

  const sptr_t end = ed->targetEnd();
  ed->setSelection(pos, end);  // 光标落在匹配起点
  ed->scrollCaret();
  return true;
}
}  // namespace

SciFindBar::SciFindBar(bool findOnly, QWidget* parent)
    : QWidget(parent), m_findOnly(findOnly) {
  auto* lay = new QHBoxLayout(this);
  lay->setContentsMargins(4, 2, 4, 2);
  lay->setSpacing(4);

  lay->addWidget(new QLabel(tr("Find:")));
  m_findEdit = new QLineEdit;
  m_findEdit->setMinimumWidth(120);
  m_findEdit->setClearButtonEnabled(true);
  // Esc 关闭、Shift+Enter 上一个：eventFilter 里处理
  m_findEdit->installEventFilter(this);
  lay->addWidget(m_findEdit, 1);

  m_prevBtn = new QPushButton(tr("Previous"));
  lay->addWidget(m_prevBtn);
  m_nextBtn = new QPushButton(tr("Next"));
  lay->addWidget(m_nextBtn);

  if (!m_findOnly) {
    lay->addWidget(new QLabel(tr("Replace with:")));
    m_replaceEdit = new QLineEdit;
    m_replaceEdit->setMinimumWidth(120);
    m_replaceEdit->setClearButtonEnabled(true);
    lay->addWidget(m_replaceEdit, 1);

    m_replaceBtn = new QPushButton(tr("Replace"));
    lay->addWidget(m_replaceBtn);
    m_replaceAllBtn = new QPushButton(tr("Replace All"));
    lay->addWidget(m_replaceAllBtn);
  }

  m_caseBox = new QCheckBox(tr("Match case"));
  lay->addWidget(m_caseBox);
  connect(m_caseBox, &QCheckBox::toggled, this, [this] {
    // 大小写开关变化后重走一次高亮，让颜色与新选项一致
    emit m_findEdit->textChanged(m_findEdit->text());
  });
  auto* closeBtn = new QPushButton(tr("×"));
  closeBtn->setFixedSize(22, 22);
  closeBtn->setToolTip(tr("Close (Esc)"));
  lay->addWidget(closeBtn);

  // 输入即更新高亮/跳转。LogEditor 有 applyFind（高亮全部+跳首个）；
  // 普通 ScintillaEdit（脚本编辑器）没有，这里用指示器手工高亮全部
  // 匹配，并把光标移到首个匹配处。
  connect(m_findEdit, &QLineEdit::textChanged, this, [this] {
    if (!m_editor) return;
    if (auto* log = qobject_cast<LogEditor*>(m_editor)) {
      log->applyFind(m_findEdit->text());
      return;
    }
    // 通用路径：指示器 1 号抹掉旧高亮 → FINDTEXTFULL 扫全文填新范围
    const sptr_t docLen = m_editor->length();
    m_editor->send(SCI_SETINDICATORCURRENT, kHighlightIndicator);
    m_editor->send(SCI_INDICATORCLEARRANGE, 0,
                   static_cast<sptr_t>(docLen));
    const QString text = m_findEdit->text();
    if (text.isEmpty()) return;
    const QByteArray bytes = text.toUtf8();
    if (bytes.isEmpty()) return;

    Sci_TextToFindFull ft{};
    ft.chrg.cpMin = 0;
    ft.chrg.cpMax = docLen;
    ft.lpstrText = bytes.constData();
    const int flags = searchFlags(m_caseBox->isChecked());
    sptr_t first = -1;
    while (m_editor->send(SCI_FINDTEXTFULL, flags,
                          reinterpret_cast<sptr_t>(&ft)) != -1) {
      if (first == -1) first = ft.chrgText.cpMin;
      m_editor->send(SCI_INDICATORFILLRANGE,
                     static_cast<uptr_t>(ft.chrgText.cpMin),
                     static_cast<sptr_t>(ft.chrgText.cpMax -
                                         ft.chrgText.cpMin));
      ft.chrg.cpMin = ft.chrgText.cpMax;
    }
    if (first != -1) m_editor->setSelection(first, first);
  });
  connect(m_findEdit, &QLineEdit::returnPressed, this, &SciFindBar::onNext);
  connect(m_prevBtn, &QPushButton::clicked, this, &SciFindBar::onPrev);
  connect(m_nextBtn, &QPushButton::clicked, this, &SciFindBar::onNext);
  if (!m_findOnly) {
    connect(m_replaceEdit, &QLineEdit::returnPressed, this,
            &SciFindBar::onReplace);
    connect(m_replaceBtn, &QPushButton::clicked, this, &SciFindBar::onReplace);
    connect(m_replaceAllBtn, &QPushButton::clicked, this,
            &SciFindBar::onReplaceAll);
  }
  connect(closeBtn, &QPushButton::clicked, this,
          &SciFindBar::closeRequested);
}

void SciFindBar::attach(ScintillaEdit* editor) {
  m_editor = editor;
  // 通用高亮路径用指示器 1 号（LogEditor 的 applyFind 占用 0 号）：
  // 黄色圆角框 + 半透明填充，与 LogEditor 的样式保持一致
  if (m_editor) {
    m_editor->indicSetStyle(kHighlightIndicator, INDIC_ROUNDBOX);
    m_editor->indicSetFore(kHighlightIndicator,
                           0x00FFFF);  // BGR：黄色
    m_editor->indicSetAlpha(kHighlightIndicator, 100);
  }
}

void SciFindBar::prefillFromSelection() {
  if (!m_editor) return;
  const QByteArray selBytes = m_editor->getSelText();
  const QString sel = QString::fromUtf8(selBytes);
  const int nl = sel.indexOf(QLatin1Char('\n'));
  if (!sel.isEmpty()) m_findEdit->setText(nl >= 0 ? sel.left(nl) : sel);
}

void SciFindBar::activate() {
  prefillFromSelection();
  show();
  m_findEdit->setFocus();
  m_findEdit->selectAll();
}

bool SciFindBar::eventFilter(QObject* obj, QEvent* ev) {
  if (obj == m_findEdit && ev->type() == QEvent::KeyPress) {
    auto* ke = static_cast<QKeyEvent*>(ev);
    if (ke->key() == Qt::Key_Escape) {
      emit closeRequested();
      return true;
    }
    if (ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter) {
      if (ke->modifiers() & Qt::ShiftModifier) {
        onPrev();
      } else {
        onNext();
      }
      return true;
    }
  }
  return QWidget::eventFilter(obj, ev);
}

void SciFindBar::onNext() {
  if (!m_editor) return;
  findInEditor(m_editor, m_findEdit->text(), true,
               searchFlags(m_caseBox->isChecked()));
}

void SciFindBar::onPrev() {
  if (!m_editor) return;
  findInEditor(m_editor, m_findEdit->text(), false,
               searchFlags(m_caseBox->isChecked()));
}

void SciFindBar::onReplace() {
  if (!m_editor || m_findOnly) return;
  const QString needle = m_findEdit->text();
  if (needle.isEmpty()) return;
  const QByteArray rep = m_replaceEdit->text().toUtf8();

  // 若当前正选中一个匹配，替换之并跳到下一个；否则先查找。
  // 大小写不敏感时选中文本与查找串可能大小写不同，同样视为匹配。
  const QString sel = QString::fromUtf8(m_editor->getSelText());
  if (!sel.isEmpty() &&
      selectionMatches(sel, needle, m_caseBox->isChecked()))
    m_editor->replaceSel(rep.constData());

  onNext();  // 替换后跳到下一个匹配
}

void SciFindBar::onReplaceAll() {
  if (!m_editor || m_findOnly) return;
  const QString needle = m_findEdit->text();
  if (needle.isEmpty()) return;
  const QByteArray rep = m_replaceEdit->text().toUtf8();

  // 从头全量替换：每轮 SCI_TARGETWHOLEDOCUMENT 后循环 searchInTarget →
  // replaceTarget。replaceTarget 会把目标区间终点平移到替换文本末尾，
  // 循环天然前进，不会死循环（替换文本比查找串长也安全）。
  m_editor->targetWholeDocument();
  m_editor->setSearchFlags(searchFlags(m_caseBox->isChecked()));
  const QByteArray ndl = needle.toUtf8();
  while (m_editor->searchInTarget(ndl.size(), ndl.constData()) != -1) {
    m_editor->replaceTarget(rep.size(), rep.constData());
  }
}
