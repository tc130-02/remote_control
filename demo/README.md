# 远程桌面项目：应试演示包

这套材料用于“独立作品介绍（录屏不超过 5 分钟）”。主流程采用 Windows 服务端与 Qt 客户端，因为界面、连接状态、画面信息和键鼠开关都能直接展示；Linux 端作为跨平台构建证明，不在正式录屏中临时折腾环境。

## 已验证状态（2026-10-12）

- `win_server.exe`：编译通过。
- `win_client.exe`：编译通过，作为备用客户端。
- `qt_client.exe`：编译通过，Qt 运行库已部署到同一目录。
- `linux_server`、`linux_client`：在 Ubuntu 22.04 / WSL2 中编译通过。
- 本机冒烟测试：服务端监听 `0.0.0.0:9999`，Qt 客户端成功建立连接并保持运行。

Windows 可执行文件：

- 服务端：`build/native-gcc/win_server.exe`
- 原生备用客户端：`build/native-gcc/win_client.exe`
- 正式演示客户端：`build/qt-client/qt_client.exe`

## 最推荐的正式录制方式

最好使用两台 Windows 电脑，或者一台电脑加 Windows 虚拟机：

1. 被控端复制整个 `build/native-gcc` 目录，运行 `win_server.exe`。
2. 控制端复制整个 `build/qt-client` 目录，运行 `qt_client.exe`。
3. 控制端输入被控端局域网 IPv4 地址，端口保持 `9999`，点击 Connect。
4. Windows 防火墙第一次询问时，仅允许“专用网络”。
5. 成功后先缩放客户端窗口，再做鼠标点击和英文键盘输入。

查被控端 IPv4 地址：打开 PowerShell，运行 `ipconfig`，找到当前网卡的“IPv4 地址”。两台机器应在同一局域网。

## 只有一台电脑时

双击或在 PowerShell 中运行：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\demo\Start-Local-Demo.ps1
```

脚本会启动本机服务端和 Qt 客户端，并自动连接 `127.0.0.1:9999`。本机连接会形成“屏幕套屏幕”的递归画面，所以只适合练习和证明链路，不是最佳正式录屏效果。

结束练习：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\demo\Stop-Local-Demo.ps1
```

该停止脚本只会结束当前仓库构建目录中的 `win_server`、`win_client` 和 `qt_client`。

## 正式演示动作

连接成功后，只做以下四个动作，稳定且足够说明项目：

1. 调整客户端窗口大小，说明远程画面保持比例，黑边不参与坐标映射。
2. 在远程画面中移动鼠标并点击被控端记事本。
3. 输入英文 `remote-control-demo`，再按 Backspace、Enter，展示按下/释放事件。
4. 取消 “Enable remote keyboard and mouse input”，再次点击画面，说明画面仍更新但控制被暂停；然后重新勾选。

不要在正式录屏时测试公网穿透、临时安装依赖或输入密码。这些都容易制造与作品本身无关的失败点。

## 录制时打开的材料

- `demo/architecture.png`：录屏时直接展示的整体数据流图。
- `demo/architecture.svg`：同一张架构图的可编辑矢量版本。
- `demo/五分钟演示稿.md`：逐段口播与操作。
- `demo/录制检查单.md`：录制前后检查。
- `demo/代码展示定位.md`：提前定位需要展示的代码，避免录屏时临时搜索。

## 面试追问时可讲的真实边界

- 当前协议没有认证与加密，只适合可信网络或实验环境。
- 当前发送完整 JPEG 帧，不是差分传输或 H.264/H.265 视频流。
- Linux 输入依赖 X11 与 `xdotool`，没有原生支持 Wayland。
- 当前是单客户端、单屏幕，没有剪贴板、文件传输和多显示器选择。
- 协议结构目前直接使用本机字节序，后续应增加版本号、网络字节序和更严格的序列化。

主动说出这些边界不会减分；它能说明你知道“能跑的作品”和“可上线产品”之间还缺什么。
