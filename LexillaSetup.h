#ifndef LEXILLASETUP_H
#define LEXILLASETUP_H

// ILexer.h 定义 Scintilla::ILexer5（Scintilla 5 拆分后词法器接口）
#include <ILexer.h>

#include <QString>

// Lexilla 静态接入的极简封装。
// 主工程只编入 Python 词法器（lexers/LexPython.cxx）+ 其依赖的 lexlib
// 子集 + src/Lexilla.cxx（提供 CreateLexer 的目录实现；其内部把全部
// lmXxx 挂进目录，但我们只链接 Python 一个词法器目标文件，未编入的
// lm 符号不会被引用，链接器按需丢弃）。
//
// Scintilla 5 拆分后：编辑控件不再内置词法器，SCI_SETILEXER 需要
// 外部传入 ILexer5*。CreateLexer("python") 即返回该指针。

namespace LexillaSetup {

// 返回 Python 词法器名（"python"），供 SCI_SETILEXER 后
// SCI_SETLEXERLANGUAGE / 文档调试用。失败返回空串（理论上不会失败）。
QString ensurePythonLexer();

// 直接创建 Python 词法器实例（SCI_SETILEXER 的 wParam）。
// 内部调用 CreateLexer("python")；失败返回 nullptr。
Scintilla::ILexer5* createPythonLexer();

}  // namespace LexillaSetup

#endif  // LEXILLASETUP_H
