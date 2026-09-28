# Linux Server Reconnect Test

## Reproduced behavior

The Linux server originally accepted one client, stopped the screen thread after
that client disconnected, closed the listening socket, and exited. A client could
return to its connection screen but could not reconnect unless the server process
was started again.

## Implementation

The listening socket now remains alive in an outer accept loop. Each accepted
client gets a fresh session:

1. Reset the per-session running flag.
2. Send the hello packet.
3. Start the screen sender.
4. Run the receive loop.
5. Shut down the disconnected client socket.
6. Join the screen thread and release the client socket.
7. Return to `accept` for the next connection.

Socket creation now checks each failure and enables `SO_REUSEADDR`, allowing the
server process to bind again promptly after a restart.

## Manual verification

Build and start the server in WSL:

```bash
g++ -std=c++17 -Wall -Wextra -Wpedantic \
    linux/server.cpp -o /tmp/remote_control_server \
    -lX11 -lXext -pthread

/tmp/remote_control_server
```

From PowerShell, connect and disconnect twice:

```powershell
1..2 | ForEach-Object {
    $client = [System.Net.Sockets.TcpClient]::new()
    $client.Connect("127.0.0.1", 9999)
    Start-Sleep -Milliseconds 500
    $client.Close()
    Start-Sleep -Seconds 1
}
```

Expected server log:

```text
client connected
client disconnected; waiting for reconnect
client connected
client disconnected; waiting for reconnect
```
