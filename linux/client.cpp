#include <iostream>
#include <unistd.h>
#include <arpa/inet.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <cstdio>
#include "../common/packet.h"

// ================================
// 函数声明
// ================================
int createClientSocket(const char* ip, int port);
Packet buildPacket(int cmd, const char* msg);
bool sendAll(int sock, const char* buf, int len);
void sendPacket(int sock, const Packet& pkt);
void sendKeyPress(int sock, char key);
void sendMouseMove(int sock, int x, int y);

int main()
{
    // ================================
    // 1. 连接服务器
    // ================================
    int sock = createClientSocket("127.0.0.1", 8080);

    // ================================
    // 2. 构造并发送命令
    // ================================
    

    // ================================
    // 3. 关闭连接
    // ================================
    close(sock);

    return 0;
}

// ================================
// 创建客户端 socket 并连接服务器
// ================================
int createClientSocket(const char* ip, int port)
{
    int sock = socket(AF_INET, SOCK_STREAM, 0);

    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = inet_addr(ip);

    connect(sock, (sockaddr*)&addr, sizeof(addr));

    return sock;
}

// ================================
// 构造 Packet
// ================================
Packet buildPacket(int cmd, const char* msg)
{
    Packet pkt = {};

    pkt.magic = PACKET_MAGIC;
    pkt.cmd = cmd;
    pkt.body_len = strlen(msg);
    strcpy(pkt.data, msg);

    return pkt;
}

// ================================
// 保证完整发送
// ================================
bool sendAll(int sock, const char* buf, int len)
{
    int total = 0;

    while (total < len)
    {
        int n = send(sock, buf + total, len - total, 0);

        if (n <= 0)
        {
            std::cout << "发送失败!" << std::endl;
            return false;
        }

        total += n;
    }

    return true;
}

// ================================
// 发送 Packet
// ================================
void sendPacket(int sock, const Packet& pkt)
{
    int len = 0;

    char* buf = encode(pkt, len);

    sendAll(sock, buf, len);

    delete[] buf;
}

//键盘命令发送
void sendKeyPress(int sock, char key){
    char msg[2];
    msg[0] = key;
    msg[1] = '\0';
    Packet pkt = buildPacket(CMD_KEY_PRESS,msg);
    sendPacket(sock, pkt);
}

//鼠标移动命令
void sendMouseMove(int sock, int x, int y){
    char msg[64];
    snprintf(msg, sizeof(msg), " %d,%d", x, y);
    Packet pkt = buildPacket(CMD_MOUSE_MOVE,msg);
    sendPacket(sock, pkt);
}