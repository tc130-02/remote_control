#define _WINSOCK_DEPRECATED_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iostream>
#include <cstring>
#include <string>
#include <fstream>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <algorithm>

#pragma comment(lib, "ws2_32.lib")

#include "../common/packet.h"

SOCKET g_server_socket = INVALID_SOCKET;
SOCKET g_connect_socket = INVALID_SOCKET;
HWND g_hwnd = NULL;

HWND g_host_label = NULL;
HWND g_host_edit = NULL;
HWND g_port_label = NULL;
HWND g_port_edit = NULL;
HWND g_connect_button = NULL;
HWND g_status_static = NULL;

const int IDC_HOST_EDIT = 1001;
const int IDC_PORT_EDIT = 1002;
const int IDC_CONNECT_BUTTON = 1003;
const UINT WM_APP_CONNECT_COMPLETE = WM_APP + 1;
const UINT WM_APP_CONNECTION_LOST = WM_APP + 2;

std::string g_default_host;
std::string g_default_port;
std::string g_attempt_host;
std::string g_attempt_port;

int g_remote_width = 1918;
int g_remote_height = 918;

int g_last_remote_x = -1;
int g_last_remote_y = -1;
DWORD g_last_mouse_send_time = 0;

bool g_use_mouse_down_up = false;
bool g_use_new_key_event = true;

std::atomic<bool> g_running(false);
std::atomic<bool> g_connected(false);
std::atomic<bool> g_connecting(false);
std::atomic<bool> g_app_running(true);
std::thread g_recv_thread;
std::thread g_connect_thread;
std::mutex g_socket_mutex;

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
int g_frame_chunk_count = 0;
bool g_receiving_frame = false;

bool InitSocket();
std::string GetServerConfigPath();
void LoadServerConfig(std::string& host, std::string& port);
bool SaveServerConfig(const std::string& host, const std::string& port);
bool ValidatePort(const std::string& port_text);
void StartConnect();
void ConnectWorker(std::string host, std::string port);
void SetConnectionUiVisible(bool visible);
void SetConnectionStatus(const std::string& status);
void ResetRemoteScreenState();
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

void recvThreadProc(SOCKET connected_socket);
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
    case WM_CREATE:
    {
        HINSTANCE instance = (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE);

        g_host_label = CreateWindowA(
            "STATIC", "Server address:",
            WS_CHILD | WS_VISIBLE,
            40, 70, 110, 24,
            hwnd, NULL, instance, NULL
        );
        g_host_edit = CreateWindowExA(
            WS_EX_CLIENTEDGE, "EDIT", g_default_host.c_str(),
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
            155, 66, 340, 28,
            hwnd, (HMENU)(INT_PTR)IDC_HOST_EDIT, instance, NULL
        );
        g_port_label = CreateWindowA(
            "STATIC", "Port:",
            WS_CHILD | WS_VISIBLE,
            40, 112, 110, 24,
            hwnd, NULL, instance, NULL
        );
        g_port_edit = CreateWindowExA(
            WS_EX_CLIENTEDGE, "EDIT", g_default_port.c_str(),
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
            155, 108, 140, 28,
            hwnd, (HMENU)(INT_PTR)IDC_PORT_EDIT, instance, NULL
        );
        g_connect_button = CreateWindowA(
            "BUTTON", "Connect",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
            315, 107, 180, 30,
            hwnd, (HMENU)(INT_PTR)IDC_CONNECT_BUTTON, instance, NULL
        );
        g_status_static = CreateWindowA(
            "STATIC", "waiting",
            WS_CHILD | WS_VISIBLE,
            40, 158, 700, 28,
            hwnd, NULL, instance, NULL
        );

        HFONT font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        SendMessage(g_host_label, WM_SETFONT, (WPARAM)font, TRUE);
        SendMessage(g_host_edit, WM_SETFONT, (WPARAM)font, TRUE);
        SendMessage(g_port_label, WM_SETFONT, (WPARAM)font, TRUE);
        SendMessage(g_port_edit, WM_SETFONT, (WPARAM)font, TRUE);
        SendMessage(g_connect_button, WM_SETFONT, (WPARAM)font, TRUE);
        SendMessage(g_status_static, WM_SETFONT, (WPARAM)font, TRUE);

        SetFocus(g_host_edit);
    }
    break;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDC_CONNECT_BUTTON
            && HIWORD(wParam) == BN_CLICKED) {
            StartConnect();
        }
        break;

    case WM_APP_CONNECT_COMPLETE:
    {
        if (g_connect_thread.joinable()) {
            g_connect_thread.join();
        }

        g_connecting = false;

        if (wParam != 0) {
            g_connected = true;
            SetConnectionStatus("connected");
            SetWindowTextA(
                hwnd,
                ("Remote Control - connected to "
                    + g_attempt_host + ":" + g_attempt_port).c_str()
            );
            SaveServerConfig(g_attempt_host, g_attempt_port);
            SetConnectionUiVisible(false);

            SOCKET connected_socket = INVALID_SOCKET;
            {
                std::lock_guard<std::mutex> lock(g_socket_mutex);
                connected_socket = g_server_socket;
            }

            if (connected_socket != INVALID_SOCKET) {
                sendHello(connected_socket, "hello linux window client");
                g_running = true;
                g_recv_thread = std::thread(
                    recvThreadProc,
                    connected_socket
                );
            }
        }
        else {
            int error_code = (int)lParam;
            SetConnectionStatus(
                "connect failed, WSA error=" + std::to_string(error_code)
            );
            EnableWindow(g_connect_button, TRUE);
            SetFocus(g_host_edit);
        }
    }
    break;

    case WM_APP_CONNECTION_LOST:
    {
        if (g_recv_thread.joinable()) {
            g_recv_thread.join();
        }

        g_running = false;
        g_connected = false;
        ResetRemoteScreenState();
        SetWindowTextA(hwnd, "Remote Control Client");
        SetConnectionUiVisible(true);
        EnableWindow(g_connect_button, TRUE);

        int error_code = (int)wParam;
        if (error_code != 0) {
            SetConnectionStatus(
                "waiting - disconnected, WSA error="
                + std::to_string(error_code)
            );
        }
        else {
            SetConnectionStatus("waiting - disconnected");
        }

        InvalidateRect(hwnd, NULL, TRUE);
        SetFocus(g_host_edit);
    }
    break;

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        RECT client_rect;
        GetClientRect(hwnd, &client_rect);

        if (g_connected) {
            drawRemoteScreen(hdc, client_rect);
        }
        else {
            FillRect(hdc, &client_rect, (HBRUSH)(COLOR_WINDOW + 1));
            const char* title = "Remote Control Connection";
            TextOutA(hdc, 40, 28, title, (int)strlen(title));
        }

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
        g_app_running = false;
        g_running = false;
        g_connected = false;

        {
            std::lock_guard<std::mutex> lock(g_socket_mutex);

            if (g_connect_socket != INVALID_SOCKET) {
                shutdown(g_connect_socket, SD_BOTH);
                closesocket(g_connect_socket);
                g_connect_socket = INVALID_SOCKET;
            }

            if (g_server_socket != INVALID_SOCKET) {
                shutdown(g_server_socket, SD_BOTH);
                closesocket(g_server_socket);
                g_server_socket = INVALID_SOCKET;
            }
        }

        if (g_connect_thread.joinable()) {
            g_connect_thread.join();
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

    LoadServerConfig(g_default_host, g_default_port);

    if (!InitWindow(hInstance, nCmdShow)) {
        WSACleanup();
        return 0;
    }

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
        "Remote Control Client",
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

    return true;
}

std::string GetServerConfigPath()
{
    char executable_path[MAX_PATH] = { 0 };
    DWORD path_length = GetModuleFileNameA(NULL, executable_path, MAX_PATH);
    if (path_length == 0 || path_length >= MAX_PATH) {
        std::cout << "failed to get program directory: " << GetLastError() << std::endl;
        return "";
    }

    std::string config_path(executable_path, path_length);
    size_t separator = config_path.find_last_of("\\/");
    if (separator != std::string::npos) {
        config_path.resize(separator + 1);
    }
    else {
        config_path.clear();
    }
    config_path += "server.conf";
    return config_path;
}

void LoadServerConfig(std::string& host, std::string& port)
{
    host.clear();
    port.clear();

    std::string config_path = GetServerConfigPath();
    if (config_path.empty()) {
        return;
    }

    std::ifstream config(config_path);
    if (!config.is_open()) {
        return;
    }

    auto trim = [](const std::string& value) {
        size_t first = value.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) {
            return std::string();
        }

        size_t last = value.find_last_not_of(" \t\r\n");
        return value.substr(first, last - first + 1);
    };

    std::getline(config, host);
    std::getline(config, port);
    host = trim(host);
    port = trim(port);

    if (!ValidatePort(port)) {
        port.clear();
    }
}

bool SaveServerConfig(const std::string& host, const std::string& port)
{
    std::string config_path = GetServerConfigPath();
    if (config_path.empty()) {
        return false;
    }

    std::ofstream config(config_path, std::ios::trunc);
    if (!config.is_open()) {
        std::cout << "failed to save server.conf: " << config_path << std::endl;
        return false;
    }

    config << host << "\n" << port << "\n";
    return config.good();
}

bool ValidatePort(const std::string& port_text)
{
    if (port_text.empty()) {
        return false;
    }

    char* port_end = nullptr;
    long port = std::strtol(port_text.c_str(), &port_end, 10);
    return port_end != port_text.c_str()
        && *port_end == '\0'
        && port >= 1
        && port <= 65535;
}

void SetConnectionStatus(const std::string& status)
{
    if (g_status_static != NULL) {
        SetWindowTextA(g_status_static, status.c_str());
    }
}

void SetConnectionUiVisible(bool visible)
{
    int command = visible ? SW_SHOW : SW_HIDE;
    ShowWindow(g_host_label, command);
    ShowWindow(g_host_edit, command);
    ShowWindow(g_port_label, command);
    ShowWindow(g_port_edit, command);
    ShowWindow(g_connect_button, command);
    ShowWindow(g_status_static, command);
}

void ResetRemoteScreenState()
{
    std::lock_guard<std::mutex> lock(g_screen_mutex);
    g_screen_bgra.clear();
    g_screen_width = 0;
    g_screen_height = 0;
    g_frame_buffer.clear();
    g_frame_id = -1;
    g_frame_width = 0;
    g_frame_height = 0;
    g_frame_total_size = 0;
    g_frame_received_size = 0;
    g_frame_chunk_count = 0;
    g_receiving_frame = false;
}

void StartConnect()
{
    if (g_connecting || g_connected || !g_app_running) {
        return;
    }

    char host_buffer[512] = { 0 };
    char port_buffer[32] = { 0 };
    GetWindowTextA(g_host_edit, host_buffer, sizeof(host_buffer));
    GetWindowTextA(g_port_edit, port_buffer, sizeof(port_buffer));

    auto trim = [](const std::string& value) {
        size_t first = value.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) {
            return std::string();
        }
        size_t last = value.find_last_not_of(" \t\r\n");
        return value.substr(first, last - first + 1);
    };

    std::string host = trim(host_buffer);
    std::string port = trim(port_buffer);

    if (host.empty()) {
        SetConnectionStatus("connect failed: server address is empty");
        SetFocus(g_host_edit);
        return;
    }

    if (!ValidatePort(port)) {
        SetConnectionStatus("connect failed: port must be 1-65535");
        SetFocus(g_port_edit);
        return;
    }

    if (g_connect_thread.joinable()) {
        g_connect_thread.join();
    }

    g_attempt_host = host;
    g_attempt_port = port;
    g_connecting = true;
    EnableWindow(g_connect_button, FALSE);
    SetConnectionStatus("connecting");

    g_connect_thread = std::thread(ConnectWorker, host, port);
}

void ConnectWorker(std::string host, std::string port)
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

    if (resolve_result != 0 || address_list == nullptr) {
        PostMessage(
            g_hwnd,
            WM_APP_CONNECT_COMPLETE,
            FALSE,
            (LPARAM)resolve_result
        );
        return;
    }

    SOCKET connected_socket = INVALID_SOCKET;
    int last_error = WSAHOST_NOT_FOUND;

    for (addrinfo* item = address_list;
         item != nullptr && g_app_running;
         item = item->ai_next) {
        SOCKET attempt_socket = socket(
            item->ai_family,
            item->ai_socktype,
            item->ai_protocol
        );

        if (attempt_socket == INVALID_SOCKET) {
            last_error = WSAGetLastError();
            continue;
        }

        {
            std::lock_guard<std::mutex> lock(g_socket_mutex);
            if (!g_app_running) {
                closesocket(attempt_socket);
                break;
            }
            g_connect_socket = attempt_socket;
        }

        int connect_result = connect(
            attempt_socket,
            item->ai_addr,
            (int)item->ai_addrlen
        );

        bool still_owned = false;
        {
            std::lock_guard<std::mutex> lock(g_socket_mutex);
            if (g_connect_socket == attempt_socket) {
                g_connect_socket = INVALID_SOCKET;
                still_owned = true;
            }
        }

        if (connect_result == 0 && still_owned && g_app_running) {
            connected_socket = attempt_socket;
            break;
        }

        last_error = WSAGetLastError();
        if (still_owned) {
            closesocket(attempt_socket);
        }
    }

    freeaddrinfo(address_list);

    if (connected_socket == INVALID_SOCKET) {
        if (g_app_running) {
            PostMessage(
                g_hwnd,
                WM_APP_CONNECT_COMPLETE,
                FALSE,
                (LPARAM)last_error
            );
        }
        return;
    }

    BOOL no_delay = TRUE;
    if (setsockopt(
            connected_socket,
            IPPROTO_TCP,
            TCP_NODELAY,
            (const char*)&no_delay,
            sizeof(no_delay)
        ) == SOCKET_ERROR) {
        std::cout << "set TCP_NODELAY failed: " << WSAGetLastError() << std::endl;
    }

    {
        std::lock_guard<std::mutex> lock(g_socket_mutex);
        if (!g_app_running) {
            closesocket(connected_socket);
            return;
        }
        g_server_socket = connected_socket;
    }

    PostMessage(g_hwnd, WM_APP_CONNECT_COMPLETE, TRUE, 0);
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

void recvThreadProc(SOCKET connected_socket)
{
    char buffer[262144] = { 0 };
    int offset = 0;
    int disconnect_error = 0;

    while (g_running) {
        if (offset >= (int)sizeof(buffer)) {
            std::cout << "recv buffer full, protocol error" << std::endl;
            break;
        }

        int len = recv(
            connected_socket,
            buffer + offset,
            sizeof(buffer) - offset,
            0
        );

        if (len == 0) {
            std::cout << "server closed connection" << std::endl;
            break;
        }

        if (len == SOCKET_ERROR) {
            disconnect_error = WSAGetLastError();
            std::cout << "recv failed: " << disconnect_error << std::endl;
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

    }

    g_running = false;
    g_connected = false;

    bool should_close = false;
    {
        std::lock_guard<std::mutex> lock(g_socket_mutex);
        if (g_server_socket == connected_socket) {
            g_server_socket = INVALID_SOCKET;
            should_close = true;
        }
    }

    if (should_close) {
        closesocket(connected_socket);
    }

    if (g_app_running) {
        PostMessage(
            g_hwnd,
            WM_APP_CONNECTION_LOST,
            (WPARAM)disconnect_error,
            0
        );
    }
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
    g_frame_chunk_count = 0;
    g_receiving_frame = true;

    g_frame_buffer.clear();
    g_frame_buffer.resize(g_frame_total_size);

    std::cout << "recv screen begin frame_id=" << g_frame_id
        << " total_size=" << g_frame_total_size
        << " resolution=" << g_frame_width
        << "x" << g_frame_height
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

    if (header.offset < 0
        || header.offset > g_frame_total_size
        || header.data_len > g_frame_total_size - header.offset) {
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
    g_frame_chunk_count++;

    if (g_frame_chunk_count % 10 == 0
        || header.offset + header.data_len == g_frame_total_size) {
        std::cout << "recv screen chunk frame_id=" << header.frame_id
            << " chunk=" << g_frame_chunk_count
            << " offset=" << header.offset
            << " len=" << header.data_len
            << " received_size=" << g_frame_received_size
            << "/" << g_frame_total_size
            << std::endl;
    }
}

void handleScreenEnd(const Packet& pkt)
{
    if (pkt.body_len != sizeof(int)) {
        std::cout << "invalid screen end len=" << pkt.body_len << std::endl;
        return;
    }

    int end_frame_id = -1;
    memcpy(&end_frame_id, pkt.data, sizeof(int));

    std::cout << "recv screen end frame_id=" << end_frame_id
        << " received_size=" << g_frame_received_size
        << " total_size=" << g_frame_total_size
        << " chunks=" << g_frame_chunk_count
        << std::endl;

    if (!g_receiving_frame || end_frame_id != g_frame_id) {
        std::cout << "screen end frame mismatch current_frame_id="
            << g_frame_id
            << " receiving=" << g_receiving_frame
            << std::endl;
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
