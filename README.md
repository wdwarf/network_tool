# Network Test Tool

基于 Qt 5.14.2 (MinGW 64-bit) 的网络测试工具，支持 IPv4/IPv6，覆盖 TCP 服务端/客户端、UDP 服务端/客户端、广播、组播，收发数据可在文本与十六进制之间切换显示。

## 功能

- **TCP Server**：监听指定 IP/端口，支持多客户端同时连接，可选择向全部或指定客户端发送。
- **TCP Client**：连接远端 host:port，可指定本地端口，收发数据。
- **UDP Server**：绑定本地端口接收数据报，记录对端列表，双击对端即可填充回复目标。
- **UDP Client**：向目标发送数据报；Open 绑定本地端口后可接收回复。
- **Broadcast**：发送广播数据报（`255.255.255.255` 或子网定向广播）。
- **Multicast**：加入/离开组播组（IPv4 `224.0.0.0/4` 或 IPv6 `ff00::/8`），收发组播数据。
- 收发区可切换 **Text / Hex** 显示；发送区可独立选择按文本或十六进制输入；可开关时间戳与 TX 回显。

## 依赖

- Qt 5.14.2 (MinGW 64-bit)：`D:\Qt\Qt5.14.2\5.14.2\mingw73_64`
- CMake ≥ 3.10
- MinGW（随 Qt 自带 `D:\Qt\Qt5.14.2\Tools\mingw730_64`）

## 构建

### 命令行（CMake + MinGW Makefiles）

```bash
cd D:/test/network_tool
mkdir build
cd build
cmake -G "MinGW Makefiles" -DCMAKE_PREFIX_PATH=D:/Qt/Qt5.14.2/5.14.2/mingw73_64 ..
cmake --build . -j
```

生成可执行文件 `network_tool.exe`（位于 `build` 目录下）。

> 若未通过 `-DCMAKE_PREFIX_PATH` 指定，`CMakeLists.txt` 内已默认指向上述 Qt 路径。

### Qt Creator

用 Qt Creator 打开 `CMakeLists.txt`，选择 MinGW 64-bit Kit，点击构建/运行即可。

## 使用要点

- **十六进制发送**：Send 区选 `Send as: Hex`，可输入形如 `48 65 6C 6C 6F`、`0x41 0x42` 或 `4142`，空格/换行/`0x` 前缀会被自动忽略；输入非法会提示且不发送。
- **IPv6**：所有 IP/地址栏均可填 IPv4 或 IPv6 字面量；监听/绑定下拉含 `Any IPv4`、`Any IPv6`、`Any (dual)` 及本机地址。
- **组播 TTL**：默认 1（仅本链路），需跨网段时调大。
- **广播**：`255.255.255.255` 为受限广播；定向广播用子网广播地址（如 `192.168.1.255`）。IPv6 无广播，使用组播 `ff02::1`。
- **接收显示切换**：切换 Text/Hex 会重新渲染整段历史接收记录（保留原始字节），便于随时查看。

## 文件结构

```txt
network_tool/
├── CMakeLists.txt
├── main.cpp
├── MainWindow.{h,cpp}        # 主窗口（多页签）
├── NetModule.{h,cpp}         # 基类：接收区/发送区/文本-十六进制切换
├── DataUtils.{h,cpp}         # hex/文本互转、IP 下拉填充
├── TcpServerModule.{h,cpp}
├── TcpClientModule.{h,cpp}
├── UdpServerModule.{h,cpp}
├── UdpClientModule.{h,cpp}
├── BroadcastModule.{h,cpp}
├── MulticastModule.{h,cpp}
├── resources/                # 所有资源统一收纳于此
│   ├── app.qrc               # 窗口图标 png + 各 SVG 图标
│   ├── translations.qrc      # network_tool.qm 翻译
│   ├── script.qrc            # runner.py 脚本运行器
│   ├── network_tool.png/.ico # 应用图标
│   ├── network_tool.ts/.qm   # 翻译源/编译产物
│   ├── network_tool.rc       # Windows 资源（嵌入 .ico）
│   ├── runner.py             # 内置脚本运行器
│   ├── cat-*.svg             # 6 个类别图标（页签/菜单/工具栏共用）
│   └── cmd-*.svg             # 命令图标（新建/关闭/保存/加载/退出/关于）
└── README.md
```
