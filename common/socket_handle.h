#pragma once

#ifdef _WIN32
#include <winsock2.h>

using NativeSocket = SOCKET;

inline NativeSocket invalidNativeSocket()
{
    return INVALID_SOCKET;
}

inline void closeNativeSocket(NativeSocket socket)
{
    closesocket(socket);
}

inline void shutdownNativeSocket(NativeSocket socket)
{
    shutdown(socket, SD_BOTH);
}

class WinsockRuntime {
public:
    WinsockRuntime()
    {
        error_code_ = WSAStartup(MAKEWORD(2, 2), &data_);
    }

    ~WinsockRuntime()
    {
        if (error_code_ == 0) {
            WSACleanup();
        }
    }

    WinsockRuntime(const WinsockRuntime&) = delete;
    WinsockRuntime& operator=(const WinsockRuntime&) = delete;

    bool valid() const
    {
        return error_code_ == 0;
    }

    int errorCode() const
    {
        return error_code_;
    }

private:
    WSADATA data_ = {};
    int error_code_ = 0;
};

#else
#include <sys/socket.h>
#include <unistd.h>

using NativeSocket = int;

inline NativeSocket invalidNativeSocket()
{
    return -1;
}

inline void closeNativeSocket(NativeSocket socket)
{
    close(socket);
}

inline void shutdownNativeSocket(NativeSocket socket)
{
    shutdown(socket, SHUT_RDWR);
}
#endif

class SocketHandle {
public:
    SocketHandle() = default;

    explicit SocketHandle(NativeSocket socket)
        : socket_(socket)
    {
    }

    ~SocketHandle()
    {
        reset();
    }

    SocketHandle(const SocketHandle&) = delete;
    SocketHandle& operator=(const SocketHandle&) = delete;

    SocketHandle(SocketHandle&& other) noexcept
        : socket_(other.release())
    {
    }

    SocketHandle& operator=(SocketHandle&& other) noexcept
    {
        if (this != &other) {
            reset(other.release());
        }
        return *this;
    }

    bool valid() const
    {
        return socket_ != invalidNativeSocket();
    }

    NativeSocket get() const
    {
        return socket_;
    }

    NativeSocket release()
    {
        NativeSocket socket = socket_;
        socket_ = invalidNativeSocket();
        return socket;
    }

    void reset(NativeSocket socket = invalidNativeSocket())
    {
        if (socket_ == socket) {
            return;
        }

        if (valid()) {
            closeNativeSocket(socket_);
        }
        socket_ = socket;
    }

    void shutdownBoth()
    {
        if (valid()) {
            shutdownNativeSocket(socket_);
        }
    }

private:
    NativeSocket socket_ = invalidNativeSocket();
};
