#include "LexillaSetup.h"

// 只编入 Python 词法器：不走 CreateLexer（其目录初始化 AddEachLexer
// 会引用全部 lmXxx 符号，未编入的成为空指针，遍历目录即崩溃），改为
// 直接 extern 引用 LexPython.cxx 里的 lmPython，用 LexerModule::Create
// 创建实例。
// ILexer.h 定义 Scintilla::ILexer5（LexerModule.h 的返回类型依赖它）。
#include <ILexer.h>
#include "LexerModule.h"

namespace LexillaSetup {

// LexPython.cxx 定义的全局词法器模块 lmPython（"using namespace
// Lexilla" 后定义，位于全局命名空间）—— extern 声明必须放在任何
// 命名空间之外，这里先声明再用。
}  // namespace LexillaSetup

extern "C++" const ::Lexilla::LexerModule lmPython;

namespace LexillaSetup {

QString ensurePythonLexer() { return QString::fromLatin1("python"); }

Scintilla::ILexer5* createPythonLexer() {
  // LexerModule::Create() 返回 ILexer5*；失败返回 nullptr
  return ::lmPython.Create();
}

}  // namespace LexillaSetup
