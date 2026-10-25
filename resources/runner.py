"""network_tool 脚本运行器:常驻解释器,命名管道 + 结构化二进制帧驱动用户脚本回调。

用法: python -u runner.py <pipe_name>
  <pipe_name> 由 C++ 传入,Windows 形如 \\\\.\\pipe\\network_tool_script_..., Linux 形如临时目录下文件路径。
帧格式: [type:u8][len:u32LE][payload:N],全程小端。
"""
import sys
import os
import struct
import traceback
import io
import builtins as _builtins

# 帧类型(C++ -> Python)
FT_LOAD = 1
FT_RECV = 2
FT_CONNECTED = 3
FT_DISCONNECTED = 4
FT_START = 5
FT_STOP_EVENT = 6
FT_SHUTDOWN = 7
# Python -> C++
FT_STARTED = 16
FT_SEND = 17
FT_LOG = 18
FT_ERROR = 19

# 跨平台连接管道:Windows 命名管道可当文件打开;Linux/Unix domain socket 用 socket。
_pipe = None  # 全局连接对象(file 或 socket)


def _open_pipe(name):
    """按平台连接 C++ 创建的管道,返回可 read/write 字节的对象。"""
    if os.name == "nt":
        # Windows 命名管道:以二进制读写打开,buffering=0 避免缓冲滞后
        return open(name, "r+b", buffering=0)
    else:
        import socket
        s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        s.connect(name)
        return s


def _raw_write(b):
    """写裸字节到管道(file 用 write,socket 用 sendall)。"""
    if hasattr(_pipe, "sendall"):
        _pipe.sendall(b)
    else:
        _pipe.write(b)


def _raw_read(n):
    """读恰好 n 字节,不足则阻塞直到 EOF(返回的短于 n 表示对端关)。

    Windows 命名管道以文件打开时,单次 read(n) 可能只返回部分字节(非 EOF),
    必须循环凑齐 n 字节,否则主循环会把半帧头误判为对端关闭。socket 分支同理用 recv 循环。
    """
    readfn = _pipe.recv if hasattr(_pipe, "recv") else _pipe.read
    chunks = []
    got = 0
    while got < n:
        c = readfn(n - got)
        if not c:
            return b"".join(chunks)
        chunks.append(c)
        got += len(c)
    return b"".join(chunks)


def _emit(type_, payload=b""):
    """组一帧并写管道。"""
    header = struct.pack("<BI", type_, len(payload))
    _raw_write(header + payload)


def _pack_str(s, width):
    """字符串 -> [len:u16或u32LE][utf8]。"""
    b = s.encode("utf-8")
    fmt = "<H" if width == 2 else "<I"
    return struct.pack(fmt, len(b)) + b


class _Bridge:
    """暴露给用户脚本的 send/log,内部转成二进制命令帧发回 C++。"""

    def send(self, data, peer=None):
        if isinstance(data, str):
            data = data.encode("utf-8")
        elif isinstance(data, bytearray):
            data = bytes(data)
        elif not isinstance(data, bytes):
            raise TypeError("send() expects bytes or str, got %s" % type(data).__name__)
        p = struct.pack("<I", len(data)) + data + _pack_str(peer or "", 2)
        _emit(FT_SEND, p)

    def log(self, msg):
        _emit(FT_LOG, _pack_str(str(msg), 4))


class _PrintRedirector(io.TextIOBase):
    """把 print() 重定向到 LOG 帧:write 缓冲到换行再整行发送。"""

    def __init__(self):
        self._buf = ""

    def writable(self):
        return True

    def write(self, s):
        if not s:
            return 0
        self._buf += s
        while "\n" in self._buf:
            line, self._buf = self._buf.split("\n", 1)
            if line:
                _emit(FT_LOG, _pack_str(line, 4))
        return len(s)

    def flush(self):
        if self._buf:
            _emit(FT_LOG, _pack_str(self._buf, 4))
            self._buf = ""


_print = _PrintRedirector()
def _user_print(*args, **kwargs):
    kwargs.setdefault("file", _print)
    _builtins.print(*args, **kwargs)


_bridge = _Bridge()
_user_ns = {
    "send": _bridge.send,
    "log": _bridge.log,
    "print": _user_print,
    "__name__": "__user_script__",
}


def _call(name, *args):
    """调用用户回调;未定义则静默跳过;异常发 ERROR 帧但不退出。

    捕获 SystemExit(用户脚本里 sys.exit()/raise SystemExit 会触发),否则它不被
    Exception 捕获,会让解释器静默退出,本实例莫名"停止响应"。KeyboardExit/
    GeneratorExit 仍向上传播,不应在此吞掉。
    """
    fn = _user_ns.get(name)
    if fn is None:
        return
    try:
        fn(*args)
    except (Exception, SystemExit):
        msg = _pack_str("%s failed" % name, 4)
        tb = _pack_str(traceback.format_exc(), 4)
        _emit(FT_ERROR, msg + tb)


def _unpack_str(payload, off, width):
    """从 payload 偏移处解 [len][utf8],返回 (str, 新偏移)。

    声明长度越界(payload 被截断或长度字段损坏)时抛 ValueError,由上层统一兜底。
    """
    fmt = "<H" if width == 2 else "<I"
    n = struct.unpack_from(fmt, payload, off)[0]
    off += struct.calcsize(fmt)
    if off + n > len(payload):
        raise ValueError("string length %d exceeds payload" % n)
    s = payload[off:off + n].decode("utf-8")
    return s, off + n


def _dispatch(type_, payload):
    """按帧类型分发到用户回调。

    整个分发体包在 try/except 里:任何解析错(越界、损坏长度字段)或回调外路径
    异常都转成 ERROR 帧上抛给 C++,主循环不被打断,进程继续接后续事件。

    捕获 SystemExit 与 _call 对齐:用户脚本在模块顶层 sys.exit()(如配置缺失
    时"提前退出")同样应上报 ERROR,而非让解释器静默退出、本实例莫名停摆。
    KeyboardInterrupt/GeneratorExit 仍向上传播,不在此吞掉。"""
    try:
        if type_ == FT_LOAD:
            off = 0
            src, off = _unpack_str(payload, off, 4)
            exec(compile(src, "<user_script>", "exec"), _user_ns)
            _emit(FT_STARTED, _pack_str(".".join(map(str, sys.version_info[:3])), 2))
        elif type_ == FT_RECV:
            off = 0
            dl = struct.unpack_from("<I", payload, off)[0]
            off += 4
            if off + dl > len(payload):
                raise ValueError("data length %d exceeds payload" % dl)
            data = payload[off:off + dl]
            off += dl
            peer, off = _unpack_str(payload, off, 2)
            _call("on_receive", data, peer)
        elif type_ == FT_CONNECTED:
            peer, _ = _unpack_str(payload, 0, 2)
            _call("on_connect", peer)
        elif type_ == FT_DISCONNECTED:
            peer, _ = _unpack_str(payload, 0, 2)
            _call("on_disconnect", peer)
        elif type_ == FT_START:
            _call("on_start")
        elif type_ == FT_STOP_EVENT:
            _call("on_stop")
    except (Exception, SystemExit):
        _emit(FT_ERROR, _pack_str("dispatch failed", 4) +
              _pack_str(traceback.format_exc(), 4))


_PIPE_MAX_PAYLOAD = 64 * 1024 * 1024  # 与 C++ kMaxPayload 对齐,防损坏长度字段触发巨量读


def main():
    global _pipe
    if len(sys.argv) < 2:
        sys.stderr.write("usage: runner.py <pipe_name>\n")
        return
    _pipe = _open_pipe(sys.argv[1])
    # 主循环:精确读 5 字节头 + len 字节 payload,逐帧分发
    while True:
        header = _raw_read(5)
        if len(header) < 5:
            break  # 对端关
        type_, ln = struct.unpack("<BI", header)
        if ln > _PIPE_MAX_PAYLOAD:
            # 损坏长度字段:不尝试分配巨量缓冲,直接报错退出(对端关/缓冲错位已无法恢复)
            _emit(FT_ERROR, _pack_str("oversized frame %d" % ln, 4))
            break
        payload = _raw_read(ln) if ln else b""
        if ln and len(payload) < ln:
            break  # 对端关半帧
        if type_ == FT_SHUTDOWN:
            break
        _dispatch(type_, payload)
    # 退出前冲刷 print 缓冲,避免末行未发出的 LOG 丢失
    try:
        _print.flush()
    except Exception:
        pass
    try:
        _pipe.close()
    except Exception:
        pass


if __name__ == "__main__":
    main()
