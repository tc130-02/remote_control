#pragma once

#include <stdint.h>
#include <string.h>
#include <stdlib.h>
const int32_t PACKET_DATA_SIZE = 65536;
const int32_t PACKET_HEADER_SIZE = 12;

struct Packet {
    int32_t magic;
    int32_t cmd;
    int32_t body_len;
    char data[PACKET_DATA_SIZE];
};

const int32_t PACKET_MAGIC = 0x12345678;

enum CmdType : int32_t {
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

enum MouseAction : int32_t {
    MOUSE_ACTION_MOVE = 1,
    MOUSE_ACTION_DOWN = 2,
    MOUSE_ACTION_UP = 3,
    MOUSE_ACTION_CLICK = 4,
    MOUSE_ACTION_DOUBLE_CLICK = 5
};

enum KeyStatus : int32_t {
    KEY_STATUS_DOWN = 0,
    KEY_STATUS_UP = 1
};

enum RemoteScreenFormat : int32_t {
    SCREEN_FORMAT_BGRA32 = 0,
    SCREEN_FORMAT_JPEG = 1
};

struct MouseEvent {
    int32_t action;
    int32_t button;
    int32_t x;
    int32_t y;
};

struct KeyEvent {
    int32_t key_status;
    char key[32];
};

struct ScreenFrameInfo {
    int32_t frame_id;
    int32_t width;
    int32_t height;
    int32_t total_size;
    int32_t format;
};

struct ScreenChunkHeader {
    int32_t frame_id;
    int32_t offset;
    int32_t data_len;
};

static_assert(sizeof(int32_t) == 4, "protocol requires 32-bit integers");
static_assert(sizeof(MouseEvent) == 16, "MouseEvent wire size changed");
static_assert(sizeof(KeyEvent) == 36, "KeyEvent wire size changed");
static_assert(sizeof(ScreenFrameInfo) == 20, "ScreenFrameInfo wire size changed");
static_assert(sizeof(ScreenChunkHeader) == 12, "ScreenChunkHeader wire size changed");
static_assert(sizeof(Packet) == PACKET_HEADER_SIZE + PACKET_DATA_SIZE,
              "Packet wire size changed");

inline char* encodePacket(const Packet* pkt, int* out_len)
{
    if (pkt == NULL || out_len == NULL) {
        return NULL;
    }

    if (pkt->body_len < 0 || pkt->body_len > PACKET_DATA_SIZE) {
        *out_len = 0;
        return NULL;
    }

    *out_len = PACKET_HEADER_SIZE + pkt->body_len;

    char* buf = (char*)malloc(*out_len);
    if (buf == NULL) {
        *out_len = 0;
        return NULL;
    }

    memcpy(buf, &pkt->magic, sizeof(int32_t));
    memcpy(buf + sizeof(int32_t), &pkt->cmd, sizeof(int32_t));
    memcpy(buf + sizeof(int32_t) * 2, &pkt->body_len, sizeof(int32_t));

    if (pkt->body_len > 0) {
        memcpy(buf + PACKET_HEADER_SIZE, pkt->data, pkt->body_len);
    }

    return buf;
}

inline Packet decodePacket(const char* buffer)
{
    Packet pkt;
    memset(&pkt, 0, sizeof(pkt));

    memcpy(&pkt.magic, buffer, sizeof(int32_t));
    memcpy(&pkt.cmd, buffer + sizeof(int32_t), sizeof(int32_t));
    memcpy(&pkt.body_len, buffer + sizeof(int32_t) * 2, sizeof(int32_t));

    if (pkt.body_len > 0 && pkt.body_len <= PACKET_DATA_SIZE) {
        memcpy(pkt.data, buffer + PACKET_HEADER_SIZE, pkt.body_len);

        if (pkt.body_len < 256) {
            pkt.data[pkt.body_len] = '\0';
        }
    }

    return pkt;
}
