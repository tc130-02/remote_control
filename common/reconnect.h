#pragma once

constexpr int RECONNECT_MAX_DELAY_SECONDS = 8;

inline int reconnectDelaySeconds(int attempt)
{
    if (attempt <= 1) {
        return 1;
    }

    if (attempt >= 4) {
        return RECONNECT_MAX_DELAY_SECONDS;
    }

    return 1 << (attempt - 1);
}
