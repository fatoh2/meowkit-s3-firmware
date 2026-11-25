#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace meowgotchi {
// Only the exact app-generated deauthentication frame is admitted by the SDK hook.
inline bool deauthFrame(const uint8_t* f, size_t len, const uint8_t* selected) {
    static const uint8_t broadcast[6] = {255,255,255,255,255,255};
    return f && selected && len==26 && f[0]==0xc0 && f[1]==0 && f[2]==0 && f[3]==0 &&
        !memcmp(f+4,broadcast,6) && !memcmp(f+10,selected,6) && !memcmp(f+16,selected,6) &&
        f[22]==0 && f[23]==0 && f[24]==7 && f[25]==0;
}
} // namespace meowgotchi
