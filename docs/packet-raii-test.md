# Packet encoder RAII review

## Scope

This change replaces the packet encoder's manually allocated byte buffer with `std::vector<char>` across all four endpoints. It does not change command values, header layout, body layout, byte order, socket ownership, reconnect behavior, heartbeat behavior, or the Qt/UI layer.

## Previous ownership model

The old encoder returned a raw pointer allocated with `malloc` and wrote the byte count through a second output pointer:

```cpp
char* buffer = encodePacket(&packet, &length);
sendAll(socket, buffer, length);
free(buffer);
```

Every caller had to remember the matching `free`. Adding an early return between allocation and cleanup would leak the buffer, and the type did not express who owned the returned memory.

## RAII ownership model

The encoder now returns one value:

```cpp
std::vector<char> buffer = encodePacket(packet);
```

The vector owns its allocation, exposes contiguous storage through `data()`, and carries its byte count through `size()`. Its destructor releases the allocation on normal return, early return, or exception unwinding.

`std::unique_ptr<char, Deleter>` could safely wrap `malloc`, but it would still require a separate length variable and a custom `free` deleter. A vector directly models a variable-length contiguous byte sequence, so it is the simpler ownership type for an encoded network packet.

Returning the vector by value does not require a persistent extra copy. Modern C++ permits return-value optimization, and otherwise the vector is moved by transferring its pointer, size, and capacity.

## Wire compatibility

The encoder still writes fields in the same order:

```text
offset 0: magic    (4 bytes)
offset 4: cmd      (4 bytes)
offset 8: body_len (4 bytes)
offset 12: body
```

An invalid body length still causes encoding to fail. Failure is now represented by an empty vector; a valid packet is always at least the 12-byte header, so the states are unambiguous.

## Local verification

Verification completed on 2026-09-28:

- MinGW Windows server build: passed.
- MinGW Windows client build: passed.
- WSL Linux server build with `-Wall -Wextra -Wpedantic`: passed.
- WSL Linux client build with `-Wall -Wextra -Wpedantic`: passed.
- Protocol round-trip test: a three-byte body encoded to 15 bytes and decoded back to the original magic, command, length, and body.
- Invalid-length test: a body length larger than `PACKET_DATA_SIZE` produced an empty buffer.
- Real local session: the Windows GUI client connected to the Linux server, exchanged `HELLO`, and returned heartbeat responses.

These checks show that ownership changed while the transmitted byte format and runtime connection behavior remained unchanged.
