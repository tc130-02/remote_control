#include <iostream>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <vector>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include "../common/packet.h"

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
Display* g_display = nullptr;
Window g_window = 0;
GC g_gc = 0;
int g_window_width = 0;
int g_window_height = 0;

// ================================
// 函数声明
// ================================
bool sendAll(int sock, const char* buf, int len);
bool sendPacket(int sock, const Packet& pkt);

Packet buildPacket(int cmd, const char* msg);
Packet buildRawPacket(int cmd, const char* buffer, int len);

void sendKeyEvent(int sock, int key_status, const char* key);
void sendKeyClick(int sock, const char* key);

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
    // ================================
    sockaddr_in server_addr = {};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(8081);
    server_addr.sin_addr.s_addr = inet_addr("[REMOVED_PRIVATE_IP]");

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
    // 5. 发送一次键盘测试事件
    // 当前只是验证 Linux client -> Windows server 的键盘事件链路
    // ================================
    std::cout << "please click Notepad or input box in Windows..." << std::endl;
    sleep(1);
    sendKeyClick(sock, "A");

    // ================================
    // 6. 持续接收 Windows server 发来的 hello 和屏幕帧
    // ================================
    recvLoop(sock);

    // ================================
    // 7. 关闭 socket
    // ================================
    close(sock);

    return 0;
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
        int n = send(sock, buf + total, len - total, 0);

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
// 作用：判断当前帧是否接收完整
// 当前版本：只验证接收完成，不保存文件、不显示窗口
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

    std::cout << "screen frame received, frame_id="
              << g_frame_id
              << ", width=" << g_frame_width
              << ", height=" << g_frame_height
              << ", size=" << g_frame_received_size
              << std::endl;
    if (g_display == nullptr){
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
    }
}

// ================================
// 函数功能：初始化 Linux client 的 X11 显示窗口
// 作用：
//   1. 连接本机 X11 图形环境
//   2. 根据远程 Windows 屏幕宽高创建显示窗口
//   3. 创建 GC 绘图上下文，供后续 drawFrameToWindow 使用
// 返回值：
//   true  初始化成功
//   false 初始化失败
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

    g_window = XCreateSimpleWindow(
        g_display,
        RootWindow(g_display, screen),
        0,
        0,
        width,
        height,
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
        ExposureMask | KeyPressMask | ButtonPressMask | PointerMotionMask
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

    g_window_width = width;
    g_window_height = height;

    XFlush(g_display);

    return true;
}

// ================================
// 函数功能：将接收到的屏幕帧绘制到 Linux client 的 X11 窗口
// 说明：
//   g_frame_buffer 保存 Windows server 回传的 BGRA32 图像数据。
//   本函数将当前完整帧包装成 XImage，并通过 XPutImage 绘制到 g_window。
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

    long long expected_size = 1LL * g_frame_width * g_frame_height * 4;

    if ((long long)g_frame_buffer.size() < expected_size)
    {
        std::cout << "frame buffer size invalid" << std::endl;
        return;
    }

    int screen = DefaultScreen(g_display);
    Visual* visual = DefaultVisual(g_display, screen);
    int depth = DefaultDepth(g_display, screen);

    std::vector<unsigned char> local_frame = g_frame_buffer;

    XImage* image = XCreateImage(
        g_display,
        visual,
        depth,
        ZPixmap,
        0,
        (char*)local_frame.data(),
        g_frame_width,
        g_frame_height,
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
        g_frame_width,
        g_frame_height
    );

    XFlush(g_display);

    image->data = nullptr;
    XDestroyImage(image);
}

