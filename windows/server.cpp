#define _WINSOCK_DEPRECATED_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <winsock2.h>
#include <windows.h>
#include <iostream>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <thread>
#include <atomic>
#include <algorithm>
#include <chrono>
#include <string>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")

#include "../common/packet.h"

const int SERVER_PORT = 8081;
const int RECV_BUFFER_SIZE = 262144;
const int MAX_SCREEN_SIZE = 100 * 1024 * 1024;

SOCKET g_server_socket = INVALID_SOCKET;
SOCKET g_client_socket = INVALID_SOCKET;
std::atomic<bool> g_running(false);

bool initServer(int port);
SOCKET acceptClient(SOCKET server_socket);
void recvLoop(SOCKET client_socket);
void screenSendLoop(SOCKET client_socket);
bool sendRealScreenFrame(SOCKET client_socket, int frame_id);

Packet buildPacket(int cmd, const char* msg);
Packet buildRawPacket(int cmd, const char* buffer, int len);
bool sendAll(SOCKET sock, const char* buf, int len);
bool sendPacket(SOCKET sock, const Packet& pkt);
void sendHello(SOCKET sock, const char* msg);

void handlePacket(const Packet& pkt);
void handleMouseEvent(const char* data, int len);
void handleKeyEvent(const char* data, int len);
void handleOldMouseMove(const char* data);
void handleOldMouseClick(const char* data);
void handleOldKeyPress(const char* data);

void moveMouse(int x, int y);
void sendMouseInput(DWORD flags, DWORD mouseData = 0);
void clickMouseButton(int button);
void doubleClickMouseButton(int button);
bool keyNameToVk(const char* key, WORD& vk);
void sendKeyInput(WORD vk, bool key_down);

int main()
{
    if (!initServer(SERVER_PORT)) {
        std::cout << "init server failed" << std::endl;
        return 1;
    }

    std::cout << "windows server waiting on 0.0.0.0:" << SERVER_PORT << " ..." << std::endl;

    g_client_socket = acceptClient(g_server_socket);
    if (g_client_socket == INVALID_SOCKET) {
        std::cout << "accept failed" << std::endl;
        closesocket(g_server_socket);
        WSACleanup();
        return 1;
    }

    std::cout << "linux client connected" << std::endl;

    g_running = true;

    sendHello(g_client_socket, "hello from windows server");

    std::thread screen_thread(screenSendLoop, g_client_socket);

    recvLoop(g_client_socket);

    g_running = false;

    if (g_client_socket != INVALID_SOCKET) {
        shutdown(g_client_socket, SD_BOTH);
        closesocket(g_client_socket);
        g_client_socket = INVALID_SOCKET;
    }

    if (screen_thread.joinable()) {
        screen_thread.join();
    }

    if (g_server_socket != INVALID_SOCKET) {
        closesocket(g_server_socket);
        g_server_socket = INVALID_SOCKET;
    }

    WSACleanup();
    std::cout << "windows server stopped" << std::endl;

    return 0;
}

bool initServer(int port)
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

    BOOL opt = TRUE;
    setsockopt(g_server_socket, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    ret = bind(g_server_socket, (sockaddr*)&addr, sizeof(addr));
    if (ret == SOCKET_ERROR) {
        std::cout << "bind failed: " << WSAGetLastError() << std::endl;
        closesocket(g_server_socket);
        g_server_socket = INVALID_SOCKET;
        WSACleanup();
        return false;
    }

    ret = listen(g_server_socket, 5);
    if (ret == SOCKET_ERROR) {
        std::cout << "listen failed: " << WSAGetLastError() << std::endl;
        closesocket(g_server_socket);
        g_server_socket = INVALID_SOCKET;
        WSACleanup();
        return false;
    }

    return true;
}

SOCKET acceptClient(SOCKET server_socket)
{
    sockaddr_in client_addr = {};
    int client_len = sizeof(client_addr);

    SOCKET client_socket = accept(server_socket, (sockaddr*)&client_addr, &client_len);
    if (client_socket == INVALID_SOCKET) {
        std::cout << "accept failed: " << WSAGetLastError() << std::endl;
        return INVALID_SOCKET;
    }

    std::cout << "client ip=" << inet_ntoa(client_addr.sin_addr)
              << " port=" << ntohs(client_addr.sin_port) << std::endl;

    return client_socket;
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
        len = PACKET_DATA_SIZE;
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
        std::cout << "encodePacket failed" << std::endl;
        return false;
    }

    bool ok = sendAll(sock, buf, len);
    free(buf);

    return ok;
}

void sendHello(SOCKET sock, const char* msg)
{
    Packet pkt = buildPacket(CMD_HELLO, msg);
    if (sendPacket(sock, pkt)) {
        std::cout << "send hello: " << msg << std::endl;
    }
}

void recvLoop(SOCKET client_socket)
{
    char buffer[RECV_BUFFER_SIZE] = { 0 };
    int offset = 0;

    while (g_running) {
        int free_size = RECV_BUFFER_SIZE - offset;
        if (free_size <= 0) {
            std::cout << "recv buffer full, reset buffer" << std::endl;
            offset = 0;
            free_size = RECV_BUFFER_SIZE;
        }

        int len = recv(client_socket, buffer + offset, free_size, 0);

        if (len <= 0) {
            std::cout << "recv stopped, len=" << len
                      << " error=" << WSAGetLastError() << std::endl;
            break;
        }

        offset += len;

        while (true) {
            if (offset < 12) {
                break;
            }

            int magic = 0;
            int body_len = 0;
            memcpy(&magic, buffer, 4);
            memcpy(&body_len, buffer + 8, sizeof(int));

            if (magic != PACKET_MAGIC) {
                std::cout << "invalid packet magic: " << magic << ", reset buffer" << std::endl;
                offset = 0;
                break;
            }

            if (body_len < 0 || body_len > PACKET_DATA_SIZE) {
                std::cout << "invalid body_len: " << body_len << std::endl;
                offset = 0;
                break;
            }

            if (offset < 12 + body_len) {
                break;
            }

            Packet pkt = decodePacket(buffer);
            handlePacket(pkt);

            int pack_size = 12 + body_len;
            memmove(buffer, buffer + pack_size, offset - pack_size);
            offset -= pack_size;
        }
    }

    g_running = false;
}

void handlePacket(const Packet& pkt)
{
    if (pkt.magic != PACKET_MAGIC) {
        std::cout << "invalid packet magic" << std::endl;
        return;
    }

    if (pkt.cmd == CMD_HELLO) {
        std::cout << "recv hello: " << pkt.data << std::endl;
    }
    else if (pkt.cmd == CMD_MOUSE_MOVE) {
        handleOldMouseMove(pkt.data);
    }
    else if (pkt.cmd == CMD_MOUSE_CLICK) {
        handleOldMouseClick(pkt.data);
    }
    else if (pkt.cmd == CMD_KEY_PRESS) {
        handleOldKeyPress(pkt.data);
    }
    else if (pkt.cmd == CMD_MOUSE_EVENT) {
        handleMouseEvent(pkt.data, pkt.body_len);
    }
    else if (pkt.cmd == CMD_KEY_EVENT) {
        handleKeyEvent(pkt.data, pkt.body_len);
    }
    else {
        std::cout << "unknown cmd=" << pkt.cmd << " len=" << pkt.body_len << std::endl;
    }
}

void handleOldMouseMove(const char* data)
{
    int x = 0;
    int y = 0;
    if (sscanf(data, "%d,%d", &x, &y) == 2) {
        moveMouse(x, y);
        std::cout << "old mouse move x=" << x << " y=" << y << std::endl;
    }
}

void handleOldMouseClick(const char* data)
{
    int button = 0;
    if (sscanf(data, "%d", &button) == 1) {
        clickMouseButton(button);
        std::cout << "old mouse click button=" << button << std::endl;
    }
}

void handleOldKeyPress(const char* data)
{
    WORD vk = 0;
    if (!keyNameToVk(data, vk)) {
        std::cout << "unsupported old key: " << data << std::endl;
        return;
    }

    sendKeyInput(vk, true);
    sendKeyInput(vk, false);
    std::cout << "old key press: " << data << std::endl;
}

void moveMouse(int x, int y)
{
    SetCursorPos(x, y);
}

void sendMouseInput(DWORD flags, DWORD mouseData)
{
    INPUT input;
    memset(&input, 0, sizeof(input));

    input.type = INPUT_MOUSE;
    input.mi.dwFlags = flags;
    input.mi.mouseData = mouseData;

    SendInput(1, &input, sizeof(INPUT));
}

void clickMouseButton(int button)
{
    if (button == 1) {
        sendMouseInput(MOUSEEVENTF_LEFTDOWN);
        sendMouseInput(MOUSEEVENTF_LEFTUP);
    }
    else if (button == 2) {
        sendMouseInput(MOUSEEVENTF_MIDDLEDOWN);
        sendMouseInput(MOUSEEVENTF_MIDDLEUP);
    }
    else if (button == 3) {
        sendMouseInput(MOUSEEVENTF_RIGHTDOWN);
        sendMouseInput(MOUSEEVENTF_RIGHTUP);
    }
    else if (button == 4) {
        sendMouseInput(MOUSEEVENTF_WHEEL, WHEEL_DELTA);
    }
    else if (button == 5) {
        sendMouseInput(MOUSEEVENTF_WHEEL, (DWORD)(-WHEEL_DELTA));
    }
    else {
        std::cout << "invalid mouse button=" << button << std::endl;
    }
}

void doubleClickMouseButton(int button)
{
    clickMouseButton(button);
    Sleep(80);
    clickMouseButton(button);
}

void handleMouseEvent(const char* data, int len)
{
    if (len != sizeof(MouseEvent)) {
        std::cout << "invalid MouseEvent len=" << len << std::endl;
        return;
    }

    MouseEvent event;
    memcpy(&event, data, sizeof(event));

    if (event.action == MOUSE_ACTION_MOVE) {
        moveMouse(event.x, event.y);
    }
    else if (event.action == MOUSE_ACTION_DOWN) {
        moveMouse(event.x, event.y);
        if (event.button == 1) sendMouseInput(MOUSEEVENTF_LEFTDOWN);
        else if (event.button == 2) sendMouseInput(MOUSEEVENTF_MIDDLEDOWN);
        else if (event.button == 3) sendMouseInput(MOUSEEVENTF_RIGHTDOWN);
    }
    else if (event.action == MOUSE_ACTION_UP) {
        moveMouse(event.x, event.y);
        if (event.button == 1) sendMouseInput(MOUSEEVENTF_LEFTUP);
        else if (event.button == 2) sendMouseInput(MOUSEEVENTF_MIDDLEUP);
        else if (event.button == 3) sendMouseInput(MOUSEEVENTF_RIGHTUP);
    }
    else if (event.action == MOUSE_ACTION_CLICK) {
        moveMouse(event.x, event.y);
        clickMouseButton(event.button);
    }
    else if (event.action == MOUSE_ACTION_DOUBLE_CLICK) {
        moveMouse(event.x, event.y);
        doubleClickMouseButton(event.button);
    }
    else {
        std::cout << "unknown MouseEvent action=" << event.action << std::endl;
        return;
    }

    // std::cout << "mouse event action=" << event.action
    //           << " button=" << event.button
    //           << " x=" << event.x
    //           << " y=" << event.y << std::endl;
}

bool keyNameToVk(const char* key, WORD& vk)
{
    if (key == nullptr || key[0] == '\0') {
        return false;
    }

    if (strlen(key) == 1) {
        SHORT v = VkKeyScanA(key[0]);
        if (v == -1) {
            return false;
        }
        vk = LOBYTE(v);
        return true;
    }

    std::string k = key;

    if (k == "Return") vk = VK_RETURN;
    else if (k == "BackSpace") vk = VK_BACK;
    else if (k == "Tab") vk = VK_TAB;
    else if (k == "Escape") vk = VK_ESCAPE;
    else if (k == "space") vk = VK_SPACE;
    else if (k == "Left") vk = VK_LEFT;
    else if (k == "Right") vk = VK_RIGHT;
    else if (k == "Up") vk = VK_UP;
    else if (k == "Down") vk = VK_DOWN;
    else if (k == "Shift_L") vk = VK_SHIFT;
    else if (k == "Control_L") vk = VK_CONTROL;
    else if (k == "Alt_L") vk = VK_MENU;
    else if (k == "Delete") vk = VK_DELETE;
    else if (k == "Insert") vk = VK_INSERT;
    else if (k == "Home") vk = VK_HOME;
    else if (k == "End") vk = VK_END;
    else if (k == "Page_Up") vk = VK_PRIOR;
    else if (k == "Page_Down") vk = VK_NEXT;
    else if (k == "Caps_Lock") vk = VK_CAPITAL;
    else if (k == "minus") vk = VK_OEM_MINUS;
    else if (k == "equal") vk = VK_OEM_PLUS;
    else if (k == "bracketleft") vk = VK_OEM_4;
    else if (k == "bracketright") vk = VK_OEM_6;
    else if (k == "semicolon") vk = VK_OEM_1;
    else if (k == "apostrophe") vk = VK_OEM_7;
    else if (k == "comma") vk = VK_OEM_COMMA;
    else if (k == "period") vk = VK_OEM_PERIOD;
    else if (k == "slash") vk = VK_OEM_2;
    else if (k == "backslash") vk = VK_OEM_5;
    else if (k == "grave") vk = VK_OEM_3;
    else if (k.size() >= 2 && k[0] == 'F') {
        int index = atoi(k.c_str() + 1);
        if (index >= 1 && index <= 12) {
            vk = (WORD)(VK_F1 + index - 1);
        }
        else {
            return false;
        }
    }
    else {
        return false;
    }

    return true;
}

void sendKeyInput(WORD vk, bool key_down)
{
    INPUT input;
    memset(&input, 0, sizeof(input));

    input.type = INPUT_KEYBOARD;
    input.ki.wVk = vk;
    input.ki.dwFlags = key_down ? 0 : KEYEVENTF_KEYUP;

    SendInput(1, &input, sizeof(INPUT));
}

void handleKeyEvent(const char* data, int len)
{
    if (len != sizeof(KeyEvent)) {
        std::cout << "invalid KeyEvent len=" << len << std::endl;
        return;
    }

    KeyEvent event;
    memcpy(&event, data, sizeof(event));
    event.key[sizeof(event.key) - 1] = '\0';

    WORD vk = 0;
    if (!keyNameToVk(event.key, vk)) {
        std::cout << "unsupported key=" << event.key << std::endl;
        return;
    }

    if (event.key_status == KEY_STATUS_DOWN) {
        sendKeyInput(vk, true);
        std::cout << "key down: " << event.key << std::endl;
    }
    else if (event.key_status == KEY_STATUS_UP) {
        sendKeyInput(vk, false);
        std::cout << "key up: " << event.key << std::endl;
    }
    else {
        std::cout << "invalid key_status=" << event.key_status << std::endl;
    }
}

bool sendRealScreenFrame(SOCKET client_socket, int frame_id)
{
    auto t0 = std::chrono::steady_clock::now();

    int width = GetSystemMetrics(SM_CXSCREEN);
    int height = GetSystemMetrics(SM_CYSCREEN);

    if (width <= 0 || height <= 0) {
        std::cout << "invalid screen size" << std::endl;
        return false;
    }

    long long total64 = 1LL * width * height * 4;
    if (total64 <= 0 || total64 > MAX_SCREEN_SIZE) {
        std::cout << "screen frame too large: " << total64 << std::endl;
        return false;
    }

    int total_size = (int)total64;
    std::vector<unsigned char> frame(total_size);

    HDC screen_dc = GetDC(NULL);
    if (screen_dc == NULL) {
        std::cout << "GetDC failed" << std::endl;
        return false;
    }

    HDC mem_dc = CreateCompatibleDC(screen_dc);
    if (mem_dc == NULL) {
        std::cout << "CreateCompatibleDC failed" << std::endl;
        ReleaseDC(NULL, screen_dc);
        return false;
    }

    BITMAPINFO bmi;
    memset(&bmi, 0, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(screen_dc, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (bitmap == NULL || bits == nullptr) {
        std::cout << "CreateDIBSection failed" << std::endl;
        DeleteDC(mem_dc);
        ReleaseDC(NULL, screen_dc);
        return false;
    }

    HGDIOBJ old_bitmap = SelectObject(mem_dc, bitmap);

    BOOL ok = BitBlt(mem_dc, 0, 0, width, height, screen_dc, 0, 0, SRCCOPY | CAPTUREBLT);
    auto t1 = std::chrono::steady_clock::now();

    if (!ok) {
        std::cout << "BitBlt failed" << std::endl;
        SelectObject(mem_dc, old_bitmap);
        DeleteObject(bitmap);
        DeleteDC(mem_dc);
        ReleaseDC(NULL, screen_dc);
        return false;
    }

    memcpy(frame.data(), bits, total_size);
    for (int i = 3; i < total_size; i += 4) {
        frame[i] = 255;
    }

    auto t2 = std::chrono::steady_clock::now();

    SelectObject(mem_dc, old_bitmap);
    DeleteObject(bitmap);
    DeleteDC(mem_dc);
    ReleaseDC(NULL, screen_dc);

    ScreenFrameInfo info = {};
    info.frame_id = frame_id;
    info.width = width;
    info.height = height;
    info.total_size = total_size;
    info.format = SCREEN_FORMAT_BGRA32;

    Packet begin_pkt = buildRawPacket(CMD_SCREEN_BEGIN, (const char*)&info, sizeof(info));
    if (!sendPacket(client_socket, begin_pkt)) {
        std::cout << "send screen begin failed" << std::endl;
        return false;
    }

    int header_size = sizeof(ScreenChunkHeader);
    int max_payload = PACKET_DATA_SIZE - header_size;
    int offset = 0;
    int chunk_count = 0;

    while (offset < total_size) {
        int remain = total_size - offset;
        int data_len = std::min(remain, max_payload);

        Packet chunk_pkt;
        memset(&chunk_pkt, 0, sizeof(chunk_pkt));
        chunk_pkt.magic = PACKET_MAGIC;
        chunk_pkt.cmd = CMD_SCREEN_CHUNK;
        chunk_pkt.body_len = header_size + data_len;

        ScreenChunkHeader header = {};
        header.frame_id = frame_id;
        header.offset = offset;
        header.data_len = data_len;

        memcpy(chunk_pkt.data, &header, header_size);
        memcpy(chunk_pkt.data + header_size, frame.data() + offset, data_len);

        if (!sendPacket(client_socket, chunk_pkt)) {
            std::cout << "send screen chunk failed" << std::endl;
            return false;
        }

        offset += data_len;
        chunk_count++;
    }

    Packet end_pkt = buildRawPacket(CMD_SCREEN_END, (const char*)&frame_id, sizeof(frame_id));
    if (!sendPacket(client_socket, end_pkt)) {
        std::cout << "send screen end failed" << std::endl;
        return false;
    }

    auto t3 = std::chrono::steady_clock::now();

    static int debug_frame_count = 0;
    debug_frame_count++;

    if (debug_frame_count % 10 == 0) {
        auto capture_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
        auto convert_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1).count();
        auto send_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t3 - t2).count();
        auto total_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t3 - t0).count();

        std::cout << "[frame " << frame_id << "] "
                  << "capture=" << capture_ms << "ms, "
                  << "convert=" << convert_ms << "ms, "
                  << "send=" << send_ms << "ms, "
                  << "total=" << total_ms << "ms, "
                  << "chunks=" << chunk_count << std::endl;
    }

    return true;
}

void screenSendLoop(SOCKET client_socket)
{
    int frame_id = 1;

    while (g_running) {
        if (!sendRealScreenFrame(client_socket, frame_id)) {
            g_running = false;
            break;
        }

        frame_id++;
        Sleep(50);
    }
}
