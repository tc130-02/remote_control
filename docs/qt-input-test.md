# Qt keyboard and mouse control test

Date: 2026-09-29

## Environment

- Controller: Windows Qt client
- Controlled host: Windows server
- Network: public TCP tunnel forwarding to server port 9999
- Remote screen: 1920 x 1200 JPEG frames

## Verified behavior

- The Qt client connected through the public tunnel and displayed the remote screen.
- Mouse movement and button input controlled the remote Windows host.
- Keyboard down and up events reached the server and controlled the remote host.
- Closing the Qt client produced a normal peer disconnect on the server.
- The input enable switch releases tracked keys and buttons before pausing input.

## Debugging notes

- An initial short connection was a TCP reachability probe. It disconnected before sending a protocol hello, so the server's first send failure was expected.
- A mouse movement bug was found during review: coordinate mapping updated the previous position before duplicate suppression, causing every move to look duplicated. The previous position is now updated only after an event is sent.
- Public-tunnel screen traffic showed visible latency and one heartbeat round trip of about 11 seconds. Keyboard and mouse input remained responsive. Screen transport performance is intentionally deferred to a separate change.
