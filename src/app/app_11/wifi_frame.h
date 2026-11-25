#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace meowgotchi {
enum class KeyMessage : uint8_t { None, M1, M2, M3, M4 };
struct FrameInfo {
    const uint8_t* bssid = nullptr;
    const uint8_t* station = nullptr;
    bool advertisement = false;
    bool eapol = false;
    bool disconnect = false;
    const uint8_t* ssid = nullptr;
    uint8_t ssidLength = 0;
    KeyMessage message = KeyMessage::None;
    uint64_t replay = 0;
};
inline bool unicast(const uint8_t* mac) {
    static const uint8_t zero[6]{};
    return !(mac[0] & 1) && memcmp(mac, zero, 6) != 0;
}
// Input excludes the four-byte FCS. Never read a header or IE before checking length.
inline FrameInfo parseFrame(const uint8_t* f, size_t len) {
    FrameInfo out;
    if (!f || len < 24 || (f[0] & 3)) return out;
    const uint8_t type = (f[0] >> 2) & 3, subtype = f[0] >> 4;
    if (type == 0) {
        if ((f[1] & 3) || !unicast(f + 16)) return out;
        if ((subtype == 8 || subtype == 5) && len >= 36) {
            out.bssid = f + 16;
            out.advertisement = true;
            for (size_t p = 36; p + 2 <= len;) {
                const uint8_t tag = f[p], n = f[p + 1]; p += 2;
                if (n > len - p) break;
                if (tag == 0 && n <= 32) { out.ssid = f + p; out.ssidLength = n; break; }
                p += n;
            }
        } else if ((subtype == 12 || subtype == 10) && len >= 26) {
            out.bssid = f + 16; out.disconnect = true;
        }
        return out;
    }
    if (type != 2) return out;
    const uint8_t ds = f[1] & 3;
    if (ds != 1 && ds != 2) return out; // WDS/IBSS is not an infrastructure AP/client link
    out.bssid = f + (ds == 1 ? 4 : 10);
    if (!unicast(out.bssid)) return {};
    const uint8_t* station = f + (ds == 1 ? 10 : 4);
    if (unicast(station) && memcmp(station, out.bssid, 6)) out.station = station;
    size_t h = 24;
    if (subtype & 8) {
        if (len < h + 2 || (f[h] & 0x80)) return out; // QoS A-MSDU requires a different parser
        h += 2;
        if (f[1] & 0x80) h += 4; // HT control
    }
    if ((subtype & 4) || (f[1] & 0x44) || (f[22] & 15)) return out;
    static const uint8_t llc[8] = {0xaa,0xaa,3,0,0,0,0x88,0x8e};
    if (len < h + 12 || memcmp(f + h, llc, 8)) return out;
    const uint8_t* e = f + h + 8;
    const size_t body = (size_t(e[2]) << 8) | e[3];
    if (e[0] < 1 || e[0] > 3 || e[1] != 3 || body < 95 || body > len - h - 12) return out;
    const uint8_t* k = e + 4;
    if (k[0] != 2 && k[0] != 254) return out;
    if (95 + ((size_t(k[93]) << 8) | k[94]) != body) return out;
    out.eapol = true;
    const uint16_t bits = (uint16_t(k[1]) << 8) | k[2];
    if (!(bits & 8) || (bits & 0x0c00)) return out; // group-key, error or request
    bool nonce = false, micBytes = false;
    for (int i = 13; i < 45; ++i) nonce |= k[i] != 0;
    for (int i = 77; i < 93; ++i) micBytes |= k[i] != 0;
    const bool ack = bits & 0x80, mic = bits & 0x100, secure = bits & 0x200;
    if (ds == 2 && ack && !mic && nonce) out.message = KeyMessage::M1;
    else if (ds == 1 && !ack && mic && !secure && nonce && micBytes) out.message = KeyMessage::M2;
    else if (ds == 2 && ack && mic && nonce && micBytes) out.message = KeyMessage::M3;
    else if (ds == 1 && !ack && mic && secure && micBytes) out.message = KeyMessage::M4;
    for (int i = 5; i < 13; ++i) out.replay = (out.replay << 8) | k[i];
    return out;
}

// Counts candidate key pairs, not verified/cracked passwords or full handshakes.
// A retransmission, another client or an unrelated replay counter cannot make a pair.
class PairTracker {
    struct Entry {
        uint8_t ap[6]{}, sta[6]{};
        uint64_t replay[3]{};
        uint8_t seen = 0;
        bool counted = false;
    };
    Entry _entries[32]{};
    size_t _next = 0;
public:
    void reset() { for (auto& e : _entries) e = {}; _next = 0; }
    bool observe(const FrameInfo& f) {
        if (!f.bssid || !f.station || f.message < KeyMessage::M1 || f.message > KeyMessage::M3) return false;
        Entry* entry = nullptr;
        for (auto& e : _entries) if (e.seen && !memcmp(e.ap,f.bssid,6) && !memcmp(e.sta,f.station,6)) { entry=&e; break; }
        if (!entry) {
            entry = &_entries[_next++ % 32]; *entry = {};
            memcpy(entry->ap,f.bssid,6); memcpy(entry->sta,f.station,6);
        }
        const unsigned n = unsigned(f.message) - 1;
        entry->replay[n] = f.replay; entry->seen |= 1 << n;
        const bool pair12 = (entry->seen & 3) == 3 && entry->replay[0] == entry->replay[1];
        const bool pair23 = (entry->seen & 6) == 6 && entry->replay[1] != UINT64_MAX && entry->replay[2] == entry->replay[1]+1;
        if (!entry->counted && (pair12 || pair23)) { entry->counted = true; return true; }
        return false;
    }
};
} // namespace meowgotchi
