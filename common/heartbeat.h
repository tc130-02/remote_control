#pragma once

#include <chrono>
#include <cstdint>
#include <cstring>

#include "packet.h"

constexpr int64_t HEARTBEAT_IDLE_MS = 3000;
constexpr int64_t HEARTBEAT_TIMEOUT_MS = 10000;
constexpr int HEARTBEAT_POLL_MS = 500;

struct HeartbeatPayload {
    int64_t sequence;
    int64_t sent_at_ms;
};

static_assert(sizeof(HeartbeatPayload) == 16, "HeartbeatPayload wire size changed");

inline int64_t heartbeatNowMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()
    ).count();
}

inline Packet buildHeartbeatPacket(int32_t command, int64_t sequence)
{
    Packet packet = {};
    packet.magic = PACKET_MAGIC;
    packet.cmd = command;
    packet.body_len = sizeof(HeartbeatPayload);

    HeartbeatPayload payload = {};
    payload.sequence = sequence;
    payload.sent_at_ms = heartbeatNowMs();
    std::memcpy(packet.data, &payload, sizeof(payload));

    return packet;
}

inline bool readHeartbeatPayload(
    const Packet& packet,
    HeartbeatPayload& payload
)
{
    if (packet.body_len != sizeof(HeartbeatPayload)) {
        return false;
    }

    std::memcpy(&payload, packet.data, sizeof(payload));
    return true;
}
