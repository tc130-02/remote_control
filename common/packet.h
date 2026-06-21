// packet.h
#pragma once
#include <string.h>
const int PACKET_MAGIC = 0x12345678;
// ================================
// 命令类型
// ================================
enum CmdType {
    CMD_HELLO = 1,        // 测试通信
    CMD_MOUSE_MOVE = 2,   // 鼠标移动，后面做
    CMD_KEY_PRESS = 3,    // 键盘按键，后面做
    CMD_SCREEN = 4        // 屏幕数据，后面做
};

struct Packet {
    int magic;
    int cmd;
    int body_len;
    char data[256];
};

// encode：Packet -> byte流
inline char* encode(Packet pkt, int& out_len)
{
    char* buf = new char[12 + pkt.body_len];

    memcpy(buf, &pkt.magic, sizeof(int));
    memcpy(buf + 4, &pkt.cmd, sizeof(int));
    memcpy(buf + 8, &pkt.body_len, sizeof(int));
    memcpy(buf + 12, pkt.data, pkt.body_len);

    out_len = 12 + pkt.body_len;
    return buf;
}

// decode：byte流 -> Packet
inline Packet decode(char* buffer)
{
    Packet pkt;

    memcpy(&pkt.magic, buffer, sizeof(int));
    memcpy(&pkt.cmd, buffer + 4, sizeof(int));
    memcpy(&pkt.body_len, buffer + 8, sizeof(int));
    memcpy(pkt.data, buffer + 12, pkt.body_len);

    pkt.data[pkt.body_len] = '\0'; // 防止乱码

    return pkt;
}