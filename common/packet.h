#pragma once

#include <cstdint>
#include <cstdlib>
#include <cstring>

constexpr int32_t PACKET_DATA_SIZE = 65536;
constexpr int32_t PACKET_HEADER_SIZE = 12;
constexpr int32_t PACKET_MAGIC = 0x12345678;

struct Packet {
    int32_t magic;
    int32_t cmd;
    int32_t body_len;
    char data[PACKET_DATA_SIZE];
};

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

// These structures are copied directly to the wire. Keep their field order and
// 32-bit widths stable until the protocol gains explicit serialization.
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
static_assert(
    sizeof(Packet) == PACKET_HEADER_SIZE + PACKET_DATA_SIZE,
    "Packet wire size changed"
);

inline char* encodePacket(const Packet* packet, int* output_length)
{
    if (packet == nullptr || output_length == nullptr) {
        return nullptr;
    }

    if (packet->body_len < 0 || packet->body_len > PACKET_DATA_SIZE) {
        *output_length = 0;
        return nullptr;
    }

    *output_length = PACKET_HEADER_SIZE + packet->body_len;
    char* buffer = static_cast<char*>(std::malloc(*output_length));
    if (buffer == nullptr) {
        *output_length = 0;
        return nullptr;
    }

    std::memcpy(buffer, &packet->magic, sizeof(int32_t));
    std::memcpy(
        buffer + sizeof(int32_t),
        &packet->cmd,
        sizeof(int32_t)
    );
    std::memcpy(
        buffer + sizeof(int32_t) * 2,
        &packet->body_len,
        sizeof(int32_t)
    );

    if (packet->body_len > 0) {
        std::memcpy(
            buffer + PACKET_HEADER_SIZE,
            packet->data,
            packet->body_len
        );
    }

    return buffer;
}

inline Packet decodePacket(const char* buffer)
{
    Packet packet = {};

    std::memcpy(&packet.magic, buffer, sizeof(int32_t));
    std::memcpy(
        &packet.cmd,
        buffer + sizeof(int32_t),
        sizeof(int32_t)
    );
    std::memcpy(
        &packet.body_len,
        buffer + sizeof(int32_t) * 2,
        sizeof(int32_t)
    );

    if (packet.body_len > 0 && packet.body_len <= PACKET_DATA_SIZE) {
        std::memcpy(
            packet.data,
            buffer + PACKET_HEADER_SIZE,
            packet.body_len
        );

        // Small text commands are read as C strings by the legacy handlers.
        if (packet.body_len < 256) {
            packet.data[packet.body_len] = '\0';
        }
    }

    return packet;
}
