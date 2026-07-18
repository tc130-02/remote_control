# Remote Control：Windows/Linux 跨平台远程控制实践

这是一个使用 C++、TCP、Win32 和 X11 实现的跨平台远程控制项目。Windows 和 Linux 都可以作为被控端（server）或控制端（client），四种端到端代码路径共用同一套协议：

| 被控端 | 控制端 | 屏幕传输路径 |
| --- | --- | --- |
| Windows server | Windows client | GDI 截屏 → JPEG → Win32 显示 |
| Windows server | Linux client | GDI 截屏 → JPEG → X11 显示 |
| Linux server | Windows client | XShm 截屏 → JPEG → Win32 显示 |
| Linux server | Linux client | XShm 截屏 → JPEG → X11 显示 |

当前版本已经完成四端 JPEG 编解码、客户端本地等比例缩放、黑边鼠标坐标映射和全目标编译。最新四端实现仍建议在目标机器上重新进行 GUI、局域网和 cpolar 联调。

> 本项目没有身份认证、加密或公网安全防护，不应直接作为生产环境远程控制工具使用。

## 项目结构

```text
remote_control/
├── common/
│   └── packet.h              # 四端共享的协议、命令和固定宽度结构
├── third_party/
│   ├── stb_image.h           # JPEG 内存解码
│   ├── stb_image_write.h     # JPEG 内存编码
│   └── README.md
├── windows/
│   ├── CMakeLists.txt
│   ├── server.cpp            # Windows 被控端
│   └── client.cpp            # Windows 控制端
├── linux/
│   ├── server.cpp            # Linux 被控端
│   └── client.cpp            # Linux 控制端
├── LICENSE
└── README.md
```

## 核心功能

- WinSock/POSIX Socket TCP 通信
- IPv4、域名和自定义端口连接
- `getaddrinfo()` 地址解析
- TCP `sendall`、粘包和半包解析
- Windows GDI 原始桌面分辨率截屏
- Linux X11/XShm 原始桌面分辨率截屏
- quality 75 的 JPEG 内存编码和解码
- 约 64KB 的屏幕数据分片
- BGRA32 旧格式接收兼容
- Windows/Linux 客户端自由调整窗口大小
- 居中等比例缩放和黑色留边
- 基于实际画面区域的鼠标坐标映射
- 鼠标移动、点击、按下、抬起、双击和滚轮事件
- 键盘按下和抬起事件
- 断线后返回连接界面并允许重新连接
- 屏幕帧单槽位更新，避免应用层无限积累历史帧

服务端始终捕获并发送远程桌面的原始宽高。窗口缩放只发生在客户端显示阶段，不会要求服务端降低分辨率。

## 屏幕传输协议

四端共享 [common/packet.h](common/packet.h)，基础 Packet 格式保持为：

```text
magic | cmd | body_len | data
```

协议整数使用 `int32_t`，并通过 `static_assert` 检查关键结构的线宽大小：

```text
Packet header       12 bytes
MouseEvent          16 bytes
KeyEvent            36 bytes
ScreenFrameInfo     20 bytes
ScreenChunkHeader   12 bytes
```

屏幕传输仍使用原有三阶段流程：

```text
CMD_SCREEN_BEGIN
CMD_SCREEN_CHUNK × N
CMD_SCREEN_END
```

`SCREEN_BEGIN` 中的屏幕信息为：

```cpp
struct ScreenFrameInfo {
    int32_t frame_id;
    int32_t width;
    int32_t height;
    int32_t total_size;
    int32_t format;
};
```

- `width`、`height`：远程桌面原始分辨率
- `total_size`：当前格式实际发送的字节数；JPEG 模式下为压缩后的大小
- `format`：`SCREEN_FORMAT_BGRA32` 或 `SCREEN_FORMAT_JPEG`

默认发送格式为 JPEG：

```cpp
SCREEN_FORMAT_BGRA32 = 0
SCREEN_FORMAT_JPEG = 1
```

客户端只有在收到匹配的 `SCREEN_BEGIN`、连续完整的所有分片和对应 `SCREEN_END`，且累计字节数严格等于 `total_size` 后才会解码。新帧开始时会清理未完成旧帧。

## JPEG 编码

### Windows server

1. 使用 `GetDC`、`CreateDIBSection` 和 `BitBlt` 捕获原始桌面。
2. 截屏结果为 BGRA32。
3. 显式执行 BGRA → RGB，避免红蓝通道颠倒。
4. 使用 `stbi_write_jpg_to_func` 在内存中编码 quality 75 JPEG。
5. 将 JPEG 字节通过 begin/chunk/end 流程发送。

### Linux server

1. 使用 X11/XShm 捕获原始桌面。
2. 根据 `XImage` 的 `bits_per_pixel` 和 `byte_order` 读取像素。
3. 根据 `red_mask`、`green_mask` 和 `blue_mask` 提取 RGB，不假定固定 BGRX 排列。
4. 生成连续 RGB 缓冲区。
5. 使用 `stbi_write_jpg_to_func` 在内存中编码 quality 75 JPEG。
6. 将 JPEG 字节通过同一套屏幕协议发送。

两个服务端都不会写临时图片文件，也不会缩放或裁剪远程桌面。

每发送 10 个变化帧会输出一次编码统计：

```text
frame_id
capture_ms
color_convert_ms
jpeg_encode_ms
send_ms
raw_size
jpeg_size
compression_ratio
chunk_count
```

## 客户端解码与实时性

两个客户端都支持：

- `SCREEN_FORMAT_JPEG`：使用 `stbi_load_from_memory` 强制解码为 RGBA，再转换为显示需要的格式
- `SCREEN_FORMAT_BGRA32`：保留原始 BGRA32 兼容路径
- 严格检查 `frame_id`、分片 offset、累计大小和 `SCREEN_END`
- 不解码残缺 JPEG
- 新完整帧覆盖尚未处理的旧完整帧
- 屏幕处理队列不影响鼠标、键盘和连接命令
- 断线时停止图像处理线程并清理缓存

### Windows client

- 网络线程只负责收包和完整帧重组。
- 后台线程执行 JPEG 解码和 RGBA → BGRA 转换。
- 解码完成后通过窗口消息通知 GUI。
- 所有正式绘制都由 `WM_PAINT` 完成。
- 显示帧使用互斥锁和只读共享缓冲区保护。

### Linux client

- 网络循环负责完整帧重组。
- 解码线程负责 JPEG → RGBA → BGRA。
- 独立缩放线程生成当前窗口尺寸对应的双线性缩放缓冲区。
- 缩放结果根据当前 X11 Visual 的位深、字节序和 RGB masks 转为 XImage 原生像素。
- X11 线程只处理事件、黑色清屏和最终 `XPutImage`。
- 新帧或新窗口尺寸会覆盖过期缩放请求。

## 客户端缩放和重绘

两个客户端都根据远程宽高比和窗口客户区计算最大的等比例显示区域：

```text
remote_width / remote_height
window_width / window_height
display_x / display_y
display_width / display_height
```

远程画面在窗口中居中，未占用区域填充为黑色。

### Windows

- `WM_SIZE` 更新客户区尺寸并触发重绘。
- `WM_PAINT` 使用 `GetClientRect` 获取实时客户区。
- 先填充黑色背景。
- 使用 `SetStretchBltMode(hdc, HALFTONE)` 和 `StretchDIBits` 等比例绘制。
- 窗口被遮挡、最小化后恢复或改变大小时，使用最近一帧重绘。
- 没有有效帧时只显示黑色背景。

### Linux

- 监听 `ConfigureNotify` 和 `Expose`。
- 远程窗口可以自由放大和缩小。
- 初始窗口按远程宽高比适配到当前显示器约 80%，较小画面不主动放大。
- `ConfigureNotify` 产生新的单槽位缩放请求。
- `Expose` 使用最近准备好的显示缓冲区重绘。
- 新尺寸缓冲区尚未完成时显示黑色背景，不拉伸尺寸不匹配的旧缓冲。

## 鼠标坐标映射

鼠标坐标不再直接按照整个窗口映射，而是按照实际画面显示区域转换：

```text
remote_x = (mouse_x - display_x) * remote_width / display_width
remote_y = (mouse_y - display_y) * remote_height / display_height
```

- 位于上下或左右黑边时不发送远程鼠标事件。
- 映射结果限制在远程桌面有效范围。
- 鼠标移动、按键、点击、双击和滚轮共用同一个转换函数。
- 键盘协议不受显示缩放影响。

## 编译

项目使用 C++17。`stb_image` 和 `stb_image_write` 已包含在 `third_party`，不需要单独安装 JPEG 库。

### Windows

需要 CMake 和支持 C++17 的编译器。使用 MinGW：

```powershell
cmake -S windows -B windows/build -G "MinGW Makefiles"
cmake --build windows/build --target win_server win_client -j 4
```

如果构建目录已经由 Ninja 或 Visual Studio 配置，继续使用同一生成器：

```powershell
cmake --build windows/build --target win_server win_client -j 4
```

生成：

```text
windows/build/win_server.exe
windows/build/win_client.exe
```

### Linux

安装依赖：

```bash
sudo apt update
sudo apt install build-essential libx11-dev libxext-dev xdotool
```

编译：

```bash
g++ -std=c++17 -Wall -Wextra -Wpedantic \
    linux/server.cpp -o linux/server \
    -lX11 -lXext -pthread

g++ -std=c++17 -Wall -Wextra -Wpedantic \
    linux/client.cpp -o linux/client \
    -lX11 -pthread
```

## 运行

Windows 和 Linux server 默认监听：

```text
0.0.0.0:9999
```

### Windows server → 任意 client

Windows：

```powershell
.\windows\build\win_server.exe
```

Linux client：

```bash
./linux/client
```

或启动 Windows client：

```powershell
.\windows\build\win_client.exe
```

### Linux server → 任意 client

Linux：

```bash
./linux/server
```

然后启动 Windows client 或 Linux client，在连接界面输入 Linux 主机地址和端口。

两个客户端都保留现有连接界面，支持 IPv4、域名和自定义端口。断线后会返回连接界面。

Linux client 也可以用命令行参数预填连接信息：

```bash
./linux/client 192.168.1.10 9999
./linux/client example.tcp.cpolar.cn 12345
```

参数只用于预填，仍会先显示连接界面。

## server.conf

客户端会从可执行文件所在目录读取并保存服务器地址。

Windows client 使用两行格式：

```text
192.168.1.10
9999
```

Linux client 使用键值格式：

```text
ip=192.168.1.10
port=9999
```

`server.conf` 已被 `.gitignore` 忽略，不应提交。

## cpolar

cpolar 仍使用普通 TCP 隧道，不需要修改应用协议。例如将被控端本地 `9999` 暴露到公网：

```bash
cpolar tcp 9999
```

将 cpolar 返回的域名和端口输入客户端连接界面即可。使用公网隧道前请注意：当前项目没有身份认证和加密，暴露端口存在安全风险。

## 当前限制

- 没有身份认证、授权、加密或完整的公网安全机制。
- 自定义结构采用固定 32 位字段并保持现有直接序列化方式，没有增加网络字节序转换或协议版本协商。
- 屏幕采用完整 JPEG 帧传输，没有差分编码、码率控制或视频编码。
- JPEG 编解码和双线性缩放会消耗 CPU，实际帧率和延迟需要在目标机器上测量。
- Linux 输入模拟依赖 `xdotool`，主要面向 X11；不支持原生 Wayland 输入控制。
- Windows server 当前使用主桌面 GDI 捕获逻辑，不包含专门的多显示器布局管理。
- 项目按单客户端连接流程设计。
- 最新四端 JPEG、自由缩放和重连流程已经通过编译，但仍需实际 GUI 和网络回归测试。

## 建议测试清单

1. 分别测试四种 server/client 组合。
2. 使用纯红、纯蓝和渐变画面检查红蓝通道。
3. 快速放大、缩小两个客户端窗口，检查宽高比和黑边。
4. 在黑边移动、点击和滚轮，确认不会发送远程鼠标事件。
5. 检查画面四角的鼠标坐标映射。
6. 遮挡、最小化并恢复窗口，检查最近一帧重绘。
7. 断线后重新连接，并测试远程分辨率变化。
8. 通过局域网和 cpolar 分别观察日志和画面时效性。
9. 在不同 X11 Visual、DPI、窗口管理器和桌面分辨率下验证显示。

## 说明

本项目主要用于学习和展示以下内容：

- C++ 跨平台网络编程
- TCP 应用层消息边界处理
- Win32/GDI 与 X11/XShm 桌面捕获
- 内存 JPEG 编解码
- GUI 重绘、等比例缩放和输入坐标映射
- 多线程图像流水线与最新帧策略

请在受控网络和测试环境中使用。
