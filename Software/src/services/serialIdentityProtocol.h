#pragma once
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <cstdint>

namespace serialIdentity {
// A single bounded parser owns the input stream in every build. Invalid binary
// and oversized lines are discarded in full, never interpreted as a suffix.
class CommandBuffer {
    char line[32]{};
    size_t used = 0;
    bool invalid = false;
public:
    const char* push(int c) {
        if (c < 0) return nullptr;
        if (c == '\n') {
            line[used] = '\0';
            const bool valid = !invalid && used != 0;
            used = 0;
            invalid = false;
            return valid ? line : nullptr;
        }
        if (c == '\r') return nullptr;
        if (c < 32 || c > 126 || used == sizeof(line) - 1) invalid = true;
        if (!invalid) line[used++] = static_cast<char>(c);
        return nullptr;
    }
};

inline bool safeToken(const char* value) {
    if (!value || !*value) return false;
    size_t count = 0;
    for (; *value; ++value) {
        const char c = *value;
        if (++count > 80 || !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '+')) return false;
    }
    return true;
}

struct Info {
    const char* product;
    const char* model;
    const char* version;
    const char* track;
    const char* build;
    const char* deviceId;
    uint32_t targetFlashBytes;
    uint32_t targetPsramBytes;
    uint32_t flashBytes;
    uint32_t psramBytes;
};

inline size_t format(char* output, size_t capacity, const Info& info) {
    if (!safeToken(info.product) || !safeToken(info.model) || !safeToken(info.version) ||
        !safeToken(info.track) || !safeToken(info.build) || !safeToken(info.deviceId)) return 0;
    const int size = snprintf(output, capacity,
        "\n%s %s\nRAD_ID {\"schema\":1,\"product\":\"%s\",\"model\":\"%s\","
        "\"version\":\"%s\",\"track\":\"%s\",\"build\":\"%s\",\"device_id\":\"%s\","
        "\"target_flash_bytes\":%lu,\"target_psram_bytes\":%lu,\"flash_bytes\":%lu,\"psram_bytes\":%lu}\n",
        info.model, info.version, info.product, info.model, info.version, info.track,
        info.build, info.deviceId, static_cast<unsigned long>(info.targetFlashBytes),
        static_cast<unsigned long>(info.targetPsramBytes), static_cast<unsigned long>(info.flashBytes),
        static_cast<unsigned long>(info.psramBytes));
    return size > 0 && static_cast<size_t>(size) < capacity ? static_cast<size_t>(size) : 0;
}
} // namespace serialIdentity
