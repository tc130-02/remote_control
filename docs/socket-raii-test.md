# Server socket RAII review

## Scope

This change adds unique ownership for server-side listening and connected sockets on Windows and Linux. It also gives Windows Winsock startup and cleanup an RAII lifetime.

Client active sockets are intentionally not changed in this review. They are currently shared between GUI, connect, and receive threads. Converting them safely requires a connection-session owner rather than wrapping the same raw value independently in multiple RAII objects.

## SocketHandle ownership

`SocketHandle` has the following rules:

- A default object owns no socket.
- A constructed object owns exactly one native socket.
- Copy construction and copy assignment are disabled.
- Move operations transfer the native value and invalidate the source.
- `release` transfers ownership to a caller without closing.
- `reset` closes the currently owned socket before accepting a replacement.
- Destruction closes a still-owned socket exactly once.

The wrapper uses `close` on Linux and `closesocket` on Windows. Factory functions create a local owner and call `release` only after socket setup succeeds. Any earlier return therefore closes a partially initialized socket automatically.

## shutdown versus close

`shutdown` and `close` have separate jobs in the server session lifecycle:

1. `shutdown` disables socket I/O and wakes a screen thread blocked in `send`.
2. The main thread joins the screen thread so no code can still use the native value.
3. `SocketHandle` reaches the end of the loop iteration and closes the native handle.

Closing before joining would allow the operating system to reuse that numeric descriptor while the old thread still holds it. The explicit shutdown-join-destroy order prevents that use-after-close race.

## Winsock lifetime

Windows socket calls require a successful `WSAStartup`. `WinsockRuntime` records whether startup succeeded and calls `WSACleanup` only for a successful startup. It is declared before the listening `SocketHandle`, so local objects are destroyed in reverse order: connected socket, listening socket, then Winsock runtime.

## Local verification

Verification completed on 2026-09-28:

- MinGW Windows server build: passed.
- MinGW Windows client build: passed.
- WSL Linux server build with `-Wall -Wextra -Wpedantic`: passed.
- WSL Linux client build with `-Wall -Wextra -Wpedantic`: passed.
- Ownership test: move construction invalidated the source object.
- Ownership test: `release` transferred the same native value without closing it.
- Ownership test: leaving scope closed an owned Windows socket exactly once.
- Windows server accepted two sequential local sessions; both received the server hello packet.
- Linux server accepted two sequential local sessions; both received the server hello packet.

The abrupt test clients close immediately after receiving hello, so screen threads may log an expected send failure while shutdown is propagating. The server remains alive and accepts the following connection.
