#include <iostream>
#include <unistd.h>
#include <arpa/inet.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <algorithm>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <thread>
#include <atomic>
#include <chrono>
#include <cerrno>
#include <X11/extensions/XShm.h>
#include <sys/ipc.h>
#include <sys/shm.h>

#include "../common/packet.h"

// ================================
// 函数声明
// ================================
void handleMouseMove(const char *data);
void handleKeyPress(const char *data);
void handlePacket(const Packet &pkt);
void handleMouseClick(const char *data);
void handleMouseEvent(const char *data, int len);
void handleKeyEvent(const char *data, int len);

bool sendAll(int sock, const char *buf, int len);
bool sendPacket(int sock, const Packet &pkt);

void sendRealScreenFrame(int client_fd, int frame_id);

int createServerSocket(int port);
void printLocalIPv4Addresses(int port);
int acceptClient(int server_fd);
void screenSendLoop(int client_fd);
void recvLoop(int client_fd);

std::atomic<bool> g_running(true);

const int SERVER_PORT = 9999;
const useconds_t SCREEN_CHUNK_DELAY_US = 10000;
const useconds_t SCREEN_FRAME_DELAY_US = 1000000;

int main()
{
    // ================================
    // 1. 创建监听 socket
    // ================================
    int server_fd = createServerSocket(SERVER_PORT);

    printLocalIPv4Addresses(SERVER_PORT);
    std::cout << "server waiting on 0.0.0.0:" << SERVER_PORT << " ..." << std::endl;

    // ================================
    // 2. 等待 Windows 客户端连接
    // ================================
    int client_fd = acceptClient(server_fd);

    std::cout << "client connected" << std::endl;

    // ================================
    // 3. 发送 Linux -> Windows 的测试包
    // 用于验证反向通信链路是否正常
    // ================================
    Packet hello = {};
    hello.magic = PACKET_MAGIC;
    hello.cmd = CMD_HELLO;

    const char *msg = "hello from linux server";
    hello.body_len = strlen(msg);
    memcpy(hello.data, msg, hello.body_len);

    sendPacket(client_fd, hello);

    // ================================
    // 4. 第一版屏幕回传：连续发送 5 帧真实截图
    // 当前版本是同步发送：
    // 发送屏幕期间暂时不会处理键鼠消息
    // 后续可以改成独立线程，让屏幕发送和键鼠接收并行
    // ================================

    std::thread screen_thread(screenSendLoop, client_fd);

    // ================================
    // 5. 循环接收 Windows 发来的键鼠控制命令
    // ================================
    recvLoop(client_fd);
    g_running = false;

    // ================================
    // 6. 关闭连接
    // ================================
    screen_thread.join();
    close(client_fd);
    close(server_fd);

    return 0;
}

// ================================
// 处理旧版鼠标移动命令
// data 格式："x,y"
// ================================
void handleMouseMove(const char *data)
{
    int x = 0;
    int y = 0;
    char cmd[128];

    sscanf(data, "%d,%d", &x, &y);

    snprintf(cmd, sizeof(cmd), "xdotool mousemove %d %d", x, y);

    system(cmd);

    std::cout << "mouse move x=" << x << " y=" << y << std::endl;
}

// ================================
// 处理旧版键盘命令
// data 是按键字符串，例如 a / Return / Escape
// ================================
void handleKeyPress(const char *data)
{
    char cmd[128];

    snprintf(cmd, sizeof(cmd), "xdotool key %s", data);

    system(cmd);

    std::cout << "收到键盘命令：" << data << std::endl;
}

// ================================
// 分发 Packet 命令
// 根据 cmd 字段调用不同的处理函数
// ================================
void handlePacket(const Packet &pkt)
{
    // 校验 magic，过滤非法数据包
    if (pkt.magic != PACKET_MAGIC)
    {
        std::cout << "非法数据包, magic错误: " << pkt.magic << std::endl;
        return;
    }

    if (pkt.cmd == CMD_HELLO)
    {
        std::cout << "收到测试命令：" << pkt.data << std::endl;
    }
    else if (pkt.cmd == CMD_MOUSE_MOVE)
    {
        handleMouseMove(pkt.data);
    }
    else if (pkt.cmd == CMD_KEY_PRESS)
    {
        handleKeyPress(pkt.data);
    }
    else if (pkt.cmd == CMD_MOUSE_CLICK)
    {
        handleMouseClick(pkt.data);
    }
    else if (pkt.cmd == CMD_MOUSE_EVENT)
    {
        handleMouseEvent(pkt.data, pkt.body_len);
    }
    else if (pkt.cmd == CMD_KEY_EVENT)
    {
        handleKeyEvent(pkt.data, pkt.body_len);
    }
    else
    {
        std::cout << "未知命令：" << pkt.cmd << std::endl;
    }
}

// ================================
// 创建监听 socket
// ================================
int createServerSocket(int port)
{
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);

    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;

    bind(server_fd, (sockaddr *)&addr, sizeof(addr));

    listen(server_fd, 5);

    return server_fd;
}

void printLocalIPv4Addresses(int port)
{
    char hostname[NI_MAXHOST] = {0};
    if (gethostname(hostname, sizeof(hostname)) != 0)
    {
        perror("gethostname");
        return;
    }

    addrinfo hints = {};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    addrinfo* result = nullptr;
    int ret = getaddrinfo(hostname, nullptr, &hints, &result);
    if (ret != 0)
    {
        std::cout << "getaddrinfo failed: " << gai_strerror(ret) << std::endl;
        return;
    }

    std::vector<std::string> addresses;
    for (addrinfo* item = result; item != nullptr; item = item->ai_next)
    {
        sockaddr_in* address = reinterpret_cast<sockaddr_in*>(item->ai_addr);
        if (address->sin_addr.s_addr == htonl(INADDR_LOOPBACK))
        {
            continue;
        }

        char ip[INET_ADDRSTRLEN] = {0};
        if (inet_ntop(AF_INET, &address->sin_addr, ip, sizeof(ip)) != nullptr &&
            std::find(addresses.begin(), addresses.end(), ip) == addresses.end())
        {
            addresses.push_back(ip);
        }
    }

    freeaddrinfo(result);

    if (addresses.empty())
    {
        std::cout << "no available non-loopback IPv4 address found" << std::endl;
        return;
    }

    std::cout << "available server IPv4 addresses:" << std::endl;
    for (const std::string& ip : addresses)
    {
        std::cout << "  " << ip << ":" << port << std::endl;
    }
}

// ================================
// 等待客户端连接
// ================================
int acceptClient(int server_fd)
{
    int client_fd = accept(server_fd, NULL, NULL);

    return client_fd;
}

// ================================
// 循环接收并拆包
// TCP 是字节流，可能出现粘包和半包
// 所以这里用 buffer + offset 保存未处理完的数据
// ================================
void recvLoop(int client_fd)
{
    char buffer[262144] = {0};
    int offset = 0;

    while (true)
    {
        if (offset >= (int)sizeof(buffer))
        {
            std::cout << "recv buffer full, protocol error" << std::endl;
            break;
        }

        int len = recv(client_fd, buffer + offset, sizeof(buffer) - offset, 0);

        if (len == 0)
        {
            std::cout << "client closed connection" << std::endl;
            break;
        }

        if (len < 0)
        {
            std::cout << "recv failed: errno=" << errno
                      << " message=" << strerror(errno) << std::endl;
            break;
        }

        offset += len;

        while (true)
        {
            // 一个 Packet 至少有 12 字节包头：
            // magic 4 字节 + cmd 4 字节 + body_len 4 字节
            if (offset < 12)
            {
                break;
            }

            int body_len = 0;

            memcpy(&body_len, buffer + 8, sizeof(int));

            // 当前协议中 Packet.data 最大是 256 字节
            if (body_len < 0 || body_len > PACKET_DATA_SIZE)
            {
                std::cout << "非法 body_len: " << body_len << std::endl;
                offset = 0;
                break;
            }

            // 数据还没收完整，继续等下一次 recv
            if (offset < 12 + body_len)
            {
                break;
            }

            Packet pkt = decodePacket(buffer);

            handlePacket(pkt);

            int pack_size = 12 + body_len;

            // 把已经处理过的数据移出 buffer
            memmove(buffer,
                    buffer + pack_size,
                    offset - pack_size);

            offset -= pack_size;
        }
    }
}

// ================================
// 处理旧版鼠标点击命令
// data 是按钮编号：1 左键，2 中键，3 右键
// ================================
void handleMouseClick(const char *data)
{
    int button;
    char cmd[256];

    sscanf(data, "%d", &button);

    if (button < 1 || button > 3)
    {
        std::cout << "非法鼠标按键:" << button << std::endl;
        return;
    }

    snprintf(cmd, sizeof(cmd), "xdotool click %d", button);

    system(cmd);

    std::cout << "mouse click button = " << button << std::endl;
}

// ================================
// 处理新版鼠标事件
// 支持移动、单击、双击、滚轮
// ================================
void handleMouseEvent(const char *data, int len)
{
    if (len != sizeof(MouseEvent))
    {
        std::cout << "非法 MouseEvent 长度:" << len << std::endl;
        return;
    }

    MouseEvent event;
    memcpy(&event, data, sizeof(MouseEvent));

    std::cout << event.action << " "
              << event.button << " "
              << event.x << " "
              << event.y << " "
              << std::endl;

    char cmd[256];

    if (event.action == MOUSE_ACTION_MOVE)
    {
        snprintf(cmd, sizeof(cmd), "xdotool mousemove %d %d", event.x, event.y);
        system(cmd);

        std::cout << "xdotool mousemove x=" << event.x
                  << " y=" << event.y << std::endl;
    }
    else if (event.action == MOUSE_ACTION_CLICK)
    {
        // button 1/2/3 是左右中键，4/5 是滚轮上下
        if (event.button != 1 &&
            event.button != 2 &&
            event.button != 3 &&
            event.button != 4 &&
            event.button != 5)
        {
            std::cout << "非法鼠标按键" << std::endl;
            return;
        }

        snprintf(cmd, sizeof(cmd), "xdotool mousemove %d %d", event.x, event.y);
        system(cmd);

        snprintf(cmd, sizeof(cmd), "xdotool click %d", event.button);
        system(cmd);

        std::cout << "mouse event click button=" << event.button << std::endl;
    }
    else if (event.action == MOUSE_ACTION_DOUBLE_CLICK)
    {
        if (event.button != 1 &&
            event.button != 2 &&
            event.button != 3)
        {
            std::cout << "非法双击鼠标按键" << std::endl;
            return;
        }

        snprintf(cmd, sizeof(cmd), "xdotool mousemove %d %d", event.x, event.y);
        system(cmd);

        snprintf(cmd, sizeof(cmd), "xdotool click %d", event.button);
        system(cmd);

        usleep(100000);

        system(cmd);

        std::cout << "mouse event double click button=" << event.button << std::endl;
    }
    else if (event.action == MOUSE_ACTION_DOWN)
    {
        if (event.button != 1 &&
            event.button != 2 &&
            event.button != 3)
        {
            std::cout << "unsupported mouse down button=" << event.button << std::endl;
            return;
        }

        snprintf(cmd, sizeof(cmd), "xdotool mousemove %d %d", event.x, event.y);
        system(cmd);

        snprintf(cmd, sizeof(cmd), "xdotool mousedown %d", event.button);
        system(cmd);

        std::cout << "mouse event down button=" << event.button << std::endl;
    }
    else if (event.action == MOUSE_ACTION_UP)
    {
        if (event.button != 1 &&
            event.button != 2 &&
            event.button != 3)
        {
            std::cout << "unsupported mouse up button=" << event.button << std::endl;
            return;
        }

        snprintf(cmd, sizeof(cmd), "xdotool mousemove %d %d", event.x, event.y);
        system(cmd);

        snprintf(cmd, sizeof(cmd), "xdotool mouseup %d", event.button);
        system(cmd);

        std::cout << "mouse event up button=" << event.button << std::endl;
    }
    else
    {
        std::cout << "unsupported MouseEvent action=" << event.action << std::endl;
    }
}

// ================================
// 处理新版键盘事件
// KEY_STATUS_DOWN：按下
// KEY_STATUS_UP：抬起
// ================================
void handleKeyEvent(const char *data, int len)
{
    if (len != sizeof(KeyEvent))
    {
        std::cout << "非法 KeyEvent 长度:" << len << std::endl;
        return;
    }

    KeyEvent event;
    memcpy(&event, data, sizeof(KeyEvent));

    // 防止字符串没有结尾
    event.key[31] = '\0';

    if (event.key[0] == '\0')
    {
        std::cout << "非法 KeyEvent: key为空" << std::endl;
        return;
    }

    char cmd[256];

    if (event.key_status == KEY_STATUS_DOWN)
    {
        snprintf(cmd, sizeof(cmd), "xdotool keydown %s", event.key);
        system(cmd);

        std::cout << "key down: " << event.key << std::endl;
    }
    else if (event.key_status == KEY_STATUS_UP)
    {
        snprintf(cmd, sizeof(cmd), "xdotool keyup %s", event.key);
        system(cmd);

        std::cout << "key up: " << event.key << std::endl;
    }
    else
    {
        std::cout << "非法 KeyEvent" << std::endl;
    }
}

// ================================
// 确保完整发送 len 字节
// send 可能一次只发送部分数据，所以需要循环发送
// ================================
bool sendAll(int sock, const char *buf, int len)
{
    int total = 0;

    while (total < len)
    {
        int n = send(sock, buf + total, len - total, MSG_NOSIGNAL);

        if (n < 0)
        {
            std::cout << "send failed: errno=" << errno
                      << " message=" << strerror(errno)
                      << " sent=" << total
                      << " packet_size=" << len
                      << std::endl;
            return false;
        }

        if (n == 0)
        {
            std::cout << "connection closed while sending"
                      << " sent=" << total
                      << " packet_size=" << len
                      << std::endl;
            return false;
        }

        total += n;
    }

    return true;
}

// ================================
// 按自定义协议编码并发送 Packet
// ================================
bool sendPacket(int sock, const Packet &pkt)
{
    int len = 0;

    char *buf = encodePacket(&pkt, &len);

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
// 捕获 Linux 当前屏幕，并通过自定义协议发送给 Windows 客户端
//
// 当前版本：
// 1. 使用 X11 的 XGetImage 抓取整张桌面截图
// 2. 将 XImage 像素转换为 Windows 端可显示的 BGRA32 格式
// 3. 使用 SCREEN_BEGIN / SCREEN_CHUNK / SCREEN_END 分包发送
// 4. 第一版不做压缩、不做差分、不做高帧率优化
// ================================
void sendRealScreenFrame(int client_fd, int frame_id)
{
    auto t0 = std::chrono::steady_clock::now();

    // ================================
    // XShm 相关对象只初始化一次
    // 后续每一帧复用同一块共享内存
    // ================================
    static Display* display = NULL;
    static Window root = 0;
    static int width = 0;
    static int height = 0;
    static XImage* image = NULL;
    static XShmSegmentInfo shminfo = {};
    static bool shm_inited = false;

    if (!shm_inited)
    {
        display = XOpenDisplay(NULL);

        if (display == NULL)
        {
            std::cout << "XOpenDisplay failed" << std::endl;
            return;
        }
        root = DefaultRootWindow(display);

        XWindowAttributes linuxwindow;

        if (XGetWindowAttributes(display, root, &linuxwindow) == 0)
        {
            std::cout << "XGetWindowAttributes failed" << std::endl;
            return;
        }

        width = linuxwindow.width;
        height = linuxwindow.height;

        int screen = DefaultScreen(display);
        Visual* visual = DefaultVisual(display, screen);
        int depth = DefaultDepth(display, screen);

        // 创建基于共享内存的 XImage
        image = XShmCreateImage(
            display,
            visual,
            depth,
            ZPixmap,
            NULL,
            &shminfo,
            width,
            height
        );

        if (image == NULL)
        {
            std::cout << "XShmCreateImage failed" << std::endl;
            return;
        }

        // 为 XImage 分配共享内存
        int shm_size = image->bytes_per_line * image->height;

        shminfo.shmid = shmget(
            IPC_PRIVATE,
            shm_size,
            IPC_CREAT | 0777
        );

        if (shminfo.shmid < 0)
        {
            std::cout << "shmget failed" << std::endl;
            XDestroyImage(image);
            image = NULL;
            return;
        }

        shminfo.shmaddr = (char*)shmat(shminfo.shmid, 0, 0);

        if (shminfo.shmaddr == (char*)-1)
        {
            std::cout << "shmat failed" << std::endl;
            shmctl(shminfo.shmid, IPC_RMID, 0);
            XDestroyImage(image);
            image = NULL;
            return;
        }

        image->data = shminfo.shmaddr;
        shminfo.readOnly = False;

        // 把共享内存挂到 X server
        if (!XShmAttach(display, &shminfo))
        {
            std::cout << "XShmAttach failed" << std::endl;
            shmdt(shminfo.shmaddr);
            shmctl(shminfo.shmid, IPC_RMID, 0);
            XDestroyImage(image);
            image = NULL;
            return;
        }

        // 标记这块共享内存可删除
        // 进程结束或 detach 后系统会自动回收，避免残留 shm 段
        shmctl(shminfo.shmid, IPC_RMID, 0);

        XSync(display, False);

        shm_inited = true;

        std::cout << "XShm init success width=" << width
                  << " height=" << height
                  << " bytes_per_line=" << image->bytes_per_line
                  << " bits_per_pixel=" << image->bits_per_pixel
                  << std::endl;
    }

    // ================================
    // 1. 使用 XShmGetImage 抓取当前桌面
    // 比 XGetImage 少一次普通跨进程拷贝
    // ================================
    if (!XShmGetImage(display, root, image, 0, 0, AllPlanes))
    {
        std::cout << "XShmGetImage failed" << std::endl;
        return;
    }

    auto t1 = std::chrono::steady_clock::now();

    // ================================
    // 2. 创建 BGRA32 缓冲区
    // Windows 端显示时使用 BGRA32，每个像素 4 字节
    // ================================
    int total_size = width * height * 4;
    std::vector<unsigned char> frame(total_size);

    // ================================
    // 3. 将 XImage 数据转换为 BGRA32
    // 当前环境 bits_per_pixel 应该是 32
    // 小端机器下一般是 B G R X 顺序
    // ================================
    if (image->bits_per_pixel == 32)
    {
        unsigned char* src = reinterpret_cast<unsigned char*>(image->data);

        for (int y = 0; y < height; y++)
        {
            unsigned char* src_row = src + y * image->bytes_per_line;

            for (int x = 0; x < width; x++)
            {
                unsigned char* src_pixel = src_row + x * 4;

                int index = (y * width + x) * 4;

                frame[index + 0] = src_pixel[0];
                frame[index + 1] = src_pixel[1];
                frame[index + 2] = src_pixel[2];
                frame[index + 3] = 255;
            }
        }
    }
    else
    {
        std::cout << "unsupported bits_per_pixel="
                  << image->bits_per_pixel << std::endl;
        return;
    }

    auto t2 = std::chrono::steady_clock::now();

    // ================================
    // 4. 构造屏幕帧头信息
    // ================================
    ScreenFrameInfo info = {};
    info.frame_id = frame_id;
    info.width = width;
    info.height = height;
    info.format = SCREEN_FORMAT_BGRA32;
    info.total_size = total_size;

    Packet begin_pkt = {};
    begin_pkt.magic = PACKET_MAGIC;
    begin_pkt.cmd = CMD_SCREEN_BEGIN;
    begin_pkt.body_len = sizeof(ScreenFrameInfo);

    memcpy(begin_pkt.data, &info, sizeof(info));

    if (!sendPacket(client_fd, begin_pkt))
    {
        std::cout << "send screen begin failed" << std::endl;
        return;
    }

    std::cout << "screen begin sent frame=" << frame_id
              << " size=" << total_size << std::endl;

    // ================================
    // 5. 分包发送图像数据
    // 每个包结构：
    // [ Packet Header ][ ScreenChunkHeader ][ 图像数据片段 ]
    // ================================
    int header_size = sizeof(ScreenChunkHeader);
    int max_payload = PACKET_DATA_SIZE - header_size;
    int offset = 0;
    int chunk_count = 0;

    while (offset < total_size)
    {
        int remain = total_size - offset;
        int data_len = std::min(remain, max_payload);

        ScreenChunkHeader header = {};
        header.frame_id = frame_id;
        header.offset = offset;
        header.data_len = data_len;

        Packet chunk_pkt = {};
        chunk_pkt.magic = PACKET_MAGIC;
        chunk_pkt.cmd = CMD_SCREEN_CHUNK;
        chunk_pkt.body_len = sizeof(ScreenChunkHeader) + data_len;

        memcpy(chunk_pkt.data, &header, sizeof(header));

        memcpy(
            chunk_pkt.data + sizeof(ScreenChunkHeader),
            frame.data() + offset,
            data_len
        );

        if (!sendPacket(client_fd, chunk_pkt))
        {
            std::cout << "send screen chunk failed" << std::endl;
            return;
        }

        offset += data_len;
        chunk_count++;

        if (chunk_count % 10 == 0 || offset == total_size)
        {
            std::cout << "screen chunk sent frame=" << frame_id
                      << " chunks=" << chunk_count
                      << " bytes=" << offset
                      << "/" << total_size
                      << std::endl;
        }

        usleep(SCREEN_CHUNK_DELAY_US);
    }

    // ================================
    // 6. 发送 SCREEN_END
    // 通知 Windows 当前帧已经发完
    // ================================
    Packet end_pkt = {};
    end_pkt.magic = PACKET_MAGIC;
    end_pkt.cmd = CMD_SCREEN_END;
    end_pkt.body_len = sizeof(int);

    memcpy(end_pkt.data, &frame_id, sizeof(frame_id));

    if (!sendPacket(client_fd, end_pkt))
    {
        std::cout << "send screen end failed" << std::endl;
        return;
    }

    std::cout << "screen end sent frame=" << frame_id
              << " chunks=" << chunk_count << std::endl;

    auto t3 = std::chrono::steady_clock::now();

    // ================================
    // 7. 每 10 帧打印一次性能统计
    // ================================
    static int debug_frame_count = 0;
    debug_frame_count++;

    if (debug_frame_count % 10 == 0)
    {
        auto capture_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
        auto convert_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1).count();
        auto send_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t3 - t2).count();
        auto total_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t3 - t0).count();

        std::cout << "[frame " << frame_id << "] "
                  << "capture=" << capture_ms << "ms, "
                  << "convert=" << convert_ms << "ms, "
                  << "send=" << send_ms << "ms, "
                  << "total=" << total_ms << "ms, "
                  << "chunks=" << chunk_count
                  << std::endl;
    }
}

void screenSendLoop(int client_fd){
    int frame_id = 1;
    while (g_running)
    {
        sendRealScreenFrame(client_fd, frame_id);
        frame_id++;
        usleep(SCREEN_FRAME_DELAY_US);
    }
}
