# Windows/Linux Qt four-end verification

Date: 2026-09-29

## Build environments

- Windows: Qt 6.11.2, MinGW 13.1.0, CMake 3.30.5, Ninja
- Linux: Ubuntu 22.04 under WSL, Qt 6.2.4, GCC 11.4.0, CMake 3.22.1, Ninja

Both platforms built the unified `remote_control` application and their native server executable successfully.

## Startup verification

- Windows `remote_control.exe` opened a Chinese main window, automatically launched `win_server.exe`, and listened on `0.0.0.0:9999`.
- Linux `remote_control` launched with the xcb platform backend, automatically launched `linux_server`, and listened on `0.0.0.0:9999`.
- Users only launch the Qt application. The native server process is an internal component managed by the `允许远程控制` page.

## Four connection combinations

| Controller | Controlled host | Result |
| --- | --- | --- |
| Windows Qt | Windows server | TCP session established |
| Linux Qt | Linux server | TCP session established |
| Windows Qt | Linux server | Protocol hello received; heartbeat RTT 1 ms |
| Linux Qt | Windows server | TCP session established |

## Debugging record

- The first Windows build selected a system MinGW 15 compiler that was incompatible with the Qt MinGW 13 libraries. The clean build now pins Qt's matching MinGW toolchain.
- Ubuntu Qt 6.2 does not provide `qt_standard_project_setup`; CMake now falls back to `AUTOMOC`, `AUTOUIC`, and `AUTORCC` for older Qt 6 versions.
- Ubuntu required `libgl1-mesa-dev` before Qt Widgets could resolve its OpenGL dependency.
- WSLg selected Wayland even though this project requires X11 input injection. Linux now defaults to Qt's xcb backend unless the user explicitly sets another platform.
- Windows could open a raw TCP connection to Linux, while the Qt client initially did not. The client now explicitly uses `QNetworkProxy::NoProxy`, which fixed direct private-network connections and produced a successful cross-platform hello and heartbeat.
- One-shot WSL commands reclaim background processes when the command exits. Cross-platform verification therefore kept the WSL server session alive instead of treating that environment behavior as a protocol failure.

## Remaining physical-device checks

The four protocol paths and both Qt builds are verified. A physical Linux desktop should still be used to confirm X11 screen capture, keyboard injection, mouse injection, desktop fonts, and packaging on the target distribution. Native Wayland control remains unsupported.
