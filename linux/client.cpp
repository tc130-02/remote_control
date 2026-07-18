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
#include <thread>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <memory>
#include <cstdint>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

#define STB_IMAGE_IMPLEMENTATION
#include "../third_party/stb_image.h"

#include "../common/packet.h"

// ================================
// 全局变量：屏幕帧接收状态
// ================================
const long long MAX_PROTOCOL_FRAME_SIZE = INT_MAX;

std::vector<unsigned char> g_receive_frame_buffer;
int g_receive_frame_id = -1;
int g_receive_frame_width = 0;
int g_receive_frame_height = 0;
int g_receive_frame_total_size = 0;
int g_receive_frame_received_size = 0;
int g_receive_frame_format = SCREEN_FORMAT_BGRA32;
bool g_receiving_frame = false;

std::shared_ptr<const std::vector<unsigned char>> g_frame_buffer;
int g_frame_id = -1;
int g_frame_width = 0;
int g_frame_height = 0;
int g_frame_generation = 0;
int g_frame_receive_size = 0;
long long g_frame_jpeg_decode_ms = 0;
std::vector<unsigned char> g_scaled_frame_buffer;
int g_scaled_window_width = 0;
int g_scaled_window_height = 0;
int g_scaled_bytes_per_line = 0;
int g_display_x = 0;
int g_display_y = 0;
int g_display_width = 0;
int g_display_height = 0;
int g_ximage_bits_per_pixel = 0;
int g_ximage_bitmap_pad = 32;
int g_ximage_byte_order = LSBFirst;
unsigned long g_ximage_red_mask = 0;
unsigned long g_ximage_green_mask = 0;
unsigned long g_ximage_blue_mask = 0;

struct QueuedScreenFrame
{
    int frame_id = -1;
    int width = 0;
    int height = 0;
    int receive_size = 0;
    int format = SCREEN_FORMAT_BGRA32;
    int generation = 0;
    std::vector<unsigned char> data;
};

struct DecodedScreenFrame
{
    int frame_id = -1;
    int width = 0;
    int height = 0;
    int receive_size = 0;
    long long jpeg_decode_ms = 0;
    int generation = 0;
    std::vector<unsigned char> bgra;
};

std::mutex g_screen_frame_mutex;
std::condition_variable g_screen_frame_condition;
std::thread g_screen_decode_thread;
bool g_screen_decode_running = false;
bool g_pending_screen_frame_ready = false;
bool g_decoded_screen_frame_ready = false;
std::atomic<int> g_screen_generation(0);
QueuedScreenFrame g_pending_screen_frame;
DecodedScreenFrame g_decoded_screen_frame;

struct ScaleRequest
{
    int frame_id = -1;
    int width = 0;
    int height = 0;
    int window_width = 0;
    int window_height = 0;
    int receive_size = 0;
    long long jpeg_decode_ms = 0;
    int generation = 0;
    uint64_t serial = 0;
    std::shared_ptr<const std::vector<unsigned char>> bgra;
};

struct ScaledScreenFrame
{
    int frame_id = -1;
    int remote_width = 0;
    int remote_height = 0;
    int window_width = 0;
    int window_height = 0;
    int display_x = 0;
    int display_y = 0;
    int display_width = 0;
    int display_height = 0;
    int bytes_per_line = 0;
    int receive_size = 0;
    long long jpeg_decode_ms = 0;
    long long scale_ms = 0;
    int generation = 0;
    uint64_t serial = 0;
    std::vector<unsigned char> bgra;
};

std::mutex g_scale_mutex;
std::condition_variable g_scale_condition;
std::thread g_scale_thread;
bool g_scale_running = false;
bool g_scale_request_ready = false;
bool g_scaled_screen_frame_ready = false;
uint64_t g_scale_serial = 0;
ScaleRequest g_scale_request;
ScaledScreenFrame g_scaled_screen_frame;

// ================================
// 全局变量：X11 显示窗口状态
// ================================
Display* g_display = nullptr;
Window g_window = 0;
GC g_gc = 0;

int g_window_width = 0;
int g_window_height = 0;
int g_configured_remote_width = 0;
int g_configured_remote_height = 0;
Atom g_wm_delete_window = None;

enum class ClientState
{
    WAITING,
    CONNECTING,
    CONNECTED,
    FAILED
};

ClientState g_client_state = ClientState::WAITING;
std::atomic<bool> g_app_running(true);
std::atomic<bool> g_connecting(false);
std::thread g_connect_thread;
std::mutex g_connect_mutex;
int g_pending_connect_socket = -1;
int g_connect_result_socket = -1;
bool g_connect_result_ready = false;
std::string g_connect_result_error;

std::string g_host_input;
std::string g_port_input;
std::string g_connection_status = "waiting";
int g_active_input = 1;

const int CONNECTION_WINDOW_WIDTH = 640;
const int CONNECTION_WINDOW_HEIGHT = 300;
const int HOST_X = 145;
const int HOST_Y = 65;
const int HOST_WIDTH = 430;
const int HOST_HEIGHT = 34;
const int PORT_X = 145;
const int PORT_Y = 120;
const int PORT_WIDTH = 180;
const int PORT_HEIGHT = 34;
const int CONNECT_X = 365;
const int CONNECT_Y = 175;
const int CONNECT_WIDTH = 210;
const int CONNECT_HEIGHT = 40;

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
std::string getServerConfigPath();
void loadServerConfig(std::string& host, std::string& port);
bool saveServerConfig(const std::string& host, const std::string& port);
bool validatePort(const std::string& port);
void beginAsyncConnect();
void connectWorker(std::string host, std::string port);
bool pollConnectResult(int& connected_socket);
void cancelConnectThread();

Packet buildPacket(int cmd, const char* msg);
Packet buildRawPacket(int cmd, const char* buffer, int len);

void sendKeyEvent(int sock, int key_status, const char* key);
void sendKeyClick(int sock, const char* key);

void sendMouseEvent(int sock, int action, int button, int x, int y);
bool convertLocalToRemote(int local_x, int local_y, int& remote_x, int& remote_y);
void handleX11Events(int sock);
void handleConnectionEvent(const XEvent& event);

bool initConnectionWindow();
bool initDisplayWindow(int width, int height);
void drawConnectionInterface();
void drawFrameToWindow();
void displayLatestDecodedFrame();
void displayLatestScaledFrame();
void screenDecodeLoop();
void stopScreenDecodeThread();
void screenScaleLoop();
void stopScreenScaleThread();
void startScreenProcessingThreads();
void queueScaleRequest();
bool calculateDisplayRect(
    int remote_width,
    int remote_height,
    int window_width,
    int window_height,
    int& display_x,
    int& display_y,
    int& display_width,
    int& display_height
);
unsigned long scaleChannelToMask(
    unsigned char channel,
    unsigned long mask
);
void writeXImagePixel(
    unsigned char* destination,
    int bytes_per_pixel,
    unsigned long pixel
);
void discardReceivingFrame();
void resetRemoteState();
void closeDisplayWindow();

void handleScreenBegin(const Packet& pkt);
void handleScreenChunk(const Packet& pkt);
void handleScreenEnd(const Packet& pkt);

void handlePacket(const Packet& pkt);
void recvLoop(int sock);

int main(int argc, char* argv[])
{
    loadServerConfig(g_host_input, g_port_input);

    if (argc >= 3)
    {
        g_host_input = argv[1];
        g_port_input = argv[2];
    }

    if (!initConnectionWindow())
    {
        return 1;
    }

    int sock = -1;

    while (g_app_running)
    {
        while (g_app_running && g_client_state != ClientState::CONNECTED)
        {
            handleX11Events(-1);

            if (pollConnectResult(sock))
            {
                drawConnectionInterface();
                XFlush(g_display);
                break;
            }

            fd_set read_fds;
            FD_ZERO(&read_fds);
            int x11_fd = ConnectionNumber(g_display);
            FD_SET(x11_fd, &read_fds);

            timeval timeout = {};
            timeout.tv_usec = 20000;
            select(x11_fd + 1, &read_fds, nullptr, nullptr, &timeout);
        }

        if (!g_app_running)
        {
            break;
        }

        resetRemoteState();
        startScreenProcessingThreads();

        Packet hello = buildPacket(CMD_HELLO, "hello from linux client");
        if (!sendPacket(sock, hello))
        {
            stopScreenDecodeThread();
            stopScreenScaleThread();
            resetRemoteState();
            g_connection_status = "connect failed: hello send failed";
            g_client_state = ClientState::FAILED;
            close(sock);
            sock = -1;
            drawConnectionInterface();
            continue;
        }

        drawFrameToWindow();
        recvLoop(sock);
        stopScreenDecodeThread();
        stopScreenScaleThread();
        close(sock);
        sock = -1;

        if (g_app_running)
        {
            resetRemoteState();
            g_client_state = ClientState::WAITING;
            g_connection_status = "waiting - disconnected";
            XStoreName(g_display, g_window, "Linux Remote Control Client");
            int screen = DefaultScreen(g_display);
            XSetWindowBackground(
                g_display,
                g_window,
                WhitePixel(g_display, screen)
            );
            XSizeHints size_hints = {};
            size_hints.flags = 0;
            XSetWMNormalHints(g_display, g_window, &size_hints);
            XResizeWindow(
                g_display,
                g_window,
                CONNECTION_WINDOW_WIDTH,
                CONNECTION_WINDOW_HEIGHT
            );
            g_window_width = CONNECTION_WINDOW_WIDTH;
            g_window_height = CONNECTION_WINDOW_HEIGHT;
            drawConnectionInterface();
        }
    }

    if (sock >= 0)
    {
        shutdown(sock, SHUT_RDWR);
        close(sock);
    }

    cancelConnectThread();
    stopScreenDecodeThread();
    stopScreenScaleThread();
    closeDisplayWindow();
    return 0;
}

std::string getServerConfigPath()
{
    char executable_path[PATH_MAX] = {0};
    ssize_t path_length = readlink(
        "/proc/self/exe",
        executable_path,
        sizeof(executable_path) - 1
    );
    if (path_length < 0)
    {
        return "";
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
    return config_path + "server.conf";
}

void loadServerConfig(std::string& host, std::string& port)
{
    host.clear();
    port.clear();

    std::ifstream config(getServerConfigPath());
    if (!config.is_open())
    {
        return;
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

    std::string line;
    while (std::getline(config, line))
    {
        line = trim(line);
        size_t separator = line.find('=');
        if (separator == std::string::npos)
        {
            continue;
        }

        std::string key = trim(line.substr(0, separator));
        std::string value = trim(line.substr(separator + 1));
        if (key == "ip")
        {
            host = value;
        }
        else if (key == "port")
        {
            port = value;
        }
    }

    if (!validatePort(port))
    {
        port.clear();
    }
}

bool saveServerConfig(const std::string& host, const std::string& port)
{
    std::string config_path = getServerConfigPath();
    if (config_path.empty())
    {
        return false;
    }

    std::ofstream config(config_path, std::ios::trunc);
    if (!config.is_open())
    {
        return false;
    }

    config << "ip=" << host << "\n";
    config << "port=" << port << "\n";
    return config.good();
}

bool validatePort(const std::string& port)
{
    if (port.empty())
    {
        return false;
    }

    char* end = nullptr;
    long value = std::strtol(port.c_str(), &end, 10);
    return end != port.c_str()
        && *end == '\0'
        && value >= 1
        && value <= 65535;
}

void beginAsyncConnect()
{
    if (g_connecting || g_client_state == ClientState::CONNECTED)
    {
        return;
    }

    if (g_host_input.empty())
    {
        g_client_state = ClientState::FAILED;
        g_connection_status = "connect failed: host is empty";
        g_active_input = 1;
        drawConnectionInterface();
        return;
    }

    if (!validatePort(g_port_input))
    {
        g_client_state = ClientState::FAILED;
        g_connection_status = "connect failed: port must be 1-65535";
        g_active_input = 2;
        drawConnectionInterface();
        return;
    }

    if (g_connect_thread.joinable())
    {
        g_connect_thread.join();
    }

    {
        std::lock_guard<std::mutex> lock(g_connect_mutex);
        g_connect_result_ready = false;
        g_connect_result_socket = -1;
        g_connect_result_error.clear();
    }

    g_connecting = true;
    g_client_state = ClientState::CONNECTING;
    g_connection_status = "connecting";
    drawConnectionInterface();
    g_connect_thread = std::thread(
        connectWorker,
        g_host_input,
        g_port_input
    );
}

void connectWorker(std::string host, std::string port)
{
    addrinfo hints = {};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    addrinfo* address_list = nullptr;
    int resolve_result = getaddrinfo(
        host.c_str(),
        port.c_str(),
        &hints,
        &address_list
    );

    if (resolve_result != 0 || address_list == nullptr)
    {
        std::lock_guard<std::mutex> lock(g_connect_mutex);
        g_connect_result_error =
            std::string("connect failed: ") + gai_strerror(resolve_result);
        g_connect_result_ready = true;
        return;
    }

    int connected_socket = -1;
    std::string last_error = "connect failed";

    for (addrinfo* item = address_list;
         item != nullptr && g_app_running;
         item = item->ai_next)
    {
        int attempt_socket = socket(
            item->ai_family,
            item->ai_socktype,
            item->ai_protocol
        );
        if (attempt_socket < 0)
        {
            last_error = "connect failed errno="
                + std::to_string(errno) + ": " + strerror(errno);
            continue;
        }

        {
            std::lock_guard<std::mutex> lock(g_connect_mutex);
            if (!g_app_running)
            {
                close(attempt_socket);
                break;
            }
            g_pending_connect_socket = attempt_socket;
        }

        int result = connect(
            attempt_socket,
            item->ai_addr,
            item->ai_addrlen
        );
        int connect_errno = errno;

        bool still_owned = false;
        {
            std::lock_guard<std::mutex> lock(g_connect_mutex);
            if (g_pending_connect_socket == attempt_socket)
            {
                g_pending_connect_socket = -1;
                still_owned = true;
            }
        }

        if (result == 0 && still_owned && g_app_running)
        {
            connected_socket = attempt_socket;
            break;
        }

        last_error = "connect failed errno="
            + std::to_string(connect_errno) + ": "
            + strerror(connect_errno);
        if (still_owned)
        {
            close(attempt_socket);
        }
    }

    freeaddrinfo(address_list);

    if (connected_socket >= 0)
    {
        int no_delay = 1;
        setsockopt(
            connected_socket,
            IPPROTO_TCP,
            TCP_NODELAY,
            &no_delay,
            sizeof(no_delay)
        );
    }

    {
        std::lock_guard<std::mutex> lock(g_connect_mutex);
        g_connect_result_socket = connected_socket;
        g_connect_result_error =
            connected_socket >= 0 ? "" : last_error;
        g_connect_result_ready = true;
    }
}

bool pollConnectResult(int& connected_socket)
{
    int result_socket = -1;
    std::string error;

    {
        std::lock_guard<std::mutex> lock(g_connect_mutex);
        if (!g_connect_result_ready)
        {
            return false;
        }

        result_socket = g_connect_result_socket;
        error = g_connect_result_error;
        g_connect_result_socket = -1;
        g_connect_result_ready = false;
    }

    if (g_connect_thread.joinable())
    {
        g_connect_thread.join();
    }
    g_connecting = false;

    if (result_socket >= 0)
    {
        connected_socket = result_socket;
        g_client_state = ClientState::CONNECTED;
        g_connection_status = "connected";
        saveServerConfig(g_host_input, g_port_input);
        return true;
    }

    g_client_state = ClientState::FAILED;
    g_connection_status = error;
    drawConnectionInterface();
    return false;
}

void cancelConnectThread()
{
    {
        std::lock_guard<std::mutex> lock(g_connect_mutex);
        if (g_pending_connect_socket >= 0)
        {
            shutdown(g_pending_connect_socket, SHUT_RDWR);
            close(g_pending_connect_socket);
            g_pending_connect_socket = -1;
        }
    }

    if (g_connect_thread.joinable())
    {
        g_connect_thread.join();
    }

    {
        std::lock_guard<std::mutex> lock(g_connect_mutex);
        if (g_connect_result_socket >= 0)
        {
            close(g_connect_result_socket);
            g_connect_result_socket = -1;
        }
        g_connect_result_ready = false;
    }

    g_connecting = false;
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

    int display_x = 0;
    int display_y = 0;
    int display_width = 0;
    int display_height = 0;
    if (!calculateDisplayRect(
            g_frame_width,
            g_frame_height,
            g_window_width,
            g_window_height,
            display_x,
            display_y,
            display_width,
            display_height
        ))
    {
        return false;
    }

    if (local_x < display_x || local_y < display_y
        || local_x >= display_x + display_width
        || local_y >= display_y + display_height)
    {
        return false;
    }

    remote_x = static_cast<int>(
        1LL * (local_x - display_x) * g_frame_width / display_width
    );
    remote_y = static_cast<int>(
        1LL * (local_y - display_y) * g_frame_height / display_height
    );

    remote_x = std::max(0, std::min(g_frame_width - 1, remote_x));
    remote_y = std::max(0, std::min(g_frame_height - 1, remote_y));

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

    if (g_client_state == ClientState::CONNECTED)
    {
        displayLatestDecodedFrame();
        displayLatestScaledFrame();
    }

    while (XPending(g_display) > 0)
    {
        XEvent event = {};
        XNextEvent(g_display, &event);

        if (event.type == ClientMessage
            && (Atom)event.xclient.data.l[0] == g_wm_delete_window)
        {
            g_app_running = false;
            if (sock >= 0)
            {
                shutdown(sock, SHUT_RDWR);
            }
            continue;
        }

        if (g_client_state != ClientState::CONNECTED)
        {
            handleConnectionEvent(event);
            continue;
        }

        if (event.type == Expose)
        {
            drawFrameToWindow();
        }
        else if (event.type == ConfigureNotify)
        {
            g_window_width = event.xconfigure.width;
            g_window_height = event.xconfigure.height;
            queueScaleRequest();
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
                continue;
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
    if (sock >= 0
        && g_client_state == ClientState::CONNECTED
        && g_mouse_move_pending
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

void handleConnectionEvent(const XEvent& event)
{
    if (event.type == Expose)
    {
        drawConnectionInterface();
        return;
    }

    if (event.type == ConfigureNotify)
    {
        g_window_width = event.xconfigure.width;
        g_window_height = event.xconfigure.height;
        drawConnectionInterface();
        return;
    }

    if (g_client_state == ClientState::CONNECTING)
    {
        return;
    }

    if (event.type == ButtonPress)
    {
        int x = event.xbutton.x;
        int y = event.xbutton.y;

        if (x >= HOST_X && x <= HOST_X + HOST_WIDTH
            && y >= HOST_Y && y <= HOST_Y + HOST_HEIGHT)
        {
            g_active_input = 1;
        }
        else if (x >= PORT_X && x <= PORT_X + PORT_WIDTH
            && y >= PORT_Y && y <= PORT_Y + PORT_HEIGHT)
        {
            g_active_input = 2;
        }
        else if (x >= CONNECT_X && x <= CONNECT_X + CONNECT_WIDTH
            && y >= CONNECT_Y && y <= CONNECT_Y + CONNECT_HEIGHT)
        {
            beginAsyncConnect();
            return;
        }

        drawConnectionInterface();
        return;
    }

    if (event.type != KeyPress)
    {
        return;
    }

    char text[32] = {0};
    KeySym keysym = NoSymbol;
    int text_length = XLookupString(
        const_cast<XKeyEvent*>(&event.xkey),
        text,
        sizeof(text) - 1,
        &keysym,
        nullptr
    );

    if (keysym == XK_Return || keysym == XK_KP_Enter)
    {
        beginAsyncConnect();
        return;
    }

    if (keysym == XK_Tab)
    {
        g_active_input = g_active_input == 1 ? 2 : 1;
        drawConnectionInterface();
        return;
    }

    std::string* active_text =
        g_active_input == 1 ? &g_host_input : &g_port_input;

    if (keysym == XK_BackSpace)
    {
        if (!active_text->empty())
        {
            active_text->pop_back();
        }
        drawConnectionInterface();
        return;
    }

    for (int i = 0; i < text_length; ++i)
    {
        unsigned char ch = (unsigned char)text[i];
        if (ch < 32 || ch > 126)
        {
            continue;
        }

        if (g_active_input == 2 && (ch < '0' || ch > '9'))
        {
            continue;
        }

        size_t max_length = g_active_input == 1 ? 255 : 5;
        if (active_text->size() < max_length)
        {
            active_text->push_back((char)ch);
        }
    }

    drawConnectionInterface();
}

// ================================
// 函数功能：处理 Windows server 发来的 SCREEN_BEGIN 包
// 作用：读取当前屏幕帧的基本信息，并准备接收缓冲区
// ================================
void discardReceivingFrame()
{
    g_receive_frame_buffer.clear();
    g_receive_frame_id = -1;
    g_receive_frame_width = 0;
    g_receive_frame_height = 0;
    g_receive_frame_total_size = 0;
    g_receive_frame_received_size = 0;
    g_receive_frame_format = SCREEN_FORMAT_BGRA32;
    g_receiving_frame = false;
}

bool calculateDisplayRect(
    int remote_width,
    int remote_height,
    int window_width,
    int window_height,
    int& display_x,
    int& display_y,
    int& display_width,
    int& display_height
)
{
    if (remote_width <= 0 || remote_height <= 0
        || window_width <= 0 || window_height <= 0)
    {
        return false;
    }

    if (1LL * window_width * remote_height
        <= 1LL * window_height * remote_width)
    {
        display_width = window_width;
        display_height = std::max(
            1,
            static_cast<int>(
                1LL * window_width * remote_height / remote_width
            )
        );
    }
    else
    {
        display_height = window_height;
        display_width = std::max(
            1,
            static_cast<int>(
                1LL * window_height * remote_width / remote_height
            )
        );
    }

    display_x = (window_width - display_width) / 2;
    display_y = (window_height - display_height) / 2;
    return true;
}

unsigned long scaleChannelToMask(
    unsigned char channel,
    unsigned long mask
)
{
    if (mask == 0)
    {
        return 0;
    }

    int shift = 0;
    while (((mask >> shift) & 1UL) == 0)
    {
        ++shift;
    }
    unsigned long maximum = mask >> shift;
    unsigned long value = static_cast<unsigned long>(
        (static_cast<unsigned long long>(channel) * maximum + 127ULL)
        / 255ULL
    );
    return (value << shift) & mask;
}

void writeXImagePixel(
    unsigned char* destination,
    int bytes_per_pixel,
    unsigned long pixel
)
{
    if (g_ximage_byte_order == LSBFirst)
    {
        for (int byte = 0; byte < bytes_per_pixel; ++byte)
        {
            destination[byte] = static_cast<unsigned char>(
                (pixel >> (byte * 8)) & 0xffUL
            );
        }
    }
    else
    {
        for (int byte = 0; byte < bytes_per_pixel; ++byte)
        {
            int shift = (bytes_per_pixel - byte - 1) * 8;
            destination[byte] = static_cast<unsigned char>(
                (pixel >> shift) & 0xffUL
            );
        }
    }
}

void queueScaleRequest()
{
    if (!g_frame_buffer || g_frame_buffer->empty()
        || g_frame_width <= 0 || g_frame_height <= 0
        || g_window_width <= 0 || g_window_height <= 0)
    {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(g_scale_mutex);
        ScaleRequest request;
        request.frame_id = g_frame_id;
        request.width = g_frame_width;
        request.height = g_frame_height;
        request.window_width = g_window_width;
        request.window_height = g_window_height;
        request.receive_size = g_frame_receive_size;
        request.jpeg_decode_ms = g_frame_jpeg_decode_ms;
        request.generation = g_frame_generation;
        request.serial = ++g_scale_serial;
        request.bgra = g_frame_buffer;
        g_scale_request = std::move(request);
        g_scale_request_ready = true;
    }
    g_scale_condition.notify_one();
}

void startScreenProcessingThreads()
{
    {
        std::lock_guard<std::mutex> lock(g_screen_frame_mutex);
        g_screen_decode_running = true;
        g_pending_screen_frame_ready = false;
    }
    if (!g_screen_decode_thread.joinable())
    {
        g_screen_decode_thread = std::thread(screenDecodeLoop);
    }

    {
        std::lock_guard<std::mutex> lock(g_scale_mutex);
        g_scale_running = true;
        g_scale_request_ready = false;
    }
    if (!g_scale_thread.joinable())
    {
        g_scale_thread = std::thread(screenScaleLoop);
    }
}

void screenScaleLoop()
{
    while (true)
    {
        ScaleRequest request;
        {
            std::unique_lock<std::mutex> lock(g_scale_mutex);
            g_scale_condition.wait(
                lock,
                []()
                {
                    return !g_scale_running || g_scale_request_ready;
                }
            );

            if (!g_scale_running && !g_scale_request_ready)
            {
                return;
            }

            request = std::move(g_scale_request);
            g_scale_request_ready = false;
        }

        ScaledScreenFrame scaled;
        scaled.frame_id = request.frame_id;
        scaled.remote_width = request.width;
        scaled.remote_height = request.height;
        scaled.window_width = request.window_width;
        scaled.window_height = request.window_height;
        scaled.receive_size = request.receive_size;
        scaled.jpeg_decode_ms = request.jpeg_decode_ms;
        scaled.generation = request.generation;
        scaled.serial = request.serial;

        if (!calculateDisplayRect(
                request.width,
                request.height,
                request.window_width,
                request.window_height,
                scaled.display_x,
                scaled.display_y,
                scaled.display_width,
                scaled.display_height
            ))
        {
            continue;
        }

        auto scale_start = std::chrono::steady_clock::now();
        int bytes_per_pixel = (g_ximage_bits_per_pixel + 7) / 8;
        int pad_bytes = g_ximage_bitmap_pad / 8;
        if (bytes_per_pixel <= 0
            || bytes_per_pixel > static_cast<int>(sizeof(unsigned long))
            || pad_bytes <= 0
            || g_ximage_red_mask == 0
            || g_ximage_green_mask == 0
            || g_ximage_blue_mask == 0)
        {
            continue;
        }

        long long row_bits =
            1LL * scaled.display_width * g_ximage_bits_per_pixel;
        scaled.bytes_per_line = static_cast<int>(
            ((row_bits + g_ximage_bitmap_pad - 1)
                / g_ximage_bitmap_pad)
            * pad_bytes
        );
        long long scaled_size =
            1LL * scaled.bytes_per_line * scaled.display_height;
        if (scaled_size <= 0 || scaled_size > MAX_PROTOCOL_FRAME_SIZE)
        {
            continue;
        }
        scaled.bgra.resize(static_cast<size_t>(scaled_size));

        const std::vector<unsigned char>& source = *request.bgra;
        if (static_cast<long long>(source.size())
            != 1LL * request.width * request.height * 4)
        {
            continue;
        }
        for (int y = 0; y < scaled.display_height; ++y)
        {
            uint64_t source_y_fixed =
                scaled.display_height <= 1
                    ? 0
                    : static_cast<uint64_t>(y)
                        * static_cast<uint64_t>(request.height - 1)
                        * 65536ULL
                        / static_cast<uint64_t>(
                            scaled.display_height - 1
                        );
            int y0 = static_cast<int>(source_y_fixed >> 16);
            int y1 = std::min(request.height - 1, y0 + 1);
            uint32_t fy = static_cast<uint32_t>(
                source_y_fixed & 0xffffU
            );
            uint32_t inverse_y = 65536U - fy;

            for (int x = 0; x < scaled.display_width; ++x)
            {
                uint64_t source_x_fixed =
                    scaled.display_width <= 1
                        ? 0
                        : static_cast<uint64_t>(x)
                            * static_cast<uint64_t>(request.width - 1)
                            * 65536ULL
                            / static_cast<uint64_t>(
                                scaled.display_width - 1
                            );
                int x0 = static_cast<int>(source_x_fixed >> 16);
                int x1 = std::min(request.width - 1, x0 + 1);
                uint32_t fx = static_cast<uint32_t>(
                    source_x_fixed & 0xffffU
                );
                uint32_t inverse_x = 65536U - fx;

                size_t top_left =
                    (static_cast<size_t>(y0) * request.width + x0) * 4;
                size_t top_right =
                    (static_cast<size_t>(y0) * request.width + x1) * 4;
                size_t bottom_left =
                    (static_cast<size_t>(y1) * request.width + x0) * 4;
                size_t bottom_right =
                    (static_cast<size_t>(y1) * request.width + x1) * 4;
                unsigned char channels[3] = {};
                for (int channel = 0; channel < 3; ++channel)
                {
                    uint64_t top =
                        static_cast<uint64_t>(source[top_left + channel])
                            * inverse_x
                        + static_cast<uint64_t>(
                            source[top_right + channel]
                        ) * fx;
                    uint64_t bottom =
                        static_cast<uint64_t>(
                            source[bottom_left + channel]
                        ) * inverse_x
                        + static_cast<uint64_t>(
                            source[bottom_right + channel]
                        ) * fx;
                    uint64_t value =
                        top * inverse_y + bottom * fy;
                    channels[channel] = static_cast<unsigned char>(
                        (value + (1ULL << 31)) >> 32
                    );
                }

                unsigned long pixel =
                    scaleChannelToMask(
                        channels[2],
                        g_ximage_red_mask
                    )
                    | scaleChannelToMask(
                        channels[1],
                        g_ximage_green_mask
                    )
                    | scaleChannelToMask(
                        channels[0],
                        g_ximage_blue_mask
                    );
                unsigned char* destination =
                    scaled.bgra.data()
                    + static_cast<size_t>(y) * scaled.bytes_per_line
                    + static_cast<size_t>(x) * bytes_per_pixel;
                writeXImagePixel(
                    destination,
                    bytes_per_pixel,
                    pixel
                );
            }
        }
        auto scale_end = std::chrono::steady_clock::now();
        scaled.scale_ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                scale_end - scale_start
            ).count();

        {
            std::lock_guard<std::mutex> lock(g_scale_mutex);
            if (!g_scale_running
                || scaled.serial != g_scale_serial
                || scaled.generation != g_screen_generation)
            {
                continue;
            }
            g_scaled_screen_frame = std::move(scaled);
            g_scaled_screen_frame_ready = true;
        }
    }
}

void stopScreenScaleThread()
{
    {
        std::lock_guard<std::mutex> lock(g_scale_mutex);
        g_scale_running = false;
        g_scale_request_ready = false;
    }
    g_scale_condition.notify_all();

    if (g_scale_thread.joinable())
    {
        g_scale_thread.join();
    }
}

void screenDecodeLoop()
{
    while (true)
    {
        QueuedScreenFrame encoded;

        {
            std::unique_lock<std::mutex> lock(g_screen_frame_mutex);
            g_screen_frame_condition.wait(
                lock,
                []()
                {
                    return !g_screen_decode_running
                        || g_pending_screen_frame_ready;
                }
            );

            if (!g_screen_decode_running && !g_pending_screen_frame_ready)
            {
                return;
            }

            encoded = std::move(g_pending_screen_frame);
            g_pending_screen_frame_ready = false;
        }

        DecodedScreenFrame decoded;
        decoded.frame_id = encoded.frame_id;
        decoded.width = encoded.width;
        decoded.height = encoded.height;
        decoded.receive_size = encoded.receive_size;
        decoded.generation = encoded.generation;

        if (encoded.format == SCREEN_FORMAT_JPEG)
        {
            auto decode_start = std::chrono::steady_clock::now();
            int decoded_width = 0;
            int decoded_height = 0;
            int source_channels = 0;
            unsigned char* rgba = stbi_load_from_memory(
                encoded.data.data(),
                (int)encoded.data.size(),
                &decoded_width,
                &decoded_height,
                &source_channels,
                4
            );
            auto decode_end = std::chrono::steady_clock::now();
            decoded.jpeg_decode_ms =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    decode_end - decode_start
                ).count();

            if (rgba == nullptr)
            {
                const char* reason = stbi_failure_reason();
                std::cout << "JPEG decode failed for frame_id="
                          << encoded.frame_id << ": "
                          << (reason == nullptr ? "unknown" : reason)
                          << std::endl;
                continue;
            }

            if (decoded_width != encoded.width
                || decoded_height != encoded.height)
            {
                std::cout << "JPEG dimensions mismatch for frame_id="
                          << encoded.frame_id << std::endl;
                stbi_image_free(rgba);
                continue;
            }

            long long decoded_size =
                1LL * decoded_width * decoded_height * 4;
            if (decoded_size <= 0
                || decoded_size > MAX_PROTOCOL_FRAME_SIZE)
            {
                std::cout << "decoded screen frame too large" << std::endl;
                stbi_image_free(rgba);
                continue;
            }

            decoded.bgra.resize((size_t)decoded_size);
            for (long long pixel = 0;
                 pixel < 1LL * decoded_width * decoded_height;
                 ++pixel)
            {
                long long index = pixel * 4;
                decoded.bgra[index + 0] = rgba[index + 2];
                decoded.bgra[index + 1] = rgba[index + 1];
                decoded.bgra[index + 2] = rgba[index + 0];
                decoded.bgra[index + 3] = 255;
            }
            stbi_image_free(rgba);
        }
        else if (encoded.format == SCREEN_FORMAT_BGRA32)
        {
            long long expected_size =
                1LL * encoded.width * encoded.height * 4;
            if ((long long)encoded.data.size() != expected_size)
            {
                std::cout << "invalid BGRA32 frame size" << std::endl;
                continue;
            }
            decoded.bgra = std::move(encoded.data);
        }
        else
        {
            continue;
        }

        {
            std::lock_guard<std::mutex> lock(g_screen_frame_mutex);
            if (decoded.generation != g_screen_generation)
            {
                continue;
            }

            if (g_pending_screen_frame_ready
                && g_pending_screen_frame.generation == decoded.generation
                && g_pending_screen_frame.frame_id > decoded.frame_id)
            {
                continue;
            }

            g_decoded_screen_frame = std::move(decoded);
            g_decoded_screen_frame_ready = true;
        }
    }
}

void stopScreenDecodeThread()
{
    {
        std::lock_guard<std::mutex> lock(g_screen_frame_mutex);
        g_screen_decode_running = false;
        g_pending_screen_frame_ready = false;
    }
    g_screen_frame_condition.notify_all();

    if (g_screen_decode_thread.joinable())
    {
        g_screen_decode_thread.join();
    }
}

void displayLatestDecodedFrame()
{
    DecodedScreenFrame decoded;

    {
        std::lock_guard<std::mutex> lock(g_screen_frame_mutex);
        if (!g_decoded_screen_frame_ready
            || g_decoded_screen_frame.generation != g_screen_generation)
        {
            return;
        }

        if (g_pending_screen_frame_ready
            && g_pending_screen_frame.generation
                == g_decoded_screen_frame.generation
            && g_pending_screen_frame.frame_id
                > g_decoded_screen_frame.frame_id)
        {
            g_decoded_screen_frame_ready = false;
            return;
        }

        decoded = std::move(g_decoded_screen_frame);
        g_decoded_screen_frame_ready = false;
    }

    g_frame_id = decoded.frame_id;
    g_frame_width = decoded.width;
    g_frame_height = decoded.height;
    g_frame_generation = decoded.generation;
    g_frame_receive_size = decoded.receive_size;
    g_frame_jpeg_decode_ms = decoded.jpeg_decode_ms;
    g_frame_buffer =
        std::make_shared<const std::vector<unsigned char>>(
            std::move(decoded.bgra)
        );

    if (g_configured_remote_width != g_frame_width
        || g_configured_remote_height != g_frame_height)
    {
        if (!initDisplayWindow(g_frame_width, g_frame_height))
        {
            std::cout << "resize display window failed" << std::endl;
            return;
        }
    }

    queueScaleRequest();
}

void displayLatestScaledFrame()
{
    ScaledScreenFrame scaled;
    {
        std::lock_guard<std::mutex> lock(g_scale_mutex);
        if (!g_scaled_screen_frame_ready)
        {
            return;
        }
        if (g_scale_request_ready
            && g_scale_request.serial > g_scaled_screen_frame.serial)
        {
            g_scaled_screen_frame_ready = false;
            return;
        }
        scaled = std::move(g_scaled_screen_frame);
        g_scaled_screen_frame_ready = false;
    }

    if (scaled.generation != g_screen_generation
        || scaled.window_width != g_window_width
        || scaled.window_height != g_window_height)
    {
        return;
    }

    g_scaled_frame_buffer = std::move(scaled.bgra);
    g_scaled_window_width = scaled.window_width;
    g_scaled_window_height = scaled.window_height;
    g_scaled_bytes_per_line = scaled.bytes_per_line;
    g_display_x = scaled.display_x;
    g_display_y = scaled.display_y;
    g_display_width = scaled.display_width;
    g_display_height = scaled.display_height;

    auto display_start = std::chrono::steady_clock::now();
    drawFrameToWindow();
    auto display_end = std::chrono::steady_clock::now();
    static int displayed_frame_count = 0;
    displayed_frame_count++;
    if (displayed_frame_count % 10 == 0)
    {
        long long display_ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                display_end - display_start
            ).count();
        std::cout << "[screen] frame_id=" << scaled.frame_id
                  << " receive_size=" << scaled.receive_size
                  << " jpeg_decode_ms=" << scaled.jpeg_decode_ms
                  << " scale_ms=" << scaled.scale_ms
                  << " display_ms=" << display_ms
                  << " width=" << scaled.remote_width
                  << " height=" << scaled.remote_height
                  << std::endl;
    }
}

void handleScreenBegin(const Packet& pkt)
{
    // A new frame supersedes any older frame that did not reach SCREEN_END.
    discardReceivingFrame();

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

    if (info.format != SCREEN_FORMAT_BGRA32
        && info.format != SCREEN_FORMAT_JPEG)
    {
        std::cout << "unsupported screen format=" << info.format << std::endl;
        return;
    }

    long long raw_size = 1LL * info.width * info.height * 4;
    if (raw_size <= 0 || raw_size > MAX_PROTOCOL_FRAME_SIZE)
    {
        std::cout << "screen frame too large" << std::endl;
        return;
    }

    if (info.total_size > MAX_PROTOCOL_FRAME_SIZE)
    {
        std::cout << "encoded screen frame too large" << std::endl;
        return;
    }

    if (info.format == SCREEN_FORMAT_BGRA32
        && raw_size != info.total_size)
    {
        std::cout << "invalid BGRA32 total size" << std::endl;
        return;
    }

    g_receive_frame_id = info.frame_id;
    g_receive_frame_width = info.width;
    g_receive_frame_height = info.height;
    g_receive_frame_total_size = info.total_size;
    g_receive_frame_received_size = 0;
    g_receive_frame_format = info.format;
    g_receive_frame_buffer.resize(g_receive_frame_total_size);

    g_receiving_frame = true;
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
        discardReceivingFrame();
        return;
    }

    ScreenChunkHeader header = {};
    memcpy(&header, pkt.data, header_size);

    if (header.frame_id != g_receive_frame_id)
    {
        std::cout << "screen chunk frame_id mismatch" << std::endl;
        discardReceivingFrame();
        return;
    }

    if (header.data_len <= 0)
    {
        discardReceivingFrame();
        return;
    }

    long long end_pos = 1LL * header.offset + header.data_len;

    if (header.offset != g_receive_frame_received_size
        || end_pos > g_receive_frame_total_size)
    {
        std::cout << "invalid screen chunk offset" << std::endl;
        discardReceivingFrame();
        return;
    }

    if (header_size + header.data_len != pkt.body_len)
    {
        std::cout << "invalid screen chunk payload" << std::endl;
        discardReceivingFrame();
        return;
    }

    memcpy(
        g_receive_frame_buffer.data() + header.offset,
        pkt.data + header_size,
        header.data_len
    );

    g_receive_frame_received_size += header.data_len;
}

// ================================
// 函数功能：处理 Windows server 发来的 SCREEN_END 包
// 作用：判断当前帧是否接收完整，完整后初始化窗口并绘制屏幕帧
// ================================
void handleScreenEnd(const Packet& pkt)
{
    if (pkt.body_len != sizeof(int32_t))
    {
        std::cout << "invalid screen end len=" << pkt.body_len << std::endl;
        discardReceivingFrame();
        return;
    }

    int32_t end_frame_id = -1;
    memcpy(&end_frame_id, pkt.data, sizeof(end_frame_id));

    if (!g_receiving_frame)
    {
        return;
    }

    if (end_frame_id != g_receive_frame_id)
    {
        std::cout << "screen end frame_id mismatch" << std::endl;
        discardReceivingFrame();
        return;
    }

    if (g_receive_frame_received_size != g_receive_frame_total_size)
    {
        std::cout << "screen frame incomplete: "
                  << g_receive_frame_received_size << "/"
                  << g_receive_frame_total_size << std::endl;
        discardReceivingFrame();
        return;
    }

    QueuedScreenFrame completed;
    completed.frame_id = g_receive_frame_id;
    completed.width = g_receive_frame_width;
    completed.height = g_receive_frame_height;
    completed.receive_size = g_receive_frame_total_size;
    completed.format = g_receive_frame_format;
    completed.data = std::move(g_receive_frame_buffer);

    {
        std::lock_guard<std::mutex> lock(g_screen_frame_mutex);
        completed.generation = g_screen_generation;
        // This is intentionally a single-slot queue. A newer complete screen
        // frame replaces an older one that the decoder has not started yet.
        g_pending_screen_frame = std::move(completed);
        g_pending_screen_frame_ready = true;
    }
    g_screen_frame_condition.notify_one();

    discardReceivingFrame();
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

    while (g_app_running && g_client_state == ClientState::CONNECTED)
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

bool initConnectionWindow()
{
    g_display = XOpenDisplay(nullptr);
    if (g_display == nullptr)
    {
        std::cout << "XOpenDisplay failed" << std::endl;
        return false;
    }

    int screen = DefaultScreen(g_display);
    Visual* visual = DefaultVisual(g_display, screen);
    int depth = DefaultDepth(g_display, screen);
    XImage* format_probe = XCreateImage(
        g_display,
        visual,
        depth,
        ZPixmap,
        0,
        nullptr,
        1,
        1,
        32,
        0
    );
    if (format_probe == nullptr)
    {
        std::cout << "XCreateImage format probe failed" << std::endl;
        XCloseDisplay(g_display);
        g_display = nullptr;
        return false;
    }
    g_ximage_bits_per_pixel = format_probe->bits_per_pixel;
    g_ximage_bitmap_pad = format_probe->bitmap_pad;
    g_ximage_byte_order = format_probe->byte_order;
    g_ximage_red_mask = format_probe->red_mask;
    g_ximage_green_mask = format_probe->green_mask;
    g_ximage_blue_mask = format_probe->blue_mask;
    format_probe->data = nullptr;
    XDestroyImage(format_probe);

    g_window = XCreateSimpleWindow(
        g_display,
        RootWindow(g_display, screen),
        0,
        0,
        CONNECTION_WINDOW_WIDTH,
        CONNECTION_WINDOW_HEIGHT,
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

    XStoreName(g_display, g_window, "Linux Remote Control Client");

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

    g_wm_delete_window = XInternAtom(
        g_display,
        "WM_DELETE_WINDOW",
        False
    );
    XSetWMProtocols(
        g_display,
        g_window,
        &g_wm_delete_window,
        1
    );

    XMapWindow(g_display, g_window);
    g_window_width = CONNECTION_WINDOW_WIDTH;
    g_window_height = CONNECTION_WINDOW_HEIGHT;
    XFlush(g_display);
    return true;
}

bool initDisplayWindow(int width, int height)
{
    if (g_display == nullptr || g_window == 0)
    {
        return false;
    }

    if (width <= 0 || height <= 0)
    {
        return false;
    }

    XSizeHints size_hints = {};
    size_hints.flags = 0;
    XSetWMNormalHints(g_display, g_window, &size_hints);

    int screen = DefaultScreen(g_display);
    int available_width = std::max(
        1,
        DisplayWidth(g_display, screen) * 4 / 5
    );
    int available_height = std::max(
        1,
        DisplayHeight(g_display, screen) * 4 / 5
    );
    double initial_scale = std::min(
        1.0,
        std::min(
            static_cast<double>(available_width) / width,
            static_cast<double>(available_height) / height
        )
    );
    int initial_width = std::max(
        1,
        static_cast<int>(width * initial_scale)
    );
    int initial_height = std::max(
        1,
        static_cast<int>(height * initial_scale)
    );

    XStoreName(g_display, g_window, "Linux Client - Remote Screen");
    XSetWindowBackground(
        g_display,
        g_window,
        BlackPixel(g_display, screen)
    );
    XResizeWindow(g_display, g_window, initial_width, initial_height);
    g_window_width = initial_width;
    g_window_height = initial_height;
    g_configured_remote_width = width;
    g_configured_remote_height = height;
    g_scaled_frame_buffer.clear();
    g_scaled_window_width = 0;
    g_scaled_window_height = 0;
    g_scaled_bytes_per_line = 0;
    g_display_x = 0;
    g_display_y = 0;
    g_display_width = 0;
    g_display_height = 0;

    XFlush(g_display);
    return true;
}

void drawConnectionInterface()
{
    if (g_display == nullptr || g_window == 0 || g_gc == 0)
    {
        return;
    }

    XClearWindow(g_display, g_window);
    int screen = DefaultScreen(g_display);
    unsigned long black = BlackPixel(g_display, screen);
    unsigned long white = WhitePixel(g_display, screen);

    XSetForeground(g_display, g_gc, black);
    const char* title = "Remote Control Connection";
    XDrawString(g_display, g_window, g_gc, 35, 35, title, strlen(title));

    const char* host_label = "Host:";
    const char* port_label = "Port:";
    XDrawString(
        g_display, g_window, g_gc,
        55, HOST_Y + 23,
        host_label, strlen(host_label)
    );
    XDrawString(
        g_display, g_window, g_gc,
        55, PORT_Y + 23,
        port_label, strlen(port_label)
    );

    XDrawRectangle(
        g_display, g_window, g_gc,
        HOST_X, HOST_Y, HOST_WIDTH, HOST_HEIGHT
    );
    XDrawRectangle(
        g_display, g_window, g_gc,
        PORT_X, PORT_Y, PORT_WIDTH, PORT_HEIGHT
    );

    if (g_active_input == 1)
    {
        XDrawRectangle(
            g_display, g_window, g_gc,
            HOST_X - 2, HOST_Y - 2,
            HOST_WIDTH + 4, HOST_HEIGHT + 4
        );
    }
    else
    {
        XDrawRectangle(
            g_display, g_window, g_gc,
            PORT_X - 2, PORT_Y - 2,
            PORT_WIDTH + 4, PORT_HEIGHT + 4
        );
    }

    std::string visible_host = g_host_input;
    if (visible_host.size() > 55)
    {
        visible_host = visible_host.substr(visible_host.size() - 55);
    }

    XDrawString(
        g_display, g_window, g_gc,
        HOST_X + 8, HOST_Y + 23,
        visible_host.c_str(), visible_host.size()
    );
    XDrawString(
        g_display, g_window, g_gc,
        PORT_X + 8, PORT_Y + 23,
        g_port_input.c_str(), g_port_input.size()
    );

    if (g_client_state == ClientState::CONNECTING)
    {
        XSetForeground(g_display, g_gc, white);
        XFillRectangle(
            g_display, g_window, g_gc,
            CONNECT_X, CONNECT_Y, CONNECT_WIDTH, CONNECT_HEIGHT
        );
        XSetForeground(g_display, g_gc, black);
    }

    XDrawRectangle(
        g_display, g_window, g_gc,
        CONNECT_X, CONNECT_Y, CONNECT_WIDTH, CONNECT_HEIGHT
    );

    const char* button_text =
        g_client_state == ClientState::CONNECTING
            ? "Connecting..."
            : "Connect";
    XDrawString(
        g_display, g_window, g_gc,
        CONNECT_X + 62, CONNECT_Y + 25,
        button_text, strlen(button_text)
    );

    XDrawString(
        g_display, g_window, g_gc,
        55, 255,
        g_connection_status.c_str(),
        g_connection_status.size()
    );
    XFlush(g_display);
}

// ================================
// 函数功能：将接收到的屏幕帧绘制到 Linux client 的 X11 窗口
// 说明：
//   后台线程按当前窗口尺寸生成等比例 XImage 原生像素缓冲区。
//   X11 线程只清理黑色背景并绘制最近准备好的显示帧。
// ================================
void drawFrameToWindow()
{
    if (g_display == nullptr || g_window == 0 || g_gc == 0)
    {
        return;
    }

    if (g_window_width <= 0 || g_window_height <= 0)
    {
        return;
    }

    int screen = DefaultScreen(g_display);
    XSetForeground(
        g_display,
        g_gc,
        BlackPixel(g_display, screen)
    );
    XFillRectangle(
        g_display,
        g_window,
        g_gc,
        0,
        0,
        static_cast<unsigned int>(g_window_width),
        static_cast<unsigned int>(g_window_height)
    );

    if (g_scaled_frame_buffer.empty()
        || g_display_width <= 0 || g_display_height <= 0
        || g_scaled_window_width != g_window_width
        || g_scaled_window_height != g_window_height)
    {
        XFlush(g_display);
        return;
    }

    long long expected_size =
        1LL * g_scaled_bytes_per_line * g_display_height;
    if (static_cast<long long>(g_scaled_frame_buffer.size())
            != expected_size
        || g_scaled_bytes_per_line <= 0)
    {
        std::cout << "frame buffer size invalid" << std::endl;
        return;
    }

    Visual* visual = DefaultVisual(g_display, screen);
    int depth = DefaultDepth(g_display, screen);

    XImage* image = XCreateImage(
        g_display,
        visual,
        depth,
        ZPixmap,
        0,
        (char*)g_scaled_frame_buffer.data(),
        g_display_width,
        g_display_height,
        g_ximage_bitmap_pad,
        g_scaled_bytes_per_line
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
        g_display_x,
        g_display_y,
        g_display_width,
        g_display_height
    );

    XFlush(g_display);

    image->data = nullptr;
    XDestroyImage(image);
}

void resetRemoteState()
{
    discardReceivingFrame();

    {
        std::lock_guard<std::mutex> lock(g_screen_frame_mutex);
        g_screen_generation++;
        g_pending_screen_frame = QueuedScreenFrame();
        g_decoded_screen_frame = DecodedScreenFrame();
        g_pending_screen_frame_ready = false;
        g_decoded_screen_frame_ready = false;
    }

    {
        std::lock_guard<std::mutex> lock(g_scale_mutex);
        ++g_scale_serial;
        g_scale_request = ScaleRequest();
        g_scaled_screen_frame = ScaledScreenFrame();
        g_scale_request_ready = false;
        g_scaled_screen_frame_ready = false;
    }

    g_frame_buffer.reset();
    g_frame_id = -1;
    g_frame_width = 0;
    g_frame_height = 0;
    g_frame_generation = g_screen_generation;
    g_frame_receive_size = 0;
    g_frame_jpeg_decode_ms = 0;
    g_scaled_frame_buffer.clear();
    g_scaled_window_width = 0;
    g_scaled_window_height = 0;
    g_scaled_bytes_per_line = 0;
    g_display_x = 0;
    g_display_y = 0;
    g_display_width = 0;
    g_display_height = 0;
    g_configured_remote_width = 0;
    g_configured_remote_height = 0;
    g_mouse_move_pending = false;
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
