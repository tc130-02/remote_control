#pragma once

#include <string.h>
#include <stdlib.h>
const int PACKET_DATA_SIZE = 65536;

struct Packet {
    int magic;
    int cmd;
    int body_len;
    char data[PACKET_DATA_SIZE];
};

const int PACKET_MAGIC = 0x12345678;

enum CmdType {
    CMD_HELLO = 1,

    CMD_MOUSE_MOVE = 2,
    CMD_KEY_PRESS = 3,
    CMD_MOUSE_CLICK = 4,

    CMD_SCREEN = 5,

    CMD_MOUSE_EVENT = 10,
    CMD_KEY_EVENT = 11,

    CMD_SCREEN_BEGIN = 20,
    CMD_SCREEN_CHUNK = 21,
    CMD_SCREEN_END = 22
};

enum MouseAction {
    MOUSE_ACTION_MOVE = 1,
    MOUSE_ACTION_DOWN = 2,
    MOUSE_ACTION_UP = 3,
    MOUSE_ACTION_CLICK = 4,
    MOUSE_ACTION_DOUBLE_CLICK = 5
};

enum KeyStatus {
    KEY_STATUS_DOWN = 0,
    KEY_STATUS_UP = 1
};

enum RemoteScreenFormat {
    SCREEN_FORMAT_BGRA32 = 1
};

struct MouseEvent {
    int action;
    int button;
    int x;
    int y;
};

struct KeyEvent {
    int key_status;
    char key[32];
};

struct ScreenFrameInfo {
    int frame_id;
    int width;
    int height;
    int total_size;
    int format;
};

struct ScreenChunkHeader {
    int frame_id;
    int offset;
    int data_len;
};


inline char* encodePacket(const Packet* pkt, int* out_len)
{
    if (pkt == NULL || out_len == NULL) {
        return NULL;
    }

    if (pkt->body_len < 0 || pkt->body_len > PACKET_DATA_SIZE) {
        *out_len = 0;
        return NULL;
    }

    *out_len = 12 + pkt->body_len;

    char* buf = (char*)malloc(*out_len);
    if (buf == NULL) {
        *out_len = 0;
        return NULL;
    }

    memcpy(buf, &pkt->magic, 4);
    memcpy(buf + 4, &pkt->cmd, 4);
    memcpy(buf + 8, &pkt->body_len, 4);

    if (pkt->body_len > 0) {
        memcpy(buf + 12, pkt->data, pkt->body_len);
    }

    return buf;
}

inline Packet decodePacket(const char* buffer)
{
    Packet pkt;
    memset(&pkt, 0, sizeof(pkt));

    memcpy(&pkt.magic, buffer, 4);
    memcpy(&pkt.cmd, buffer + 4, 4);
    memcpy(&pkt.body_len, buffer + 8, 4);

    if (pkt.body_len > 0 && pkt.body_len <= PACKET_DATA_SIZE) {
        memcpy(pkt.data, buffer + 12, pkt.body_len);

        if (pkt.body_len < 256) {
            pkt.data[pkt.body_len] = '\0';
        }
    }

    return pkt;
}