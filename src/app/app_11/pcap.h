#pragma once
#include <cstddef>
#include <cstdint>

namespace meowgotchi::pcap {
constexpr uint32_t SnapLength = 2304;
inline void le32(uint8_t* out, uint32_t n) {
    for (unsigned i=0;i<4;++i) out[i]=uint8_t(n>>(8*i));
}
template<class Sink> bool start(Sink& sink) {
    uint8_t h[24]{};
    le32(h,0xa1b2c3d4); h[4]=2; h[6]=4;
    le32(h+16,SnapLength); le32(h+20,105); // raw IEEE802.11, without FCS
    return sink.write(h,sizeof(h))==sizeof(h);
}
template<class Sink> bool packet(Sink& sink, const uint8_t* data, size_t len, uint64_t micros) {
    if (!data || !len || len>SnapLength) return false;
    uint8_t h[16];
    le32(h,uint32_t(micros/1000000)); le32(h+4,uint32_t(micros%1000000));
    le32(h+8,uint32_t(len)); le32(h+12,uint32_t(len));
    return sink.write(h,sizeof(h))==sizeof(h) && sink.write(data,len)==len;
}
} // namespace meowgotchi::pcap
