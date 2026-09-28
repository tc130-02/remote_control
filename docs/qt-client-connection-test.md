# Qt Client Connection Test

## Environment

- Windows local test host
- Qt 6.11.2 `mingw_64`
- MinGW 13.1.0
- CMake 3.30.5
- Ninja 1.12.1

## Build verification

The Qt-enabled build successfully produced all three Windows targets:

- `qt_client`
- `win_client`
- `win_server`

A second configuration with `BUILD_QT_CLIENT=OFF` also built `win_client` and
`win_server`. This confirms that Qt remains optional for the existing targets.

## Local connection verification

1. Started `win_server` on `127.0.0.1:9999`.
2. Started `qt_client` with `--host 127.0.0.1 --port 9999 --connect`.
3. The server accepted the connection and received
   `hello qt windows client`.
4. The server streamed screen packets while the Qt client kept parsing the
   protocol stream.
5. Multiple heartbeat PING/PONG exchanges completed with local RTT values of
   approximately 1-3 ms.

During the first integration pass, the Qt client generated a new timestamp for
PONG packets. That made the RTT measurement describe PONG creation time instead
of the original PING round trip. The implementation now changes only the packet
command and echoes the original heartbeat payload, matching the existing four
endpoints.

## Automatic reconnect verification

1. Stopped `win_server` while leaving `qt_client` running.
2. Confirmed that the Qt process remained active and retried with the shared
   1, 2, 4, 8 second backoff policy.
3. Restarted `win_server` on the same port.
4. A transient TCP session was reset during the restart race; the next retry
   connected successfully without user action.
5. The new session sent the Qt hello packet and resumed heartbeat exchanges.

## Current scope

This review step covers the Qt connection-management window only. It consumes
screen packets to keep the stream synchronized, but does not render frames or
send mouse and keyboard input yet.
