# 基于 TCP 的跨平台远程控制系统实践

## 项目简介

本项目是一个基于 C++ 实现的跨平台远程控制系统，支持 Windows 与 Linux 双端作为控制端或被控端运行。项目使用统一的 TCP 自定义应用层协议，实现远程屏幕传输、鼠标键盘事件传输、TCP 粘包/半包处理以及跨平台系统接口适配。

项目已完成局域网通信和基于 cpolar TCP 隧道的公网互通测试，用于学习和实践 C++ 网络编程、跨平台通信及桌面输入输出处理。当前未实现身份认证和加密传输，不应作为生产环境远程控制工具直接使用。

## 项目结构

```text
remote_control/
├── common/
│   └── packet.h          # 公共协议定义：Packet、命令号、键鼠事件、屏幕帧结构
├── linux/
│   ├── client.cpp        # Linux 控制端：接收 Windows 屏幕并发送鼠标/键盘事件
│   └── server.cpp        # Linux 被控端：接收控制命令并回传 Linux 屏幕
├── windows/
│   ├── CMakeLists.txt     # Windows 端 CMake 构建配置
│   ├── client.cpp        # Windows 控制端
│   └── server.cpp        # Windows 被控端：接收 Linux 控制并回传 Windows 屏幕
├── .gitignore
├── LICENSE
└── README.md
```

## 当前功能

### 1. 已验证平台组合

Windows/Linux 四端组合均已完成远程控制链路验证：

| 控制端 | 被控端 | 验证结果 |
| --- | --- | --- |
| Windows Client | Windows Server | ✅ |
| Windows Client | Linux Server | ✅ |
| Linux Client | Windows Server | ✅ |
| Linux Client | Linux Server | ✅ |

测试覆盖局域网连接和通过 TCP 公网隧道进行的跨网络连接。公网测试用于验证域名解析、自定义端口和统一协议的跨网络可用性，不代表当前版本已经具备生产级安全能力。

### 2. 自定义 TCP 协议

项目使用统一的 Packet 协议格式：

```text
magic | cmd | body_len | data
```

其中：

* `magic` 用于校验数据包合法性
* `cmd` 表示命令类型
* `body_len` 表示数据体长度
* `data` 存放字符串、键鼠事件结构体或屏幕分块数据

服务端和客户端都实现了基于缓冲区的 TCP 粘包/半包处理逻辑，通过 `body_len` 判断一个完整业务包是否接收完成。

### 3. 已实现功能

* 基于 Windows WinSock 和 Linux POSIX Socket 的 TCP 通信
* Windows/Linux 四端共享的自定义应用层协议
* 由 `magic`、`cmd`、`body_len` 组成的逻辑 PacketHeader
* 基于接收缓冲区和 `body_len` 的 TCP 粘包、半包处理
* BGRA32 远程屏幕采集、分片传输、重组和缩放显示
* 鼠标移动、点击、按下、抬起和双击事件传输
* 键盘按下和抬起事件传输
* Windows Win32 API、GDI 和 WinSock 接口适配
* Linux POSIX Socket、X11 和 XShm 接口适配
* IPv4、域名和自定义 TCP 端口配置

### 4. Linux client 控制 Windows server

当前已实现：

* Linux client 连接 Windows server
* Windows server 回传 Windows 屏幕帧
* Linux client 使用 X11 创建显示窗口
* Linux client 对远程屏幕进行按比例缩放显示
* Linux client 捕获窗口内鼠标移动事件并转发给 Windows
* Linux client 捕获窗口内鼠标点击事件并转发给 Windows
* Linux client 支持键盘事件结构体发送

### 5. Windows server 屏幕回传

Windows server 使用 Win32 GDI 进行屏幕采集：

* `GetDC`
* `CreateCompatibleDC`
* `CreateDIBSection`
* `BitBlt`

采集到的图像统一为 BGRA32 格式，并通过以下三类包进行分包发送：

* `CMD_SCREEN_BEGIN`
* `CMD_SCREEN_CHUNK`
* `CMD_SCREEN_END`

Linux client 接收完整一帧后进行重组和显示。

### 6. Windows client 控制 Linux server

当前已实现：

* Windows client 连接 Linux server
* Linux server 使用 X11/XShm 采集并回传 Linux 屏幕帧
* Windows client 显示远程屏幕并发送鼠标/键盘事件

### 7. 键鼠事件传输

项目定义了统一的键鼠事件结构：

```cpp
struct MouseEvent {
    int action;
    int button;
    int x;
    int y;
};

struct KeyEvent {
    int key_status;
    char key[32];
};
```

鼠标事件包括：

* 移动
* 点击
* 按下
* 抬起
* 双击

键盘事件包括：

* 按下
* 抬起

## 编译方式

### Linux

需要安装编译工具和 X11 开发库：

```bash
sudo apt install build-essential libx11-dev libxext-dev
```

编译 client 和 server：

```bash
cd linux
g++ client.cpp -o client -lX11 -pthread
g++ server.cpp -o server -lX11 -lXext -pthread
```

### Windows

使用 CMake 编译 client 和 server。以下示例使用 MinGW：

```powershell
cmake -S windows -B windows/build -G "MinGW Makefiles"
cmake --build windows/build
```

生成以下程序：

* `win_client.exe`
* `win_server.exe`

也可根据本机环境选择 Ninja 或 Visual Studio 生成器。

## 网络配置

* Windows 和 Linux 服务端均监听所有本机 IPv4 网卡地址
* Windows 和 Linux 服务端默认监听 TCP `9999` 端口
* 服务端启动后会打印本机可用的 IPv4 地址
* 客户端启动后在连接界面中输入服务端地址和端口
* 客户端会从自身可执行文件所在目录读取 `server.conf` 作为默认值
* 连接成功后会把本次地址和端口保存回 `server.conf`

Windows client 的 `server.conf` 保存两行配置：

```text
8.tcp.vip.cpolar.cn
14320
```

第一行支持 IPv4 地址或域名，第二行为 TCP 端口。例如局域网连接可配置为：

```text
192.168.1.10
9999
```

Windows client 使用 `getaddrinfo()` 解析连接界面中的地址。`server.conf` 不存在或内容无效时输入框保持为空，用户可以直接输入 IPv4 地址或域名。端口范围为 `1～65535`。连接过程在后台线程执行，不阻塞 Win32 窗口；连接失败后会显示 WinSock 错误码并允许重新输入。

Linux client 的 `server.conf` 使用键值格式，同样支持 IPv4、域名和自定义端口：

```text
ip=6.tcp.cpolar.top
port=14826
```

`server.conf` 属于本机网络配置，已通过 `.gitignore` 忽略，请勿提交到代码仓库。

Linux client 使用现有 X11 窗口显示 host、port 输入框、连接按钮和状态栏。可用鼠标切换输入框，使用普通字符和 Backspace 编辑，按 Enter 或点击 Connect 开始异步连接。连接失败时会显示 `errno` 或 `gai_strerror()` 信息。

## 运行方式

### Linux client 连接 Windows server

1. 在 Windows 机器上运行：

```powershell
cd windows/build
.\win_server.exe
```

2. 在 Linux 端运行：

```bash
cd linux
./client
```

也可以通过命令行给出连接界面的默认值：

```bash
./client 192.168.1.10 9999
./client 8.tcp.vip.cpolar.cn 14320
```

Linux client 始终先显示 X11 连接界面。确认或修改 host 和 port 后按 Enter 或点击 Connect。连接成功后切换到远程屏幕显示；连接断开后自动返回连接界面。

### Windows client 连接 Linux server

1. 在 Linux 机器上运行：

```bash
cd linux
./server
```

2. 运行 Windows client：

```powershell
cd windows/build
.\win_client.exe
```

在 Win32 连接界面输入 Linux server 的 IPv4 地址或域名以及端口，然后点击 Connect。状态文字会显示 waiting、connecting、连接失败信息；连接成功后隐藏输入控件并进入远程屏幕。断线后会自动返回连接界面。

连接公网映射时，可在 Linux 端保持 server 监听 `9999`，再启动 TCP 隧道：

```bash
cpolar tcp 9999
```

假设 cpolar 返回 `8.tcp.vip.cpolar.cn:14320`，可直接在 Windows 或 Linux client 的连接界面输入该域名和端口。

如果连接失败，请确认域名可解析、映射端口正确、服务端与客户端网络互通，并允许防火墙放行对应 TCP 端口。

## 项目特点

* 使用 C++ 实现跨平台网络通信
* 使用自定义协议处理应用层消息边界
* 处理 TCP 粘包和半包问题
* 屏幕帧采用 begin/chunk/end 分包传输
* Linux 端使用 X11 进行窗口显示和鼠标事件捕获
* Windows 端使用 Win32 API 实现屏幕采集和键鼠输入模拟
* 支持远程屏幕缩放显示和坐标映射

## 工程问题记录

### TCP 无消息边界

TCP 提供的是连续字节流，不保留应用层消息边界，一次 `send()` 的数据可能被拆分到多次 `recv()`，多次发送的数据也可能在一次接收中合并。项目通过固定包头中的 `body_len` 计算完整 Packet 长度，并使用接收缓冲区保留尚未处理完的数据。

### 大屏幕数据分片传输

原始 BGRA32 屏幕帧通常大于单个 Packet 的数据区。项目将一帧拆分为 `CMD_SCREEN_BEGIN`、多个 `CMD_SCREEN_CHUNK` 和 `CMD_SCREEN_END`，接收端根据帧编号、偏移量和总大小重组完整屏幕。

### 跨平台接口适配

Windows 和 Linux 在 Socket、屏幕采集、窗口显示及输入事件接口上存在差异。项目在保持统一通信协议的同时，分别使用 WinSock/Win32 API/GDI 和 POSIX Socket/X11/XShm 完成平台相关实现。

## 当前限制

* 已完成 TCP 公网隧道互通验证，但未实现生产级连接安全机制
* 屏幕传输未做压缩，带宽占用较高
* 鼠标移动暂未做节流优化
* 键盘实时捕获功能仍可继续完善
* 未实现身份认证和加密传输
* 不适合作为生产环境公网远程控制工具使用

## 后续计划

* 增加屏幕压缩，降低公网传输带宽
* 实现增量或差分屏幕传输
* 增加身份认证和传输安全机制
* 增加 GUI 配置界面，管理服务器地址、端口和连接状态
* 继续完善连接断开后的资源清理与重连机制

## 说明

本项目为个人学习和求职展示项目，重点在于理解和实践 C++ 网络编程、Socket 通信、自定义协议、跨平台 API 调用和问题定位过程。
