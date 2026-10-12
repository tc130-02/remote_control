Remote Control Windows 被控端演示包

用途：在第二台 Windows 电脑上运行远程桌面服务端。

使用步骤：
1. 解压本压缩包，保持所有 EXE 和 DLL 在同一目录。
2. 双击 Start-Server.cmd。
3. Windows 防火墙首次询问时，仅允许“专用网络”。
4. 打开 PowerShell，运行 ipconfig，记下当前网卡的 IPv4 地址。
5. 在控制端 Qt 客户端中输入该 IPv4 地址和端口 9999。

注意：
- 仅在可信局域网中演示，不要把 9999 端口暴露到公网。
- 当前项目没有身份认证和传输加密。
- 关闭服务端窗口即可停止服务。
