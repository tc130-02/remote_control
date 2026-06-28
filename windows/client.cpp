#define _WINSOCK_DEPRECATED_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <winsock2.h>
#include <windows.h>
#include <iostream>
#include <cstring>
#include <string>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>

#pragma comment(lib, "ws2_32.lib")

#include "../common/packet.h"

SOCKET g_server_socket = INVALID_SOCKET;
SOCKADDR_IN g_server_addr = {};
HWND g_hwnd = NULL;

int g_remote_width = 1918;
int g_remote_height = 918;

int g_last_remote_x = -1;
int g_last_remote_y = -1;
DWORD g_last_mouse_send_time = 0;

bool g_use_mouse_down_up = false;
bool g_use_new_key_event = true;

std::atomic<bool> g_running(false);
std::thread g_recv_thread;

std::mutex g_screen_mutex;
std::vector<unsigned char> g_screen_bgra;
int g_screen_width = 0;
int g_screen_height = 0;

std::vector<unsigned char> g_frame_buffer;
int g_frame_id = -1;
int g_frame_width = 0;
int g_frame_height = 0;
int g_frame_total_size = 0;
int g_frame_received_size = 0;
int g_frame_format = 0;
bool g_receiving_frame = false;

bool InitSocket();
bool ConnectServer();
int InitWindow(HINSTANCE hInstance, int nCmdShow);

bool convertToRemotePoint(HWND hwnd, int xPos, int yPos, int& remote_x, int& remote_y);

Packet buildPacket(int cmd, const char* msg);
Packet buildRawPacket(int cmd, const char* buffer, int len);

bool sendAll(SOCKET sock, const char* buf, int len);
bool sendPacket(SOCKET sock, const Packet& pkt);

void sendHello(SOCKET sock, const char* msg);
void sendMouseEvent(SOCKET sock, int action, int button, int x, int y);
void sendMouseEventFromWindow(HWND hwnd, LPARAM lParam, int action, int button);
void sendKeyEvent(SOCKET sock, int key_status, const char* key);

void sendKeyPressOld(SOCKET sock, const char* key);

void recvThreadProc();
void handleIncomingPacket(const Packet& pkt);
void handleScreenBegin(const Packet& pkt);
void handleScreenChunk(const Packet& pkt);
void handleScreenEnd(const Packet& pkt);

std::string vkToXdotoolKey(WPARAM wParam);

void drawRemoteScreen(HDC hdc, RECT client_rect);

LRESULT CALLBACK winProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        RECT client_rect;
        GetClientRect(hwnd, &client_rect);

        drawRemoteScreen(hdc, client_rect);

        EndPaint(hwnd, &ps);
    }
    break;

    case WM_MOUSEMOVE:
    {
        if (g_server_socket == INVALID_SOCKET) {
            break;
        }

        int xPos = LOWORD(lParam);
        int yPos = HIWORD(lParam);

        int remote_x = 0;
        int remote_y = 0;

        if (!convertToRemotePoint(hwnd, xPos, yPos, remote_x, remote_y)) {
            break;
        }

        DWORD now = GetTickCount();

        if (now - g_last_mouse_send_time < 20) {
            break;
        }

        if (remote_x == g_last_remote_x && remote_y == g_last_remote_y) {
            break;
        }

        sendMouseEvent(g_server_socket, MOUSE_ACTION_MOVE, 0, remote_x, remote_y);

        g_last_remote_x = remote_x;
        g_last_remote_y = remote_y;
        g_last_mouse_send_time = now;
    }
    break;

    case WM_LBUTTONDOWN:
    {
        SetFocus(hwnd);

        if (g_use_mouse_down_up) {
            sendMouseEventFromWindow(hwnd, lParam, MOUSE_ACTION_DOWN, 1);
        }
        else {
            sendMouseEventFromWindow(hwnd, lParam, MOUSE_ACTION_CLICK, 1);
        }
    }
    break;

    case WM_LBUTTONUP:
    {
        if (g_use_mouse_down_up) {
            sendMouseEventFromWindow(hwnd, lParam, MOUSE_ACTION_UP, 1);
        }
    }
    break;

    case WM_RBUTTONDOWN:
    {
        SetFocus(hwnd);

        if (g_use_mouse_down_up) {
            sendMouseEventFromWindow(hwnd, lParam, MOUSE_ACTION_DOWN, 3);
        }
        else {
            sendMouseEventFromWindow(hwnd, lParam, MOUSE_ACTION_CLICK, 3);
        }
    }
    break;

    case WM_RBUTTONUP:
    {
        if (g_use_mouse_down_up) {
            sendMouseEventFromWindow(hwnd, lParam, MOUSE_ACTION_UP, 3);
        }
    }
    break;

    case WM_MBUTTONDOWN:
    {
        SetFocus(hwnd);

        if (g_use_mouse_down_up) {
            sendMouseEventFromWindow(hwnd, lParam, MOUSE_ACTION_DOWN, 2);
        }
        else {
            sendMouseEventFromWindow(hwnd, lParam, MOUSE_ACTION_CLICK, 2);
        }
    }
    break;

    case WM_MBUTTONUP:
    {
        if (g_use_mouse_down_up) {
            sendMouseEventFromWindow(hwnd, lParam, MOUSE_ACTION_UP, 2);
        }
    }
    break;

    case WM_LBUTTONDBLCLK:
    {
        SetFocus(hwnd);
        sendMouseEventFromWindow(hwnd, lParam, MOUSE_ACTION_DOUBLE_CLICK, 1);
    }
    break;

    case WM_MOUSEWHEEL:
    {
        if (g_server_socket == INVALID_SOCKET) {
            break;
        }

        int delta = GET_WHEEL_DELTA_WPARAM(wParam);
        int button = delta > 0 ? 4 : 5;

        POINT pt;
        pt.x = LOWORD(lParam);
        pt.y = HIWORD(lParam);
        ScreenToClient(hwnd, &pt);

        LPARAM fake_lParam = MAKELPARAM(pt.x, pt.y);
        sendMouseEventFromWindow(hwnd, fake_lParam, MOUSE_ACTION_CLICK, button);
    }
    break;

    case WM_KEYDOWN:
    {
        if (g_server_socket == INVALID_SOCKET) {
            break;
        }

        std::string key = vkToXdotoolKey(wParam);

        if (key.empty()) {
            break;
        }

        if (g_use_new_key_event) {
            if (lParam & (1 << 30)) {
                break;
            }

            sendKeyEvent(g_server_socket, KEY_STATUS_DOWN, key.c_str());
        }
        else {
            sendKeyPressOld(g_server_socket, key.c_str());
        }
    }
    break;

    case WM_KEYUP:
    {
        if (g_server_socket == INVALID_SOCKET) {
            break;
        }

        if (!g_use_new_key_event) {
            break;
        }

        std::string key = vkToXdotoolKey(wParam);

        if (key.empty()) {
            break;
        }

        sendKeyEvent(g_server_socket, KEY_STATUS_UP, key.c_str());
    }
    break;

    case WM_DESTROY:
    {
        g_running = false;

        if (g_server_socket != INVALID_SOCKET) {
            shutdown(g_server_socket, SD_BOTH);
            closesocket(g_server_socket);
            g_server_socket = INVALID_SOCKET;
        }

        if (g_recv_thread.joinable()) {
            g_recv_thread.join();
        }

        WSACleanup();
        PostQuitMessage(0);
    }
    break;

    default:
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }

    return 0;
}

int WINAPI WinMain(
    HINSTANCE hInstance,
    HINSTANCE hPrevInstance,
    PSTR pCmdLine,
    int nCmdShow
)
{
    AllocConsole();
    freopen("CONOUT$", "w", stdout);
    freopen("CONOUT$", "w", stderr);

    if (!InitSocket()) {
        MessageBoxA(NULL, "InitSocket failed", "error", MB_OK | MB_ICONERROR);
        return 0;
    }

    if (!ConnectServer()) {
        MessageBoxA(NULL, "Connect Linux server failed", "error", MB_OK | MB_ICONERROR);
        WSACleanup();
        return 0;
    }

    if (!InitWindow(hInstance, nCmdShow)) {
        WSACleanup();
        return 0;
    }

    sendHello(g_server_socket, "hello linux window client");

    g_running = true;
    g_recv_thread = std::thread(recvThreadProc);

    MSG msg = { 0 };

    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return 0;
}

int InitWindow(HINSTANCE hInstance, int nCmdShow)
{
    WNDCLASSA wc = {};
    LPCSTR CLASS_NAME = "RemoteControlWindow";

    wc.lpfnWndProc = winProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;

    if (!RegisterClassA(&wc)) {
        MessageBoxA(NULL, "Window class register failed", "error", MB_OK | MB_ICONERROR);
        return 0;
    }

    g_hwnd = CreateWindowA(
        CLASS_NAME,
        "Windows to Linux Remote Control",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        1000,
        700,
        NULL,
        NULL,
        hInstance,
        NULL
    );

    if (g_hwnd == NULL) {
        MessageBoxA(NULL, "CreateWindow failed", "error", MB_OK | MB_ICONERROR);
        return 0;
    }

    ShowWindow(g_hwnd, nCmdShow);
    UpdateWindow(g_hwnd);

    return 1;
}

bool InitSocket()
{
    WSADATA wsaData;
    int ret = WSAStartup(MAKEWORD(2, 2), &wsaData);

    if (ret != 0) {
        std::cout << "WSAStartup failed: " << ret << std::endl;
        return false;
    }

    g_server_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    if (g_server_socket == INVALID_SOCKET) {
        std::cout << "socket failed: " << WSAGetLastError() << std::endl;
        WSACleanup();
        return false;
    }

    g_server_addr.sin_family = AF_INET;
    g_server_addr.sin_port = htons(8080);
    g_server_addr.sin_addr.s_addr = inet_addr("[REMOVED_PRIVATE_IP]");

    return true;
}

bool ConnectServer()
{
    int ret = connect(
        g_server_socket,
        (sockaddr*)&g_server_addr,
        sizeof(g_server_addr)
    );

    if (ret == SOCKET_ERROR) {
        std::cout << "connect failed: " << WSAGetLastError() << std::endl;
        closesocket(g_server_socket);
        g_server_socket = INVALID_SOCKET;
        return false;
    }

    std::cout << "connect success!" << std::endl;
    return true;
}

bool convertToRemotePoint(HWND hwnd, int xPos, int yPos, int& remote_x, int& remote_y)
{
    RECT client_rect;
    GetClientRect(hwnd, &client_rect);

    int client_width = client_rect.right - client_rect.left;
    int client_height = client_rect.bottom - client_rect.top;

    if (client_width <= 0 || client_height <= 0) {
        return false;
    }

    int target_width = g_remote_width;
    int target_height = g_remote_height;

    {
        std::lock_guard<std::mutex> lock(g_screen_mutex);
        if (g_screen_width > 0 && g_screen_height > 0) {
            target_width = g_screen_width;
            target_height = g_screen_height;
        }
    }

    remote_x = xPos * target_width / client_width;
    remote_y = yPos * target_height / client_height;

    return true;
}

Packet buildPacket(int cmd, const char* msg)
{
    Packet pkt;
    memset(&pkt, 0, sizeof(pkt));

    pkt.magic = PACKET_MAGIC;
    pkt.cmd = cmd;

    if (msg == nullptr) {
        pkt.body_len = 0;
        return pkt;
    }

    int len = (int)strlen(msg);

    if (len >= PACKET_DATA_SIZE) {
        len = PACKET_DATA_SIZE - 1;
    }

    pkt.body_len = len;
    memcpy(pkt.data, msg, len);
    pkt.data[len] = '\0';

    return pkt;
}

Packet buildRawPacket(int cmd, const char* buffer, int len)
{
    Packet pkt;
    memset(&pkt, 0, sizeof(pkt));

    pkt.magic = PACKET_MAGIC;
    pkt.cmd = cmd;

    if (buffer == nullptr || len <= 0) {
        pkt.body_len = 0;
        return pkt;
    }

    if (len > PACKET_DATA_SIZE) {
        std::cout << "raw packet too large" << std::endl;
        pkt.body_len = 0;
        return pkt;
    }

    pkt.body_len = len;
    memcpy(pkt.data, buffer, len);

    return pkt;
}

bool sendAll(SOCKET sock, const char* buf, int len)
{
    int total = 0;

    while (total < len) {
        int n = send(sock, buf + total, len - total, 0);

        if (n == SOCKET_ERROR) {
            std::cout << "send failed: " << WSAGetLastError() << std::endl;
            return false;
        }

        if (n == 0) {
            std::cout << "connection closed while sending" << std::endl;
            return false;
        }

        total += n;
    }

    return true;
}

bool sendPacket(SOCKET sock, const Packet& pkt)
{
    int len = 0;

    char* buf = encodePacket(&pkt, &len);

    if (buf == NULL || len <= 0) {
        return false;
    }

    bool ok = sendAll(sock, buf, len);

    free(buf);

    return ok;
}

void sendHello(SOCKET sock, const char* msg)
{
    Packet pkt = buildPacket(CMD_HELLO, msg);
    sendPacket(sock, pkt);

    std::cout << "send hello: " << msg << std::endl;
}

void sendMouseEvent(SOCKET sock, int action, int button, int x, int y)
{
    MouseEvent event;
    memset(&event, 0, sizeof(event));

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

    std::cout << "send mouse event action=" << action
        << " button=" << button
        << " x=" << x
        << " y=" << y
        << std::endl;
}

void sendMouseEventFromWindow(HWND hwnd, LPARAM lParam, int action, int button)
{
    if (g_server_socket == INVALID_SOCKET) {
        return;
    }

    int xPos = LOWORD(lParam);
    int yPos = HIWORD(lParam);

    int remote_x = 0;
    int remote_y = 0;

    if (!convertToRemotePoint(hwnd, xPos, yPos, remote_x, remote_y)) {
        return;
    }

    sendMouseEvent(g_server_socket, action, button, remote_x, remote_y);
}

void sendKeyEvent(SOCKET sock, int key_status, const char* key)
{
    if (key == nullptr) {
        return;
    }

    KeyEvent event;
    memset(&event, 0, sizeof(event));

    event.key_status = key_status;

    strncpy(event.key, key, sizeof(event.key) - 1);
    event.key[sizeof(event.key) - 1] = '\0';

    Packet pkt = buildRawPacket(
        CMD_KEY_EVENT,
        (const char*)&event,
        sizeof(KeyEvent)
    );

    sendPacket(sock, pkt);

    std::cout << "send key event status=" << key_status
        << " key=" << event.key
        << std::endl;
}

void sendKeyPressOld(SOCKET sock, const char* key)
{
    Packet pkt = buildPacket(CMD_KEY_PRESS, key);
    sendPacket(sock, pkt);

    std::cout << "send old key press: " << key << std::endl;
}

void recvThreadProc()
{
    char buffer[262144] = { 0 };
    int offset = 0;

    while (g_running) {
        int len = recv(g_server_socket, buffer + offset, sizeof(buffer) - offset, 0);

        if (len <= 0) {
            std::cout << "recv thread stopped" << std::endl;
            break;
        }

        offset += len;

        while (true) {
            if (offset < 12) {
                break;
            }

            int body_len = 0;
            memcpy(&body_len, buffer + 8, sizeof(int));

            if (body_len < 0 || body_len > PACKET_DATA_SIZE) {
                std::cout << "invalid body_len from server: " << body_len << std::endl;
                offset = 0;
                break;
            }

            if (offset < 12 + body_len) {
                break;
            }

            Packet pkt = decodePacket(buffer);
            handleIncomingPacket(pkt);

            int pack_size = 12 + body_len;

            memmove(buffer, buffer + pack_size, offset - pack_size);
            offset -= pack_size;
        }

        if (offset >= (int)sizeof(buffer)) {
            offset = 0;
        }
    }

    g_running = false;
}

void handleIncomingPacket(const Packet& pkt)
{
    if (pkt.magic != PACKET_MAGIC) {
        std::cout << "invalid packet magic from server" << std::endl;
        return;
    }

    if (pkt.cmd == CMD_SCREEN_BEGIN) {
        handleScreenBegin(pkt);
    }
    else if (pkt.cmd == CMD_SCREEN_CHUNK) {
        handleScreenChunk(pkt);
    }
    else if (pkt.cmd == CMD_SCREEN_END) {
        handleScreenEnd(pkt);
    }
    else if (pkt.cmd == CMD_HELLO) {
        std::cout << "server hello: " << pkt.data << std::endl;
    }
    else {
        std::cout << "recv packet cmd=" << pkt.cmd << " len=" << pkt.body_len << std::endl;
    }
}

void handleScreenBegin(const Packet& pkt)
{
    if (pkt.body_len != sizeof(ScreenFrameInfo)) {
        std::cout << "invalid screen begin len=" << pkt.body_len << std::endl;
        return;
    }

    ScreenFrameInfo info;
    memcpy(&info, pkt.data, sizeof(info));

    if (info.width <= 0 || info.height <= 0 || info.total_size <= 0) {
        std::cout << "invalid screen frame info" << std::endl;
        return;
    }

    if (info.format != SCREEN_FORMAT_BGRA32) {
        std::cout << "unsupported screen format=" << info.format << std::endl;
        return;
    }

    if (info.total_size != info.width * info.height * 4) {
        std::cout << "invalid screen total size" << std::endl;
        return;
    }

    if (info.total_size > 100 * 1024 * 1024) {
        std::cout << "screen frame too large" << std::endl;
        return;
    }

    g_frame_id = info.frame_id;
    g_frame_width = info.width;
    g_frame_height = info.height;
    g_frame_total_size = info.total_size;
    g_frame_received_size = 0;
    g_frame_format = info.format;
    g_receiving_frame = true;

    g_frame_buffer.clear();
    g_frame_buffer.resize(g_frame_total_size);

    std::cout << "screen begin frame=" << g_frame_id
        << " " << g_frame_width
        << "x" << g_frame_height
        << " size=" << g_frame_total_size
        << std::endl;
}

void handleScreenChunk(const Packet& pkt)
{
    int header_size = sizeof(ScreenChunkHeader);

    if (!g_receiving_frame) {
        return;
    }

    if (pkt.body_len < header_size) {
        std::cout << "invalid screen chunk len=" << pkt.body_len << std::endl;
        return;
    }

    ScreenChunkHeader header;
    memcpy(&header, pkt.data, header_size);

    if (header.frame_id != g_frame_id) {
        return;
    }

    if (header.data_len <= 0) {
        return;
    }

    if (header.offset < 0 || header.offset + header.data_len > g_frame_total_size) {
        std::cout << "invalid screen chunk offset" << std::endl;
        return;
    }

    if (header_size + header.data_len > pkt.body_len) {
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

void handleScreenEnd(const Packet& pkt)
{
    if (pkt.body_len != sizeof(int)) {
        std::cout << "invalid screen end len=" << pkt.body_len << std::endl;
        return;
    }

    int end_frame_id = -1;
    memcpy(&end_frame_id, pkt.data, sizeof(int));

    if (!g_receiving_frame || end_frame_id != g_frame_id) {
        return;
    }

    if (g_frame_received_size < g_frame_total_size) {
        std::cout << "screen frame incomplete received="
            << g_frame_received_size
            << " total=" << g_frame_total_size
            << std::endl;
        g_receiving_frame = false;
        return;
    }

    {
        std::lock_guard<std::mutex> lock(g_screen_mutex);
        g_screen_bgra = g_frame_buffer;
        g_screen_width = g_frame_width;
        g_screen_height = g_frame_height;
    }

    g_receiving_frame = false;

    if (g_hwnd != NULL) {
        InvalidateRect(g_hwnd, NULL, FALSE);
    }

    std::cout << "screen frame ready frame=" << end_frame_id << std::endl;
}

void drawRemoteScreen(HDC hdc, RECT client_rect)
{
    std::vector<unsigned char> local_screen;
    int width = 0;
    int height = 0;

    {
        std::lock_guard<std::mutex> lock(g_screen_mutex);
        local_screen = g_screen_bgra;
        width = g_screen_width;
        height = g_screen_height;
    }

    int client_width = client_rect.right - client_rect.left;
    int client_height = client_rect.bottom - client_rect.top;

    if (local_screen.empty() || width <= 0 || height <= 0) {
        const char* text1 = "Windows -> Linux Remote Control";
        const char* text2 = "Waiting for Linux screen frames...";
        const char* text3 = "Mouse and keyboard events can still be sent.";

        TextOutA(hdc, 20, 20, text1, (int)strlen(text1));
        TextOutA(hdc, 20, 50, text2, (int)strlen(text2));
        TextOutA(hdc, 20, 80, text3, (int)strlen(text3));
        return;
    }

    BITMAPINFO bmi;
    memset(&bmi, 0, sizeof(bmi));

    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    StretchDIBits(
        hdc,
        0,
        0,
        client_width,
        client_height,
        0,
        0,
        width,
        height,
        local_screen.data(),
        &bmi,
        DIB_RGB_COLORS,
        SRCCOPY
    );
}

std::string vkToXdotoolKey(WPARAM wParam)
{
    if (wParam >= 'A' && wParam <= 'Z') {
        char key[2] = { (char)wParam, '\0' };
        return key;
    }

    if (wParam >= '0' && wParam <= '9') {
        char key[2] = { (char)wParam, '\0' };
        return key;
    }

    if (wParam >= VK_F1 && wParam <= VK_F12) {
        int index = (int)(wParam - VK_F1) + 1;
        char key[8];
        snprintf(key, sizeof(key), "F%d", index);
        return key;
    }

    switch (wParam)
    {
    case VK_RETURN:
        return "Return";
    case VK_BACK:
        return "BackSpace";
    case VK_TAB:
        return "Tab";
    case VK_ESCAPE:
        return "Escape";
    case VK_SPACE:
        return "space";
    case VK_LEFT:
        return "Left";
    case VK_RIGHT:
        return "Right";
    case VK_UP:
        return "Up";
    case VK_DOWN:
        return "Down";
    case VK_SHIFT:
        return "Shift_L";
    case VK_CONTROL:
        return "Control_L";
    case VK_MENU:
        return "Alt_L";
    case VK_DELETE:
        return "Delete";
    case VK_INSERT:
        return "Insert";
    case VK_HOME:
        return "Home";
    case VK_END:
        return "End";
    case VK_PRIOR:
        return "Page_Up";
    case VK_NEXT:
        return "Page_Down";
    case VK_CAPITAL:
        return "Caps_Lock";
    case VK_OEM_MINUS:
        return "minus";
    case VK_OEM_PLUS:
        return "equal";
    case VK_OEM_4:
        return "bracketleft";
    case VK_OEM_6:
        return "bracketright";
    case VK_OEM_1:
        return "semicolon";
    case VK_OEM_7:
        return "apostrophe";
    case VK_OEM_COMMA:
        return "comma";
    case VK_OEM_PERIOD:
        return "period";
    case VK_OEM_2:
        return "slash";
    case VK_OEM_5:
        return "backslash";
    case VK_OEM_3:
        return "grave";
    default:
        return "";
    }
}
