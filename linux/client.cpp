#include <iostream>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <string>
#include <fstream>
#include <cerrno>
#include <limits.h>
#include <vector>
#include <algorithm>
#include <chrono>

#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include "../common/packet.h"

int g_server_port = 9999;

// ================================
// 全局变量：屏幕帧接收状态
// ================================
std::vector<unsigned char> g_frame_buffer;

int g_frame_id = -1;
int g_frame_width = 0;
int g_frame_height = 0;
int g_frame_total_size = 0;
int g_frame_received_size = 0;
bool g_receiving_frame = false;

// ================================
// 全局变量：X11 显示窗口状态
// ================================
Display* g_display = nullptr;
Window g_window = 0;
GC g_gc = 0;

int g_window_width = 0;
int g_window_height = 0;

bool g_mouse_move_pending = false;
int g_pending_mouse_x = 0;
int g_pending_mouse_y = 0;
std::chrono::steady_clock::time_point g_last_mouse_send_time =
    std::chrono::steady_clock::now() - std::chrono::milliseconds(100);

// ================================
// 函数声明
// ================================
bool sendAll(int sock, const char* buf, int len);
bool sendPacket(int sock, const Packet& pkt);
bool loadServerAddress(sockaddr_in& server_addr);

Packet buildPacket(int cmd, const char* msg);
Packet buildRawPacket(int cmd, const char* buffer, int len);

void sendKeyEvent(int sock, int key_status, const char* key);
void sendKeyClick(int sock, const char* key);

void sendMouseEvent(int sock, int action, int button, int x, int y);
bool convertLocalToRemote(int local_x, int local_y, int& remote_x, int& remote_y);
void handleX11Events(int sock);

bool initDisplayWindow(int width, int height);
void drawFrameToWindow();
void closeDisplayWindow();

void handleScreenBegin(const Packet& pkt);
void handleScreenChunk(const Packet& pkt);
void handleScreenEnd(const Packet& pkt);

void handlePacket(const Packet& pkt);
void recvLoop(int sock);

int main()
{
    // ================================
    // 1. 创建 socket
    // ================================
    int sock = socket(AF_INET, SOCK_STREAM, 0);

    if (sock < 0)
    {
        perror("socket");
        return 1;
    }

    // ================================
    // 2. 配置 Windows server 地址
    // 说明：
    //   这里需要改成当前 Windows server 所在机器的 IP。
    // ================================
    sockaddr_in server_addr = {};

    if (!loadServerAddress(server_addr))
    {
        close(sock);
        return 1;
    }

    std::cout << "connecting to Windows server..." << std::endl;

    // ================================
    // 3. 连接 Windows server
    // ================================
    int ret = connect(
        sock,
        (sockaddr*)&server_addr,
        sizeof(server_addr)
    );

    if (ret < 0)
    {
        perror("connect");
        close(sock);
        return 1;
    }

    int no_delay = 1;
    if (setsockopt(
            sock,
            IPPROTO_TCP,
            TCP_NODELAY,
            &no_delay,
            sizeof(no_delay)
        ) != 0)
    {
        std::cout << "set TCP_NODELAY failed" << std::endl;
    }

    std::cout << "connect success!" << std::endl;

    // ================================
    // 4. 发送 hello，验证 Linux -> Windows 链路
    // ================================
    Packet hello = buildPacket(CMD_HELLO, "hello from linux client");

    if (sendPacket(sock, hello))
    {
        std::cout << "send hello success" << std::endl;
    }
    else
    {
        std::cout << "send hello failed" << std::endl;
    }

    // ================================
    // 5. 持续接收 Windows server 发来的 hello 和屏幕帧
    // 说明：
    //   之前这里有 sendKeyClick(sock, "A") 测试代码，
    //   现在已经删除，避免每次启动都往 Windows 当前窗口输入 A。
    // ================================
    recvLoop(sock);

    // ================================
    // 6. 清理资源
    // ================================
    closeDisplayWindow();
    close(sock);

    return 0;
}

bool loadServerAddress(sockaddr_in& server_addr)
{
    char executable_path[PATH_MAX] = {0};
    ssize_t path_length = readlink("/proc/self/exe", executable_path, sizeof(executable_path) - 1);
    if (path_length < 0)
    {
        perror("failed to get program directory");
        return false;
    }
    executable_path[path_length] = '\0';

    std::string config_path(executable_path);
    size_t separator = config_path.find_last_of('/');
    if (separator != std::string::npos)
    {
        config_path.resize(separator + 1);
    }
    else
    {
        config_path.clear();
    }
    config_path += "server.conf";

    std::ifstream config(config_path);
    if (!config.is_open())
    {
        std::cout << "server.conf not found: " << config_path << std::endl;
        return false;
    }

    auto trim = [](const std::string& value)
    {
        size_t first = value.find_first_not_of(" \t\r\n");
        if (first == std::string::npos)
        {
            return std::string();
        }

        size_t last = value.find_last_not_of(" \t\r\n");
        return value.substr(first, last - first + 1);
    };

    std::string server_host;
    std::string server_port_text;
    std::string line;

    while (std::getline(config, line))
    {
        line = trim(line);
        if (line.empty())
        {
            continue;
        }

        size_t separator = line.find('=');
        if (separator == std::string::npos)
        {
            continue;
        }

        std::string key = trim(line.substr(0, separator));
        std::string value = trim(line.substr(separator + 1));

        if (key == "ip")
        {
            server_host = value;
        }
        else if (key == "port")
        {
            server_port_text = value;
        }
    }

    if (server_host.empty())
    {
        std::cout << "ip is missing in server.conf: " << config_path << std::endl;
        return false;
    }

    if (server_port_text.empty())
    {
        std::cout << "port is missing in server.conf: " << config_path << std::endl;
        return false;
    }

    char* port_end = nullptr;
    long server_port = std::strtol(server_port_text.c_str(), &port_end, 10);
    if (port_end == server_port_text.c_str() || *port_end != '\0'
        || server_port < 1 || server_port > 65535)
    {
        std::cout << "invalid port in server.conf: " << server_port_text << std::endl;
        return false;
    }
    g_server_port = static_cast<int>(server_port);

    addrinfo hints = {};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    addrinfo* address_list = nullptr;
    int resolve_result = getaddrinfo(
        server_host.c_str(),
        nullptr,
        &hints,
        &address_list
    );

    if (resolve_result != 0 || address_list == nullptr)
    {
        std::cout << "resolve host failed: " << server_host;
        if (resolve_result != 0)
        {
            std::cout << " (" << gai_strerror(resolve_result) << ")";
        }
        std::cout << std::endl;
        return false;
    }

    const sockaddr_in* resolved_address =
        reinterpret_cast<const sockaddr_in*>(address_list->ai_addr);
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr = resolved_address->sin_addr;
    server_addr.sin_port = htons(g_server_port);
    freeaddrinfo(address_list);

    std::cout << "server address: " << server_host << ":" << g_server_port << std::endl;
    return true;
}

// ================================
// 函数功能：确保完整发送 len 字节
// 说明：send 不保证一次把所有数据都发出去，所以需要循环发送
// ================================
bool sendAll(int sock, const char* buf, int len)
{
    int total = 0;

    while (total < len)
    {
        int n = send(sock, buf + total, len - total, MSG_NOSIGNAL);

        if (n <= 0)
        {
            std::cout << "send failed" << std::endl;
            return false;
        }

        total += n;
    }

    return true;
}

// ================================
// 函数功能：把 Packet 编码成字节流后发送
// ================================
bool sendPacket(int sock, const Packet& pkt)
{
    int len = 0;
    char* buf = encodePacket(&pkt, &len);

    if (buf == NULL || len <= 0)
    {
        std::cout << "encodePacket failed" << std::endl;
        return false;
    }

    bool ok = sendAll(sock, buf, len);

    free(buf);

    return ok;
}

// ================================
// 函数功能：构造字符串类型 Packet
// 适用场景：CMD_HELLO 这类 data 是普通字符串的命令
// ================================
Packet buildPacket(int cmd, const char* msg)
{
    Packet pkt;
    memset(&pkt, 0, sizeof(pkt));

    pkt.magic = PACKET_MAGIC;
    pkt.cmd = cmd;

    if (msg == nullptr)
    {
        pkt.body_len = 0;
        return pkt;
    }

    int len = strlen(msg);

    if (len >= PACKET_DATA_SIZE)
    {
        len = PACKET_DATA_SIZE - 1;
    }

    pkt.body_len = len;
    memcpy(pkt.data, msg, len);
    pkt.data[len] = '\0';

    return pkt;
}

// ================================
// 函数功能：构造二进制类型 Packet
// 适用场景：KeyEvent、MouseEvent、ScreenFrameInfo 这类结构体数据
// ================================
Packet buildRawPacket(int cmd, const char* buffer, int len)
{
    Packet pkt;
    memset(&pkt, 0, sizeof(pkt));

    pkt.magic = PACKET_MAGIC;
    pkt.cmd = cmd;

    if (buffer == nullptr || len <= 0)
    {
        pkt.body_len = 0;
        return pkt;
    }

    if (len > PACKET_DATA_SIZE)
    {
        std::cout << "raw packet too large" << std::endl;
        pkt.body_len = 0;
        return pkt;
    }

    pkt.body_len = len;
    memcpy(pkt.data, buffer, len);

    return pkt;
}

// ================================
// 函数功能：发送键盘按下/抬起事件给 Windows server
// ================================
void sendKeyEvent(int sock, int key_status, const char* key)
{
    if (key == nullptr)
    {
        return;
    }

    KeyEvent event = {};
    event.key_status = key_status;

    strncpy(event.key, key, sizeof(event.key) - 1);
    event.key[sizeof(event.key) - 1] = '\0';

    Packet pkt = buildRawPacket(
        CMD_KEY_EVENT,
        (const char*)&event,
        sizeof(KeyEvent)
    );

    sendPacket(sock, pkt);
}

// ================================
// 函数功能：发送一次完整按键点击事件
// 说明：一次点击 = keydown + 短暂等待 + keyup
// ================================
void sendKeyClick(int sock, const char* key)
{
    if (key == nullptr)
    {
        return;
    }

    sendKeyEvent(sock, KEY_STATUS_DOWN, key);
    usleep(100000);
    sendKeyEvent(sock, KEY_STATUS_UP, key);
}

// ================================
// 函数功能：发送鼠标事件给 Windows server
// 说明：
//   Linux client 将鼠标动作封装成 MouseEvent，
//   通过 CMD_MOUSE_EVENT 发送给 Windows server 执行。
// ================================
void sendMouseEvent(int sock, int action, int button, int x, int y)
{
    MouseEvent event = {};

    event.action = action;
    event.button = button;
    event.x = x;
    event.y = y;

    Packet pkt = buildRawPacket(
        CMD_MOUSE_EVENT,
        (const char*)&event,
        sizeof(MouseEvent)
    );

    sendPacket(sock, pkt);

    // 鼠标移动事件非常高频，不打印移动日志，避免终端刷屏和卡顿。
    if (action != MOUSE_ACTION_MOVE)
    {
        std::cout << "mouse event action=" << action
                  << " button=" << button
                  << " x=" << x
                  << " y=" << y
                  << std::endl;
    }
}

// ================================
// 函数功能：将 Linux client 窗口坐标换算成远程 Windows 屏幕坐标
// 说明：
//   g_window_width / g_window_height 是本地显示窗口大小。
//   g_frame_width / g_frame_height 是远程 Windows 原始屏幕大小。
// ================================
bool convertLocalToRemote(int local_x, int local_y, int& remote_x, int& remote_y)
{
    if (g_window_width <= 0 || g_window_height <= 0)
    {
        return false;
    }

    if (g_frame_width <= 0 || g_frame_height <= 0)
    {
        return false;
    }

    remote_x = local_x * g_frame_width / g_window_width;
    remote_y = local_y * g_frame_height / g_window_height;

    if (remote_x < 0)
    {
        remote_x = 0;
    }

    if (remote_y < 0)
    {
        remote_y = 0;
    }

    if (remote_x >= g_frame_width)
    {
        remote_x = g_frame_width - 1;
    }

    if (remote_y >= g_frame_height)
    {
        remote_y = g_frame_height - 1;
    }

    return true;
}

// ================================
// 函数功能：处理 Linux client 窗口中的 X11 输入事件
// 说明：
//   当前用于捕获用户在远程屏幕窗口中的鼠标移动和点击，
//   并把窗口坐标转换为 Windows 屏幕坐标后发送给 Windows server。
// ================================
void handleX11Events(int sock)
{
    if (g_display == nullptr || g_window == 0)
    {
        return;
    }

    while (XPending(g_display) > 0)
    {
        XEvent event = {};
        XNextEvent(g_display, &event);

        if (event.type == Expose)
        {
            drawFrameToWindow();
        }
        else if (event.type == ConfigureNotify)
        {
            g_window_width = event.xconfigure.width;
            g_window_height = event.xconfigure.height;
            drawFrameToWindow();
        }
        else if (event.type == MotionNotify)
        {
            g_pending_mouse_x = event.xmotion.x;
            g_pending_mouse_y = event.xmotion.y;
            g_mouse_move_pending = true;
        }
        else if (event.type == ButtonPress)
        {
            int local_x = event.xbutton.x;
            int local_y = event.xbutton.y;
            int button = event.xbutton.button;

            int remote_x = 0;
            int remote_y = 0;

            if (!convertLocalToRemote(local_x, local_y, remote_x, remote_y))
            {
                return;
            }

            sendMouseEvent(sock, MOUSE_ACTION_CLICK, button, remote_x, remote_y);
        }
        else if (event.type == KeyPress || event.type == KeyRelease)
        {
            KeySym keysym = XLookupKeysym(&event.xkey, 0);
            const char* key_name = XKeysymToString(keysym);

            if (key_name == nullptr)
            {
                std::cout << "unsupported key event" << std::endl;
                continue;
            }

            int key_status = event.type == KeyPress ? KEY_STATUS_DOWN : KEY_STATUS_UP;
            sendKeyEvent(sock, key_status, key_name);
        }
    }

    auto now = std::chrono::steady_clock::now();
    if (g_mouse_move_pending
        && now - g_last_mouse_send_time >= std::chrono::milliseconds(20))
    {
        int remote_x = 0;
        int remote_y = 0;

        if (convertLocalToRemote(
                g_pending_mouse_x,
                g_pending_mouse_y,
                remote_x,
                remote_y
            ))
        {
            sendMouseEvent(sock, MOUSE_ACTION_MOVE, 0, remote_x, remote_y);
            g_last_mouse_send_time = now;
            g_mouse_move_pending = false;
        }
    }
}

// ================================
// 函数功能：处理 Windows server 发来的 SCREEN_BEGIN 包
// 作用：读取当前屏幕帧的基本信息，并准备接收缓冲区
// ================================
void handleScreenBegin(const Packet& pkt)
{
    if (pkt.body_len != sizeof(ScreenFrameInfo))
    {
        std::cout << "invalid screen begin len=" << pkt.body_len << std::endl;
        return;
    }

    ScreenFrameInfo info = {};
    memcpy(&info, pkt.data, sizeof(ScreenFrameInfo));

    if (info.width <= 0 || info.height <= 0 || info.total_size <= 0)
    {
        std::cout << "invalid screen frame info" << std::endl;
        return;
    }

    if (info.format != SCREEN_FORMAT_BGRA32)
    {
        std::cout << "unsupported screen format=" << info.format << std::endl;
        return;
    }

    long long expected_size = 1LL * info.width * info.height * 4;

    if (expected_size != info.total_size)
    {
        std::cout << "invalid screen total size" << std::endl;
        return;
    }

    if (expected_size > 100 * 1024 * 1024)
    {
        std::cout << "screen frame too large" << std::endl;
        return;
    }

    g_frame_id = info.frame_id;
    g_frame_width = info.width;
    g_frame_height = info.height;
    g_frame_total_size = info.total_size;
    g_frame_received_size = 0;

    g_frame_buffer.clear();
    g_frame_buffer.resize(g_frame_total_size);

    g_receiving_frame = true;

    std::cout << "screen begin frame_id=" << g_frame_id
              << ", width=" << g_frame_width
              << ", height=" << g_frame_height
              << ", total_size=" << g_frame_total_size
              << std::endl;
}

// ================================
// 函数功能：处理 Windows server 发来的 SCREEN_CHUNK 包
// 作用：把当前图像分块复制到整帧缓冲区对应 offset 位置
// ================================
void handleScreenChunk(const Packet& pkt)
{
    if (!g_receiving_frame)
    {
        return;
    }

    int header_size = sizeof(ScreenChunkHeader);

    if (pkt.body_len < header_size)
    {
        std::cout << "invalid screen chunk len=" << pkt.body_len << std::endl;
        return;
    }

    ScreenChunkHeader header = {};
    memcpy(&header, pkt.data, header_size);

    if (header.frame_id != g_frame_id)
    {
        std::cout << "screen chunk frame_id mismatch" << std::endl;
        return;
    }

    if (header.data_len <= 0)
    {
        return;
    }

    long long end_pos = 1LL * header.offset + header.data_len;

    if (header.offset < 0 || end_pos > g_frame_total_size)
    {
        std::cout << "invalid screen chunk offset" << std::endl;
        return;
    }

    if (header_size + header.data_len > pkt.body_len)
    {
        std::cout << "invalid screen chunk payload" << std::endl;
        return;
    }

    memcpy(
        g_frame_buffer.data() + header.offset,
        pkt.data + header_size,
        header.data_len
    );

    g_frame_received_size += header.data_len;
}

// ================================
// 函数功能：处理 Windows server 发来的 SCREEN_END 包
// 作用：判断当前帧是否接收完整，完整后初始化窗口并绘制屏幕帧
// ================================
void handleScreenEnd(const Packet& pkt)
{
    if (pkt.body_len != sizeof(int))
    {
        std::cout << "invalid screen end len=" << pkt.body_len << std::endl;
        return;
    }

    int end_frame_id = -1;
    memcpy(&end_frame_id, pkt.data, sizeof(int));

    if (!g_receiving_frame)
    {
        return;
    }

    if (end_frame_id != g_frame_id)
    {
        std::cout << "screen end frame_id mismatch" << std::endl;
        return;
    }

    if (g_frame_received_size < g_frame_total_size)
    {
        std::cout << "screen frame incomplete: "
                  << g_frame_received_size << "/"
                  << g_frame_total_size << std::endl;

        g_receiving_frame = false;
        return;
    }

    g_receiving_frame = false;

    if (g_display == nullptr)
    {
        if (!initDisplayWindow(g_frame_width, g_frame_height))
        {
            std::cout << "init display window failed" << std::endl;
            return;
        }
    }

    drawFrameToWindow();
}

// ================================
// 函数功能：根据 Packet 的 cmd 字段分发不同命令
// 当前 Linux client 只接收 Windows server 发来的：
//   1. CMD_HELLO
//   2. CMD_SCREEN_BEGIN
//   3. CMD_SCREEN_CHUNK
//   4. CMD_SCREEN_END
// 不在这里写鼠标/键盘分发，因为键鼠事件是 Linux client 发给 Windows server 的
// ================================
void handlePacket(const Packet& pkt)
{
    if (pkt.magic != PACKET_MAGIC)
    {
        std::cout << "invalid magic" << std::endl;
        return;
    }

    switch (pkt.cmd)
    {
    case CMD_HELLO:
        std::cout << "recv hello from windows server: "
                  << std::string(pkt.data, pkt.body_len)
                  << std::endl;
        break;

    case CMD_SCREEN_BEGIN:
        handleScreenBegin(pkt);
        break;

    case CMD_SCREEN_CHUNK:
        handleScreenChunk(pkt);
        break;

    case CMD_SCREEN_END:
        handleScreenEnd(pkt);
        break;

    default:
        std::cout << "unknown cmd=" << pkt.cmd
                  << " len=" << pkt.body_len
                  << std::endl;
        break;
    }
}

// ================================
// 函数功能：循环接收 Windows server 发来的数据，并处理 TCP 粘包/半包
// 说明：TCP 是字节流，一次 recv 不一定刚好等于一个 Packet
// ================================
void recvLoop(int sock)
{
    const int HEADER_SIZE = 12;

    char buffer[262144] = {0};
    int offset = 0;

    while (true)
    {
        if (offset >= (int)sizeof(buffer))
        {
            std::cout << "recv buffer full, protocol error" << std::endl;
            break;
        }

        handleX11Events(sock);

        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(sock, &read_fds);

        int max_fd = sock;
        int x11_fd = -1;

        if (g_display != nullptr)
        {
            x11_fd = ConnectionNumber(g_display);
            FD_SET(x11_fd, &read_fds);
            max_fd = std::max(max_fd, x11_fd);
        }

        timeval timeout = {};
        timeout.tv_usec = 20000;

        int ready = select(max_fd + 1, &read_fds, nullptr, nullptr, &timeout);

        if (ready < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }

            std::cout << "select failed: " << strerror(errno) << std::endl;
            break;
        }

        if (x11_fd >= 0 && FD_ISSET(x11_fd, &read_fds))
        {
            handleX11Events(sock);
        }

        if (ready == 0 || !FD_ISSET(sock, &read_fds))
        {
            continue;
        }

        int len = recv(sock, buffer + offset, sizeof(buffer) - offset, 0);

        if (len == 0)
        {
            std::cout << "windows server closed connection" << std::endl;
            break;
        }

        if (len < 0)
        {
            std::cout << "recv failed" << std::endl;
            break;
        }

        offset += len;

        while (offset >= HEADER_SIZE)
        {
            int body_len = 0;
            memcpy(&body_len, buffer + 8, sizeof(int));

            if (body_len < 0 || body_len > PACKET_DATA_SIZE)
            {
                std::cout << "invalid body_len: " << body_len << std::endl;
                return;
            }

            int packet_size = HEADER_SIZE + body_len;

            if (offset < packet_size)
            {
                break;
            }

            Packet pkt = decodePacket(buffer);
            handlePacket(pkt);

            int remain = offset - packet_size;

            if (remain > 0)
            {
                memmove(buffer, buffer + packet_size, remain);
            }

            offset = remain;
        }

        handleX11Events(sock);
    }
}

// ================================
// 函数功能：初始化 Linux client 的 X11 显示窗口
// 作用：
//   1. 连接本机 X11 图形环境
//   2. 根据远程 Windows 屏幕比例创建合适大小的显示窗口
//   3. 创建 GC 绘图上下文，供后续 drawFrameToWindow 使用
// ================================
bool initDisplayWindow(int width, int height)
{
    if (width <= 0 || height <= 0)
    {
        std::cout << "invalid window size" << std::endl;
        return false;
    }

    g_display = XOpenDisplay(nullptr);

    if (g_display == nullptr)
    {
        std::cout << "XOpenDisplay failed" << std::endl;
        return false;
    }

    int screen = DefaultScreen(g_display);

    int local_screen_width = DisplayWidth(g_display, screen);
    int local_screen_height = DisplayHeight(g_display, screen);

    int max_window_width = local_screen_width * 9 / 10;
    int max_window_height = local_screen_height * 9 / 10;

    double scale_x = (double)max_window_width / width;
    double scale_y = (double)max_window_height / height;
    double scale = std::min(scale_x, scale_y);

    if (scale > 1.0)
    {
        scale = 1.0;
    }

    int window_width = (int)(width * scale);
    int window_height = (int)(height * scale);

    if (window_width <= 0 || window_height <= 0)
    {
        std::cout << "calculated window size invalid" << std::endl;
        XCloseDisplay(g_display);
        g_display = nullptr;
        return false;
    }

    g_window = XCreateSimpleWindow(
        g_display,
        RootWindow(g_display, screen),
        0,
        0,
        window_width,
        window_height,
        1,
        BlackPixel(g_display, screen),
        WhitePixel(g_display, screen)
    );

    if (g_window == 0)
    {
        std::cout << "XCreateSimpleWindow failed" << std::endl;
        XCloseDisplay(g_display);
        g_display = nullptr;
        return false;
    }

    XStoreName(g_display, g_window, "Linux Client - Windows Screen");

    XSelectInput(
        g_display,
        g_window,
        ExposureMask |
        StructureNotifyMask |
        KeyPressMask |
        KeyReleaseMask |
        ButtonPressMask |
        PointerMotionMask
    );

    XMapWindow(g_display, g_window);

    g_gc = XCreateGC(g_display, g_window, 0, nullptr);

    if (g_gc == 0)
    {
        std::cout << "XCreateGC failed" << std::endl;
        XDestroyWindow(g_display, g_window);
        XCloseDisplay(g_display);

        g_window = 0;
        g_display = nullptr;

        return false;
    }

    g_window_width = window_width;
    g_window_height = window_height;

    XFlush(g_display);

    std::cout << "display window init success: remote="
              << width << "x" << height
              << ", window=" << g_window_width << "x" << g_window_height
              << std::endl;

    return true;
}

// ================================
// 函数功能：将接收到的屏幕帧绘制到 Linux client 的 X11 窗口
// 说明：
//   g_frame_buffer 保存 Windows server 回传的 BGRA32 图像数据。
//   如果远程屏幕尺寸大于本地显示窗口，则先按比例缩放后再绘制。
// ================================
void drawFrameToWindow()
{
    if (g_display == nullptr || g_window == 0 || g_gc == 0)
    {
        return;
    }

    if (g_frame_buffer.empty())
    {
        return;
    }

    if (g_frame_width <= 0 || g_frame_height <= 0)
    {
        return;
    }

    if (g_window_width <= 0 || g_window_height <= 0)
    {
        return;
    }

    long long expected_size = 1LL * g_frame_width * g_frame_height * 4;

    if ((long long)g_frame_buffer.size() < expected_size)
    {
        std::cout << "frame buffer size invalid" << std::endl;
        return;
    }

    std::vector<unsigned char> scaled_frame(g_window_width * g_window_height * 4);

    for (int y = 0; y < g_window_height; y++)
    {
        int src_y = y * g_frame_height / g_window_height;

        for (int x = 0; x < g_window_width; x++)
        {
            int src_x = x * g_frame_width / g_window_width;

            int src_index = (src_y * g_frame_width + src_x) * 4;
            int dst_index = (y * g_window_width + x) * 4;

            scaled_frame[dst_index + 0] = g_frame_buffer[src_index + 0];
            scaled_frame[dst_index + 1] = g_frame_buffer[src_index + 1];
            scaled_frame[dst_index + 2] = g_frame_buffer[src_index + 2];
            scaled_frame[dst_index + 3] = 255;
        }
    }

    int screen = DefaultScreen(g_display);
    Visual* visual = DefaultVisual(g_display, screen);
    int depth = DefaultDepth(g_display, screen);

    XImage* image = XCreateImage(
        g_display,
        visual,
        depth,
        ZPixmap,
        0,
        (char*)scaled_frame.data(),
        g_window_width,
        g_window_height,
        32,
        0
    );

    if (image == nullptr)
    {
        std::cout << "XCreateImage failed" << std::endl;
        return;
    }

    XPutImage(
        g_display,
        g_window,
        g_gc,
        image,
        0,
        0,
        0,
        0,
        g_window_width,
        g_window_height
    );

    XFlush(g_display);

    image->data = nullptr;
    XDestroyImage(image);
}

// ================================
// 函数功能：关闭 Linux client 的 X11 显示窗口并释放资源
// ================================
void closeDisplayWindow()
{
    if (g_display == nullptr)
    {
        return;
    }

    if (g_gc != 0)
    {
        XFreeGC(g_display, g_gc);
        g_gc = 0;
    }

    if (g_window != 0)
    {
        XDestroyWindow(g_display, g_window);
        g_window = 0;
    }

    XCloseDisplay(g_display);
    g_display = nullptr;

    g_window_width = 0;
    g_window_height = 0;
}
