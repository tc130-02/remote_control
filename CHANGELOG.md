# 修改日志

本项目的重要功能、修复、验证结果和已知限制记录在此文件中。

## [0.4.0] - 2026-09-29

### 新增

- 新增 Windows/Linux 共用的中文 Qt 统一应用 `remote_control`。
- 单个程序同时提供“控制其他设备”和“允许远程控制”两个页面。
- Qt 界面启动后自动运行当前平台的被控服务，不再要求用户手动启动 server。
- 新增中文连接状态、重连提示、心跳信息、画面信息和网络错误提示。
- 新增 Windows 服务端后台隐藏启动、启停控制和实时日志页面。
- 新增 Linux CMake 构建入口，并兼容 Ubuntu 22.04 的 Qt 6.2。
- 新增 Ubuntu/Debian 安装包构建脚本、桌面入口和运行命令。
- 新增 Windows x64 便携包和 Ubuntu 22.04 amd64 安装包。

### 修复

- 修复 Qt 鼠标移动坐标在发送前被提前记录，导致移动事件被误判为重复的问题。
- Qt 原始 TCP 连接明确禁用系统代理，修复 Windows 到 Linux 私有地址无法直连的问题。
- Linux 默认使用 Qt xcb 后端，与项目现有 X11/XShm 和 xdotool 输入链路保持一致。
- CMake 在旧版 Qt 6 缺少 `qt_standard_project_setup` 时自动启用 MOC、UIC 和 RCC。
- Windows 构建固定使用与 Qt 匹配的 MinGW 工具链，避免链接不兼容。

### 验证

- Windows Qt → Windows server：TCP 会话建立。
- Linux Qt → Linux server：TCP 会话建立。
- Windows Qt → Linux server：协议握手成功，心跳往返约 1 毫秒。
- Linux Qt → Windows server：TCP 会话建立。
- Windows 统一应用启动后自动运行 `win_server.exe` 并监听 9999。
- Ubuntu 安装包安装后自动运行 `/opt/remote-control/linux_server` 并监听 9999。

### 已知限制

- 四种协议路径和构建已验证，但仍需在真实 Linux X11 桌面进行画面与键鼠验收。
- Linux 原生 Wayland 输入注入暂不支持。
- 屏幕采用完整 JPEG 帧，公网低带宽下可能产生明显画面延迟。
- 当前协议没有认证、授权和加密，只适合可信设备临时测试。

## [0.3.0] - 2026-09-29

### 新增

- Qt 远程画面增加键盘、鼠标、拖拽、双击和滚轮控制。
- 增加远程输入暂停开关和失焦按键释放，降低远端卡键风险。
- 发布 Windows x64 双端便携包。

### 验证

- Windows Qt 客户端通过公网 TCP 隧道控制 Windows 被控端。
- 远程键鼠操作成功，客户端主动关闭后服务端正常回到等待状态。

## [0.2.0]

### 新增

- Qt 连接管理和远程画面显示。
- JPEG/BGRA32 画面接收、后台解码和最新帧覆盖。
- 心跳检测、指数退避自动重连和服务端断线后继续监听。
- Packet 与套接字资源使用 RAII 管理。
