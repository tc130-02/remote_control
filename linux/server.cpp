#include <iostream>
#include <unistd.h>
#include <arpa/inet.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <cstdio>
#include <cstdlib>

#include "../common/packet.h"

// ================================
// 函数声明
// ================================
void handleMouseMove(const char* data);
void handleKeyPress(const char* data);
void handlePacket(const Packet& pkt);

int createServerSocket(int port);
int acceptClient(int server_fd);
void recvLoop(int client_fd);

int main()
{
    // ================================
    // 1. 创建监听 socket
    // ================================
    int server_fd = createServerSocket(8080);

    std::cout << "server waiting..." << std::endl;

    // ================================
    // 2. 等待客户端连接
    // ================================
    int client_fd = acceptClient(server_fd);

    // ================================
    // 3. 循环接收并拆包
    // ================================
    recvLoop(client_fd);

    // ================================
    // 4. 关闭连接
    // ================================
    close(client_fd);
    close(server_fd);

    return 0;
}

// ================================
// 处理鼠标移动命令
// ================================
void handleMouseMove(const char* data)
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
// 处理键盘命令
// ================================
void handleKeyPress(const char* data)
{
    char c = data[0];
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "xdotool key %c", c);
    system(cmd);
    std::cout << "收到键盘命令：" << c << std::endl;
}

// ================================
// 分发 Packet 命令
// ================================
void handlePacket(const Packet& pkt)
{
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

    bind(server_fd, (sockaddr*)&addr, sizeof(addr));

    listen(server_fd, 5);

    return server_fd;
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
// ================================
void recvLoop(int client_fd)
{
    char buffer[1024] = {0};
    int offset = 0;

    while (true)
    {
        int len = recv(client_fd, buffer + offset, 1024 - offset, 0);

        if (len <= 0)
        {
            break;
        }

        offset += len;

        while (true)
        {
            if (offset < 12)
            {
                break;
            }

            int body_len = 0;

            memcpy(&body_len, buffer + 8, sizeof(int));

            if (body_len < 0 || body_len >= 256)
            {
                std::cout << "非法 body_len: " << body_len << std::endl;
                offset = 0;
                break;
            }

            if (offset < 12 + body_len)
            {
                break;
            }

            Packet pkt = decode(buffer);

            handlePacket(pkt);

            int pack_size = 12 + body_len;

            memmove(buffer,
                    buffer + pack_size,
                    offset - pack_size);

            offset -= pack_size;
        }
    }
}
