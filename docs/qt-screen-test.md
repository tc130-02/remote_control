# Qt Remote Screen Test

## Build verification

The Qt client, Windows client, and Windows server were built together with Qt
6.11.2, MinGW 13.1.0, CMake 3.30.5, and Ninja 1.12.1.

## Local display verification

1. Started the Windows server on `127.0.0.1:9999`.
2. Connected the Qt client with its command-line auto-connect option.
3. The server received `hello qt windows client` and streamed JPEG frames.
4. The Qt window displayed the 1920 x 1080 desktop with aspect-ratio scaling
   and black letterboxing.
5. The frame status showed decoded frame number, encoded size, and dropped-frame
   count while the window remained responsive.
6. Heartbeat PING/PONG traffic continued during the local display test.

The visual test screenshot is a local ignored build artifact and is not
committed because it contains the tester's desktop contents.

## Public tunnel pre-check

A temporary cpolar TCP tunnel was opened from a public endpoint to local port
9999. `Test-NetConnection` succeeded, and the Qt client connected through the
public endpoint and displayed the remote desktop. This verifies the public TCP
path, Qt packet assembly, JPEG decoding, and painting on one computer.

During the self-tunnel test, the server sent frames faster than the public
tunnel delivered them and later closed the session on heartbeat timeout. The
likely cause is queued full-screen JPEG data delaying heartbeat responses in
the TCP stream. This is an inference from the observed frame traffic and
timeout, not yet a confirmed root cause. The physical two-computer public test
must record whether the same behavior occurs and measure how long the session
remains usable.

The temporary public address is intentionally not recorded because cpolar free
endpoints change between runs and the protocol currently has no authentication
or encryption.

## Physical two-computer public verification

The Windows Qt client on the desktop connected through cpolar to the Windows
server running on a separate laptop. The public TCP probe succeeded before the
GUI connection was started. The Qt client then reported `Connected`, displayed
the laptop's 1920 x 1200 desktop, decoded the first remote frame without drops,
and measured a heartbeat round trip of approximately 54 ms. This confirms the
Windows-to-Windows public path across two physical computers.

The test confirmed connection, heartbeat, frame transport, JPEG decoding, and
aspect-ratio display. It was not used as a long-duration stability or bandwidth
benchmark.

## Current scope

The Qt client now displays remote frames but does not send mouse or keyboard
input. The current public test is therefore a screen-viewing test only.
