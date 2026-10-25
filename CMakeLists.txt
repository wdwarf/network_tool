cmake_minimum_required(VERSION 3.10)
project(network_tool LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# 供 IDE/clangd 读取 Qt 头路径（生成 build/compile_commands.json）
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 选择 Qt 主版本：优先 Qt6，找不到再退回 Qt5。
# 用户可用 -DQT_VERSION_MAJOR=5 强制指定（例如本机用 Qt 5.14.2）。
# 用带版本号的导入目标 Qt${QT_VERSION_MAJOR}::Core 等，兼容 Qt 5.14+ 与 Qt6
# （无版本的 Qt::Core 目标仅在 Qt 5.15+ 才提供，故不采用）。
if(NOT DEFINED QT_VERSION_MAJOR)
    find_package(Qt6 COMPONENTS Core QUIET)
    if(Qt6_FOUND)
        set(QT_VERSION_MAJOR 6)
    else()
        set(QT_VERSION_MAJOR 5)
    endif()
    set(QT_VERSION_MAJOR "${QT_VERSION_MAJOR}" CACHE STRING "Qt major version (5 or 6)" FORCE)
endif()

# 默认 Qt 路径仅在本机 Windows 且选 Qt5 时作为兜底；
# 选 Qt6 或 Linux 用户应通过 -DCMAKE_PREFIX_PATH 指定 Qt 安装位置。
if(WIN32 AND QT_VERSION_MAJOR EQUAL 5 AND NOT DEFINED CMAKE_PREFIX_PATH)
    set(CMAKE_PREFIX_PATH "D:/Qt/Qt5.14.2/5.14.2/mingw73_64")
endif()

# 本工程未实际使用 OpenGL（无 GL/QOpenGL 头引用），将其从 REQUIRED 中移除，
# 避免 Linux 上未安装 OpenGL 开发包时 configure 失败；保留可选查找以便将来扩展。
# Svg 为 REQUIRED：MainWindow 图标改用 SVG（currentColor 随主题变色），需 Qt5Svg 插件支持。
find_package(Qt${QT_VERSION_MAJOR} COMPONENTS Core Gui Widgets Network Svg REQUIRED)
find_package(Qt${QT_VERSION_MAJOR} COMPONENTS OpenGL QUIET)

# 翻译：配置期用 lrelease 把 resources/network_tool.ts 编译为 resources/network_tool.qm，
# 再经 resources/translations.qrc（AUTORCC）嵌入可执行文件，运行期以 :/network_tool.qm 加载。
# lrelease 找不到或失败均不致命——resources/ 下已存在预生成 .qm，构建可继续。
# Qt6 的 lrelease 命令行兼容 Qt5 的 -qm 用法，故此处无需分支。
find_program(LRELEASE_EXE lrelease
    HINTS "${Qt${QT_VERSION_MAJOR}_DIR}/../../../bin" "${Qt${QT_VERSION_MAJOR}_DIR}/bin" "$ENV{QTDIR}/bin")
if(LRELEASE_EXE)
    execute_process(
        COMMAND "${LRELEASE_EXE}" "${CMAKE_CURRENT_SOURCE_DIR}/resources/network_tool.ts"
                -qm "${CMAKE_CURRENT_SOURCE_DIR}/resources/network_tool.qm"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        RESULT_VARIABLE _lrelease_result)
    if(NOT _lrelease_result EQUAL 0)
        message(WARNING "lrelease 退出码非 0，使用已有的 resources/network_tool.qm（若有）")
    endif()
else()
    message(WARNING "未找到 lrelease，请确保 resources/ 下的 network_tool.qm 是最新的")
endif()

set(SOURCES
    main.cpp
    MainWindow.cpp
    InstanceContainer.cpp
    NetModule.cpp
    LogEditor.cpp
    SciFindBar.cpp
    LexillaSetup.cpp
    DataUtils.cpp
    ElidedLabel.cpp
    TcpServerModule.cpp
    TcpConnectionTab.cpp
    TcpClientModule.cpp
    UdpServerModule.cpp
    UdpPeerTab.cpp
    UdpClientModule.cpp
    BroadcastModule.cpp
    MulticastModule.cpp
    ScriptBridge.cpp
    ScriptPanel.cpp
)

set(HEADERS
    MainWindow.h
    InstanceContainer.h
    Configurable.h
    NetModule.h
    LogEditor.h
    SciFindBar.h
    LexillaSetup.h
    DataUtils.h
    ElidedLabel.h
    TcpServerModule.h
    TcpConnectionTab.h
    TcpClientModule.h
    UdpServerModule.h
    UdpPeerTab.h
    UdpClientModule.h
    BroadcastModule.h
    MulticastModule.h
    ScriptBridge.h
    ScriptPanel.h
)

# ===== 第三方：Scintilla（Qt 绑定，源码直接编入主工程）=====
# qt/ScintillaEdit 下的 ScintillaEdit/ScintillaDocument + ScintillaEditBase
# 下的 PlatQt/ScintillaQt/ScintillaEditBase + src/ 核心源文件。
# ScintillaEdit.cpp/.h 是生成文件：缺失时用 qt/ScintillaEdit/WidgetGen.py
# 从 include/Scintilla.iface 重新生成（模板 *.template 已随源码提供）。
set(SCINTILLA_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third/scintilla)
set(SCINTILLA_EDIT_DIR ${SCINTILLA_DIR}/qt/ScintillaEdit)
set(SCINTILLA_BASE_DIR ${SCINTILLA_DIR}/qt/ScintillaEditBase)
if(NOT EXISTS ${SCINTILLA_EDIT_DIR}/ScintillaEdit.cpp OR NOT EXISTS ${SCINTILLA_EDIT_DIR}/ScintillaEdit.h)
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
        execute_process(
            COMMAND ${Python3_EXECUTABLE} WidgetGen.py
            WORKING_DIRECTORY ${SCINTILLA_EDIT_DIR}
            RESULT_VARIABLE _wg_result)
        if(NOT _wg_result EQUAL 0)
            message(FATAL_ERROR "WidgetGen.py 生成 ScintillaEdit 失败（退出码 ${_wg_result}）")
        endif()
    else()
        message(FATAL_ERROR "缺少 ${SCINTILLA_EDIT_DIR}/ScintillaEdit.cpp/h，且未找到 Python 解释器用于生成")
    endif()
endif()
add_library(scintilla STATIC
    ${SCINTILLA_EDIT_DIR}/ScintillaEdit.h
    ${SCINTILLA_EDIT_DIR}/ScintillaEdit.cpp
    ${SCINTILLA_EDIT_DIR}/ScintillaDocument.h
    ${SCINTILLA_EDIT_DIR}/ScintillaDocument.cpp
    ${SCINTILLA_BASE_DIR}/ScintillaEditBase.h
    ${SCINTILLA_BASE_DIR}/PlatQt.cpp
    ${SCINTILLA_BASE_DIR}/ScintillaQt.h
    ${SCINTILLA_BASE_DIR}/ScintillaQt.cpp
    ${SCINTILLA_BASE_DIR}/ScintillaEditBase.cpp
    ${SCINTILLA_DIR}/src/AutoComplete.cxx
    ${SCINTILLA_DIR}/src/CallTip.cxx
    ${SCINTILLA_DIR}/src/CaseConvert.cxx
    ${SCINTILLA_DIR}/src/CaseFolder.cxx
    ${SCINTILLA_DIR}/src/CellBuffer.cxx
    ${SCINTILLA_DIR}/src/ChangeHistory.cxx
    ${SCINTILLA_DIR}/src/CharClassify.cxx
    ${SCINTILLA_DIR}/src/CharacterCategoryMap.cxx
    ${SCINTILLA_DIR}/src/CharacterType.cxx
    ${SCINTILLA_DIR}/src/ContractionState.cxx
    ${SCINTILLA_DIR}/src/DBCS.cxx
    ${SCINTILLA_DIR}/src/Decoration.cxx
    ${SCINTILLA_DIR}/src/Document.cxx
    ${SCINTILLA_DIR}/src/EditModel.cxx
    ${SCINTILLA_DIR}/src/Editor.cxx
    ${SCINTILLA_DIR}/src/EditView.cxx
    ${SCINTILLA_DIR}/src/Geometry.cxx
    ${SCINTILLA_DIR}/src/Indicator.cxx
    ${SCINTILLA_DIR}/src/KeyMap.cxx
    ${SCINTILLA_DIR}/src/LineMarker.cxx
    ${SCINTILLA_DIR}/src/MarginView.cxx
    ${SCINTILLA_DIR}/src/PerLine.cxx
    ${SCINTILLA_DIR}/src/PositionCache.cxx
    ${SCINTILLA_DIR}/src/RESearch.cxx
    ${SCINTILLA_DIR}/src/RunStyles.cxx
    ${SCINTILLA_DIR}/src/ScintillaBase.cxx
    ${SCINTILLA_DIR}/src/Selection.cxx
    ${SCINTILLA_DIR}/src/Style.cxx
    ${SCINTILLA_DIR}/src/UndoHistory.cxx
    ${SCINTILLA_DIR}/src/UniConversion.cxx
    ${SCINTILLA_DIR}/src/UniqueString.cxx
    ${SCINTILLA_DIR}/src/ViewStyle.cxx
    ${SCINTILLA_DIR}/src/XPM.cxx
)
target_include_directories(scintilla PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/compat
    ${SCINTILLA_EDIT_DIR}
    ${SCINTILLA_BASE_DIR}
    ${SCINTILLA_DIR}/include
    ${SCINTILLA_DIR}/src
)
# 与官方 qt/ScintillaEdit/ScintillaEdit.pro 的定义一致
target_compile_definitions(scintilla PRIVATE SCINTILLA_QT=1 MAKING_LIBRARY=1)
# 官方 release 配置定义 NDEBUG=1；非 Debug 构建统一加上
target_compile_definitions(scintilla PRIVATE $<$<NOT:$<CONFIG:Debug>>:NDEBUG=1>)
target_link_libraries(scintilla PUBLIC
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Gui
    Qt${QT_VERSION_MAJOR}::Widgets
)

# ===== 第三方：Lexilla（词法器库，静态编入；仅编 Python 词法器所需子集）=====
# 不编 src/Lexilla.cxx：其 AddEachLexer() 引用全部 lmXxx 符号，而我们
# 只编 LexPython；未编入的 lm 在静态归档里是未定义引用。改为在
# LexillaSetup.cpp 里直接 extern lmPython 并调 lmPython.Create()，
# 完全绕开 Lexilla 的目录机制。
set(LEXILLA_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third/lexilla)
add_library(lexilla STATIC
    ${LEXILLA_DIR}/lexers/LexPython.cxx
    ${LEXILLA_DIR}/lexlib/Accessor.cxx
    ${LEXILLA_DIR}/lexlib/CharacterCategory.cxx
    ${LEXILLA_DIR}/lexlib/CharacterSet.cxx
    ${LEXILLA_DIR}/lexlib/DefaultLexer.cxx
    ${LEXILLA_DIR}/lexlib/InList.cxx
    ${LEXILLA_DIR}/lexlib/LexAccessor.cxx
    ${LEXILLA_DIR}/lexlib/LexerBase.cxx
    ${LEXILLA_DIR}/lexlib/LexerModule.cxx
    ${LEXILLA_DIR}/lexlib/LexerSimple.cxx
    ${LEXILLA_DIR}/lexlib/PropSetSimple.cxx
    ${LEXILLA_DIR}/lexlib/StyleContext.cxx
    ${LEXILLA_DIR}/lexlib/WordList.cxx
)
target_include_directories(lexilla PUBLIC
    ${LEXILLA_DIR}/include
    ${LEXILLA_DIR}/lexlib
    ${SCINTILLA_DIR}/include
)
target_link_libraries(lexilla PUBLIC Qt${QT_VERSION_MAJOR}::Core)

# WIN32 使其成为 GUI 子系统程序；Qt5 CMake 会据此自动链接 qtmain 入口
# resources/translations.qrc 经 AUTORCC 把 network_tool.qm 嵌入可执行文件
# resources/app.qrc 经 AUTORCC 把 network_tool.png 及各 SVG 图标嵌入
#   （network_tool.png 供窗口图标 setWindowIcon 使用，SVG 供菜单/工具栏/页签使用）
# resources/script.qrc 经 AUTORCC 把 runner.py 嵌入，供 ScriptBridge 运行
add_executable(network_tool WIN32 ${SOURCES} ${HEADERS}
    resources/translations.qrc resources/app.qrc resources/script.qrc)

# 应用图标：Windows 下用 windres 把 resources/network_tool.rc（引用 network_tool.ico）
# 编进 .exe 资源段，作为 Explorer/任务栏/标题栏图标。
# windres 预处理 .rc 时会调用 gcc；裸 shell 下若 MinGW 不在 PATH 会报
# "gcc 不是内部命令"。这里把 windres 与预处理器都指向与 C 编译器同目录的
# 绝对路径，使清理/增量构建在任何 shell 下都能成功，且与工具链版本一致。
if(WIN32)
    target_sources(network_tool PRIVATE resources/network_tool.rc)
    get_filename_component(_mingw_bin "${CMAKE_C_COMPILER}" DIRECTORY)
    if(EXISTS "${_mingw_bin}/windres.exe")
        set(CMAKE_RC_COMPILER "${_mingw_bin}/windres.exe" CACHE FILEPATH "Windows resource compiler" FORCE)
        if(EXISTS "${_mingw_bin}/gcc.exe")
            # --preprocessor 替换默认 gcc 后，windres 不再自动附加 -E -xc 等参数，
            # 需用 --preprocessor-arg 手动补上，否则 gcc 会把 .rc 当作链接输入，
            # 报 "file not recognized: File format not recognized"。
            set(CMAKE_RC_FLAGS "${CMAKE_RC_FLAGS} --preprocessor=${_mingw_bin}/gcc.exe --preprocessor-arg=-E --preprocessor-arg=-xc --preprocessor-arg=-DRC_INVOKED")
        endif()
    endif()
endif()

target_compile_definitions(network_tool PRIVATE SCINTILLA_QT=1 MAKING_LIBRARY=1)
target_link_libraries(network_tool PRIVATE
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Gui
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Network
    Qt${QT_VERSION_MAJOR}::Svg
    scintilla
    lexilla
)
# 若环境中存在 Qt OpenGL 模块则链接之（当前无源文件使用，仅保持扩展兼容）
if(Qt${QT_VERSION_MAJOR}_OpenGL_FOUND)
    target_link_libraries(network_tool PRIVATE Qt${QT_VERSION_MAJOR}::OpenGL)
endif()

if(WIN32)
    # BroadcastModule 直接调用原生 setsockopt(SO_BROADCAST)，需链接 Winsock 库
    target_link_libraries(network_tool PRIVATE ws2_32 dwmapi)
endif()
