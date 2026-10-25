#include "LogEditor.h"

#include <Scintilla.h>

#include <QDateTime>
#include <QEvent>
#include <QPalette>
#include <QScrollBar>

LogEditor::LogEditor(QWidget* parent) : ScintillaEdit(parent) {
  levelColors_[static_cast<int>(LogLevel::Trace)] = QColor(160, 160, 160);
  levelColors_[static_cast<int>(LogLevel::Debug)] = QColor(0, 160, 0);
  levelColors_[static_cast<int>(LogLevel::Info)] =
      palette().color(QPalette::Text);
  levelColors_[static_cast<int>(LogLevel::Warn)] = QColor(180, 140, 0);
  levelColors_[static_cast<int>(LogLevel::Error)] = QColor(200, 0, 0);
  levelColors_[static_cast<int>(LogLevel::Fatal)] = QColor(200, 0, 200);
  this->setFrameShape(QFrame::Shape::StyledPanel);
  setupEditor();
}

void LogEditor::setupEditor() {
  send(SCI_SETREADONLY, 1);
  send(SCI_SETMARGINWIDTHN, 0, 0);
  send(SCI_SETMARGINWIDTHN, 1, 0);
  send(SCI_SETWRAPMODE, SC_WRAP_NONE);
  // Make the horizontal scrollbar appear only when a line overflows the view:
  // shrink the assumed document width and let tracking grow it to the widest
  // visible line. Without tracking the default 2000px width keeps the bar on.
  send(SCI_SETSCROLLWIDTH, 1);
  send(SCI_SETSCROLLWIDTHTRACKING, 1);
  send(SCI_INDICSETSTYLE, 0, INDIC_ROUNDBOX);
  send(SCI_INDICSETFORE, 0, sciColor(QColor(255, 255, 0)));
  send(SCI_INDICSETALPHA, 0, 100);
  applyStyles();
}

void LogEditor::applyStyles() {
  QByteArray fontName = QFont("Consolas").family().toUtf8();
  send(SCI_STYLESETFONT, STYLE_DEFAULT,
       reinterpret_cast<sptr_t>(fontName.constData()));
  send(SCI_STYLESETSIZE, STYLE_DEFAULT, 9);
  send(SCI_STYLECLEARALL);
  for (int i = 0; i < 6; ++i) {
    if (i == static_cast<int>(LogLevel::Info) && !customLevelColors_[i])
      levelColors_[i] = palette().color(QPalette::Text);
    send(SCI_STYLESETFORE, styleForLevel(static_cast<LogLevel>(i)),
         sciColor(levelColors_[i]));
  }
  applyPalette();
}

void LogEditor::applyPalette() {
  const QPalette current = palette();
  const QColor background = current.color(QPalette::Base);
  const QColor text = current.color(QPalette::Text);
  QColor selectionBackground =
      current.color(QPalette::Active, QPalette::Highlight);
  QColor selectionText =
      current.color(QPalette::Active, QPalette::HighlightedText);

  // Some platform palettes provide a highlighted-text color that is too
  // close to the highlight background for custom editor widgets.
  if (!selectionBackground.isValid() || selectionBackground == background) {
    selectionBackground = background.lightness() < 128 ? QColor(80, 120, 180)
                                                       : QColor(180, 210, 245);
  }
  if (!selectionText.isValid() ||
      qAbs(selectionText.lightness() - selectionBackground.lightness()) < 80) {
    selectionText =
        selectionBackground.lightness() < 128 ? Qt::white : Qt::black;
  }

  send(SCI_STYLESETBACK, STYLE_DEFAULT, sciColor(background));
  send(SCI_STYLESETFORE, STYLE_DEFAULT, sciColor(text));
  send(SCI_STYLESETBACK, STYLE_LINENUMBER, sciColor(background));
  send(SCI_SETCARETFORE, sciColor(text));
  send(SCI_SETSELALPHA, 255, 0);
  // Draw the selection background below glyphs. With an opaque selection
  // background, the over-text layer hides the log level foreground colors.
  send(SCI_SETSELECTIONLAYER, SC_LAYER_BASE);
  send(SCI_SETSELBACK, 1, sciColor(selectionBackground));
  send(SCI_SETSELFORE, 1, sciColor(selectionText));
  send(SCI_SETCARETLINEBACK, sciColor(background));

  // SCI_STYLECLEARALL copies the default style at the time it is called.
  // Set the background on every log style explicitly so existing styles do
  // not retain Scintilla's default white background.
  for (int i = 0; i < 6; ++i) {
    send(SCI_STYLESETBACK, styleForLevel(static_cast<LogLevel>(i)),
         sciColor(background));
  }
}

int LogEditor::sciColor(const QColor& c) const {
  return (c.red() & 0xff) | ((c.green() & 0xff) << 8) |
         ((c.blue() & 0xff) << 16);
}

int LogEditor::styleForLevel(LogLevel lv) const {
  return 1 + static_cast<int>(lv);
}

int LogEditor::contentLineCount() const {
  sptr_t n = send(SCI_GETLINECOUNT);
  return n > 0 ? static_cast<int>(n - 1) : 0;
}

int LogEditor::lineCount() const { return contentLineCount(); }

void LogEditor::clear() {
  buffer_.clear();
  bool wasRO = send(SCI_GETREADONLY);
  if (wasRO) send(SCI_SETREADONLY, false);
  send(SCI_CLEARALL);
  if (wasRO) send(SCI_SETREADONLY, true);
}

void LogEditor::setMaxLineCount(int lines) {
  maxLines_ = lines;
  if (paused_) {
    while (maxLines_ > 0 && buffer_.size() > maxLines_) buffer_.removeFirst();
  }
  trimIfNeeded();
}
int LogEditor::maxLineCount() const { return maxLines_; }
void LogEditor::setAutoScroll(bool on) { autoScroll_ = on; }
bool LogEditor::autoScroll() const { return autoScroll_; }
void LogEditor::setWordWrap(bool on) { send(SCI_SETWRAPMODE, on ? 1 : 0); }
void LogEditor::setTimestampEnabled(bool on) { timestampEnabled_ = on; }
bool LogEditor::timestampEnabled() const { return timestampEnabled_; }

void LogEditor::setColorForLevel(LogLevel lv, const QColor& c) {
  const int index = static_cast<int>(lv);
  levelColors_[index] = c;
  customLevelColors_[index] = true;
  send(SCI_STYLESETFORE, styleForLevel(lv), sciColor(c));
}
QColor LogEditor::colorForLevel(LogLevel lv) const {
  return levelColors_[static_cast<int>(lv)];
}

void LogEditor::setEditorFont(const QFont& f) {
  QByteArray fam = f.family().toUtf8();
  send(SCI_STYLESETFONT, STYLE_DEFAULT,
       reinterpret_cast<sptr_t>(fam.constData()));
  send(SCI_STYLESETSIZE, STYLE_DEFAULT, f.pointSize() > 0 ? f.pointSize() : 9);
  send(SCI_STYLECLEARALL);
  for (int i = 0; i < 6; ++i)
    send(SCI_STYLESETFORE, styleForLevel(static_cast<LogLevel>(i)),
         sciColor(levelColors_[i]));
}

void LogEditor::changeEvent(QEvent* event) {
  ScintillaEdit::changeEvent(event);
  if (event->type() == QEvent::PaletteChange ||
      event->type() == QEvent::ApplicationPaletteChange) {
    applyStyles();
  }
}

bool LogEditor::isPaused() const { return paused_; }

void LogEditor::setPaused(bool paused) {
  if (paused_ == paused) return;
  paused_ = paused;
  emit pausedChanged(paused);
  if (!paused_) {
    // Flushing appends to the editor; emit nothing per line.
    for (const auto& bl : buffer_) appendToEditor(bl.level, bl.text);
    buffer_.clear();
  }
}

void LogEditor::appendLog(LogLevel level, const QString& text) {
  if (paused_) {
    buffer_.append({level, text});
    if (maxLines_ > 0) {
      while (buffer_.size() > maxLines_) buffer_.removeFirst();
    }
    return;
  }
  appendToEditor(level, text);
}

void LogEditor::appendToEditor(LogLevel level, const QString& text) {
  QByteArray bytes;
  if (timestampEnabled_)
    bytes = ("[" +
             QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz") +
             "] " + text + "\n")
                .toUtf8();
  else
    bytes = (text + "\n").toUtf8();

  bool wasRO = send(SCI_GETREADONLY);
  if (wasRO) send(SCI_SETREADONLY, false);

  sptr_t oldLen = send(SCI_GETTEXTLENGTH);
  // bool atBottom = isAtBottom();
  send(SCI_APPENDTEXT, static_cast<uptr_t>(bytes.size()),
       reinterpret_cast<sptr_t>(bytes.constData()));
  send(SCI_STARTSTYLING, static_cast<uptr_t>(oldLen), 0xff);
  send(SCI_SETSTYLING, static_cast<uptr_t>(bytes.size()),
       static_cast<sptr_t>(styleForLevel(level)));

  trimIfNeeded();

  if (wasRO) send(SCI_SETREADONLY, true);

  // if (autoScroll_ && atBottom) send(SCI_GOTOPOS, send(SCI_GETTEXTLENGTH));
  if (autoScroll_) {
    auto vScroll = verticalScrollBar();
    vScroll->setValue(vScroll->maximum());
  }
}
void LogEditor::trimIfNeeded() {
  if (maxLines_ <= 0) return;
  int count = contentLineCount();
  if (count > maxLines_) {
    int delLines = count - maxLines_;
    sptr_t endPos = send(SCI_POSITIONFROMLINE, static_cast<uptr_t>(delLines));
    if (endPos > 0) {
      bool wasRO = send(SCI_GETREADONLY);
      if (wasRO) send(SCI_SETREADONLY, false);
      send(SCI_DELETERANGE, 0, static_cast<sptr_t>(endPos));
      if (wasRO) send(SCI_SETREADONLY, true);
    }
  }
}
bool LogEditor::isAtBottom() const {
  sptr_t firstVisible = send(SCI_GETFIRSTVISIBLELINE);
  sptr_t linesOnScreen = send(SCI_LINESONSCREEN);
  sptr_t lineCount = send(SCI_GETLINECOUNT);
  // Map the last document line (lineCount-1) into a visible (wrapped) line.
  sptr_t lastVisibleLine = send(SCI_VISIBLEFROMDOCLINE, lineCount - 1);
  return lastVisibleLine < firstVisible + linesOnScreen;
}

void LogEditor::applyFind(const QString& text) {
  // Clear any previous indicator ranges over the whole document.
  sptr_t docLen = send(SCI_GETTEXTLENGTH);
  send(SCI_SETINDICATORCURRENT, 0);
  send(SCI_INDICATORCLEARRANGE, 0, static_cast<sptr_t>(docLen));

  if (text.isEmpty()) return;

  QByteArray needleBytes = text.toUtf8();
  if (needleBytes.isEmpty()) return;

  Sci_TextToFindFull ft{};
  ft.chrg.cpMin = 0;
  ft.chrg.cpMax = docLen;
  ft.lpstrText = needleBytes.constData();

  while (send(SCI_FINDTEXTFULL, 0, reinterpret_cast<sptr_t>(&ft)) != -1) {
    sptr_t start = ft.chrgText.cpMin;
    sptr_t len = ft.chrgText.cpMax - ft.chrgText.cpMin;
    send(SCI_INDICATORFILLRANGE, static_cast<uptr_t>(start),
         static_cast<sptr_t>(len));
    ft.chrg.cpMin = ft.chrgText.cpMax;
  }

  // Jump caret to first match so Next continues from there.
  ft.chrg.cpMin = 0;
  if (send(SCI_FINDTEXTFULL, 0, reinterpret_cast<sptr_t>(&ft)) != -1) {
    send(SCI_GOTOPOS, static_cast<uptr_t>(ft.chrgText.cpMin));
  }
}

bool LogEditor::findNext(const QString& needle) {
  sptr_t start, end;
  if (!findMatch(needle, true, start, end)) return false;
  send(SCI_SETSELECTION, static_cast<uptr_t>(start),
       static_cast<sptr_t>(end));  // caret at match start
  return true;
}

bool LogEditor::findPrev(const QString& needle) {
  sptr_t start, end;
  if (!findMatch(needle, false, start, end)) return false;
  send(SCI_SETSELECTION, static_cast<uptr_t>(start),
       static_cast<sptr_t>(end));  // caret at match start
  return true;
}

bool LogEditor::findMatch(const QString& needle, bool forward, sptr_t& outStart,
                          sptr_t& outEnd) {
  QByteArray bytes = needle.toUtf8();
  if (bytes.isEmpty()) return false;
  sptr_t docLen = send(SCI_GETTEXTLENGTH);
  if (docLen == 0) return false;
  sptr_t caret = send(SCI_GETCURRENTPOS);

  send(SCI_SETSEARCHFLAGS, 0);
  uptr_t len = static_cast<uptr_t>(bytes.size());
  sptr_t textPtr = reinterpret_cast<sptr_t>(bytes.constData());

  // Search within [a, b]; if a > b the search runs backward (Scintilla rule).
  auto search = [&](sptr_t a, sptr_t b) {
    send(SCI_SETTARGETRANGE, static_cast<uptr_t>(a), static_cast<uptr_t>(b));
    return send(SCI_SEARCHINTARGET, len, textPtr);
  };

  sptr_t pos;
  if (forward) {
    // Start just past the caret so we advance past the current match.
    sptr_t start = caret + 1;
    if (start > docLen) start = docLen;
    pos = search(start, docLen);
    if (pos == -1) pos = search(0, docLen);  // wrap around
  } else {
    sptr_t start = caret - 1;
    if (start < 0) start = 0;
    pos = search(start, 0);                  // backward toward 0
    if (pos == -1) pos = search(docLen, 0);  // wrap around from the end
  }
  if (pos == -1) return false;

  outStart = pos;
  outEnd = send(SCI_GETTARGETEND);
  return true;
}
