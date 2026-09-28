# Heartbeat detection review

## Scope

This change adds heartbeat detection to all four endpoints:

- Windows server
- Windows client
- Linux server
- Linux client

It does not add automatic reconnect, multiple-client support, authentication, encryption, or a Qt interface.

## Protocol

Two commands are added to the existing packet stream:

```text
CMD_HEARTBEAT_PING = 30
CMD_HEARTBEAT_PONG = 31
```

Both carry the same 16-byte payload:

```cpp
struct HeartbeatPayload {
    int64_t sequence;
    int64_t sent_at_ms;
};
```

`sequence` identifies a probe. `sent_at_ms` uses the sender's monotonic clock. Because the receiver echoes the payload unchanged, the original sender can calculate round-trip time without synchronizing system clocks.

## State machine

Each receive loop keeps three values:

- `last_receive_ms`: time of the last complete valid packet.
- `last_ping_ms`: time of the last probe.
- `heartbeat_sequence`: sequence number for the next probe.

The loop checks the connection every 500 ms:

1. If no complete valid packet has arrived for 3 seconds, send `PING`.
2. If `PING` arrives, validate its payload and echo it as `PONG`.
3. If `PONG` arrives, validate it and calculate round-trip time.
4. If no complete valid packet has arrived for 10 seconds, close the current connection.

Normal screen, keyboard, and mouse packets also refresh `last_receive_ms`. Heartbeats are therefore only sent when that direction of the connection is otherwise idle.

## Why sends are serialized

TCP preserves byte order but does not preserve application message boundaries. A screen thread and heartbeat handler can call `send` at the same time. Without a mutex, bytes from two encoded packets could be interleaved, causing the peer to decode an invalid header or body.

Every endpoint now locks its send path for one complete encoded packet. Heartbeat packets may appear between screen chunks, which is valid because each chunk is already an independent framed packet.

## Local verification

Build verification completed on 2026-09-28:

- MinGW Windows server: passed.
- MinGW Windows client: passed.
- WSL Linux server with `-Wall -Wextra -Wpedantic`: passed.
- WSL Linux client with `-Wall -Wextra -Wpedantic`: passed.

Runtime verification completed on one Windows machine with WSL:

1. Linux server and real Windows GUI client stayed connected and exchanged two heartbeat probes. Logged round-trip time was 1 ms for both probes.
2. A local client that read server data but never replied was disconnected by the Linux server after 10.01 seconds.
3. The real Windows GUI client connected to a silent local server, then returned from the connected view to the connection view after about 10 seconds.
4. A local client that never replied was disconnected by the Windows server after 9.94 seconds.

The Windows server timeout test exposed an existing cleanup race: the socket was closed before the screen thread had exited. Cleanup now calls `shutdown` to wake blocked I/O, joins the screen thread, and only then closes the socket handle.

These tests cover local protocol behavior and timeout decisions. They do not cover physical two-machine networking, router failure, Wi-Fi roaming, NAT expiry, or public tunnel behavior.
