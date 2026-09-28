# Automatic reconnect review

## Scope

This change adds automatic reconnect after an established session is lost:

- Windows client reconnect state machine.
- Linux client reconnect state machine.
- Windows server repeated accept loop, matching the existing Linux server behavior.
- Mandatory first screen frame for every new server session.

It does not add background startup retries after an invalid first manual connection, multiple concurrent clients, session resumption, authentication, encryption, or a Qt interface.

## Backoff

Both clients use the shared `reconnectDelaySeconds` helper:

```text
attempt 1: 1 second
attempt 2: 2 seconds
attempt 3: 4 seconds
attempt 4+: 8 seconds
```

An established connection resets the attempt counter. If that session disconnects, the next sequence starts again at one second.

## Client state flow

```text
CONNECTED
    -> connection lost
    -> RECONNECT_WAIT
    -> CONNECTING
    -> CONNECTED on success
    -> RECONNECT_WAIT with a larger delay on failure
```

The Windows client uses a Win32 timer so the GUI message loop stays responsive. The Linux client stores a `steady_clock` deadline and checks it from the existing X11 event loop. Neither client sleeps on the GUI thread.

A manual connect action cancels the pending reconnect timer/deadline, resets the attempt counter, and immediately uses the address currently shown in the connection form.

## Server lifecycle

The Windows server now keeps its listening socket open. After a client session ends it:

1. Marks the session as stopped.
2. Calls `shutdown` to wake blocked socket operations.
3. Joins the screen thread.
4. Closes only the connected socket.
5. Returns to `accept` on the original listening socket.

The previous frame cache remains reusable, but frame ID 1 bypasses duplicate-frame suppression. A newly connected client therefore receives an initial frame even when the desktop did not change between sessions.

## Local verification

Build verification completed on 2026-09-28:

- MinGW Windows server: passed.
- MinGW Windows client: passed.
- WSL Linux server with `-Wall -Wextra -Wpedantic`: passed.
- WSL Linux client with `-Wall -Wextra -Wpedantic`: passed.

Runtime verification completed on one Windows machine with WSL:

1. A real Windows GUI client connected to the Linux server.
2. The Linux server was forcibly stopped; the client returned to its connection view and began retrying.
3. The Linux server was restarted; the same client automatically returned to the connected view without another click.
4. The Windows server accepted four sequential local client connections without restarting.
5. Two later Windows server sessions each received `CMD_HELLO` followed by `CMD_SCREEN_BEGIN`, confirming that a new session receives its first frame.

The Linux GUI client was built and launched under WSLg, but WSLg did not accept the scripted `xdotool` click/Enter injection used by this test. Its automatic reconnect runtime path remains a manual GUI check; this document does not claim that path was automated successfully.

These tests are local only. They do not cover physical two-machine networking, router recovery, Wi-Fi roaming, NAT expiry, or public tunnel behavior.
