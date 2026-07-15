# 基于 TCP 的跨平台远程控制系统实践

## 项目简介

本项目是一个用于学习和实践 C++ 网络编程、跨平台通信和桌面输入输出处理的实验性项目。项目基于 TCP Socket 自定义应用层协议，实现 Windows 与 Linux 之间的基础远程控制链路，包括消息封包、粘包/半包处理、键鼠事件传输以及屏幕帧分包回传。

本项目仅用于局域网学习、课程/求职项目展示和个人实验，不包含自启动、隐藏运行、权限提升、持久化驻留等功能。

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

### 1. 自定义 TCP 协议

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

### 2. Linux client 控制 Windows server

当前已实现：

* Linux client 连接 Windows server
* Windows server 回传 Windows 屏幕帧
* Linux client 使用 X11 创建显示窗口
* Linux client 对远程屏幕进行按比例缩放显示
* Linux client 捕获窗口内鼠标移动事件并转发给 Windows
* Linux client 捕获窗口内鼠标点击事件并转发给 Windows
* Linux client 支持键盘事件结构体发送

### 3. Windows server 屏幕回传

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

### 4. Windows client 控制 Linux server

当前已实现：

* Windows client 连接 Linux server
* Linux server 使用 X11/XShm 采集并回传 Linux 屏幕帧
* Windows client 显示远程屏幕并发送鼠标/键盘事件

### 5. 键鼠事件传输

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
g++ client.cpp -o client -lX11
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
* TCP 端口统一为 `9999`
* 服务端启动后会打印本机可用的 IPv4 地址
* 客户端从自身可执行文件所在目录读取 `server.conf`

`server.conf` 只保存一行服务端 IPv4 地址，不要添加端口或其他配置。格式示例：

```text
192.0.2.10
```

请将示例替换为服务端启动时实际打印的 IPv4 地址。配置文件不存在、内容为空或 IPv4 格式错误时，客户端会打印错误并停止连接。

`server.conf` 属于本机网络配置，已通过 `.gitignore` 忽略，请勿提交到代码仓库。

## 运行方式

### Linux client 连接 Windows server

1. 在 Windows 机器上运行：

```powershell
cd windows/build
.\win_server.exe
```

2. 将 Windows server 打印的 IPv4 地址写入 Linux `client` 可执行文件同目录下的 `server.conf`。

3. 在 Linux 端运行：

```bash
cd linux
./client
```

连接成功后，Linux client 会显示 Windows 屏幕，并支持在显示窗口内移动和点击鼠标来控制 Windows。

### Windows client 连接 Linux server

1. 在 Linux 机器上运行：

```bash
cd linux
./server
```

2. 将 Linux server 打印的 IPv4 地址写入 `win_client.exe` 同目录下的 `server.conf`。

3. 运行 Windows client：

```powershell
cd windows/build
.\win_client.exe
```

如果连接失败，请确认服务端与客户端网络互通，并允许防火墙放行 TCP `9999` 端口。

## 项目特点

* 使用 C++ 实现跨平台网络通信
* 使用自定义协议处理应用层消息边界
* 处理 TCP 粘包和半包问题
* 屏幕帧采用 begin/chunk/end 分包传输
* Linux 端使用 X11 进行窗口显示和鼠标事件捕获
* Windows 端使用 Win32 API 实现屏幕采集和键鼠输入模拟
* 支持远程屏幕缩放显示和坐标映射

## 当前限制

* 当前主要用于局域网环境测试
* 屏幕传输未做压缩，带宽占用较高
* 鼠标移动暂未做节流优化
* 键盘实时捕获功能仍可继续完善
* 未实现身份认证和加密传输
* 不适合作为公网远程控制工具使用

## 后续计划

* 增加鼠标移动节流，降低高频事件发送压力
* 完善 Linux client 键盘实时输入捕获
* 增加连接断开后的资源清理和重连机制
* 加入简单身份验证
* 优化屏幕传输性能，例如压缩或差分传输
* 整理 Windows/Linux 四端统一编译和运行脚本

## 说明

本项目为个人学习和求职展示项目，重点在于理解和实践 C++ 网络编程、Socket 通信、自定义协议、跨平台 API 调用和问题定位过程。
