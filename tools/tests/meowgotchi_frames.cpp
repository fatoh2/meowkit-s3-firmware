#include "../../src/app/app_11/wifi_frame.h"
#include "../../src/app/app_11/pcap.h"
#include "../../src/app/app_11/raw_tx_guard.h"
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>

using meowgotchi::parseFrame;
static const uint8_t ap[6] = {0x02,1,2,3,4,5};
static const uint8_t sta[6] = {0x04,6,7,8,9,10};
static const uint8_t broadcast[6] = {255,255,255,255,255,255};
static std::vector<uint8_t> frame(uint8_t fc, uint8_t flags, size_t size = 160) {
    std::vector<uint8_t> f(size);
    f[0] = fc; f[1] = flags;
    memcpy(f.data()+4, flags == 2 ? sta : ap, 6);
    memcpy(f.data()+10, flags == 2 ? ap : sta, 6);
    memcpy(f.data()+16, ap, 6);
    return f;
}
static void key(std::vector<uint8_t>& f, size_t h) {
    const uint8_t llc[] = {0xaa,0xaa,3,0,0,0,0x88,0x8e};
    memcpy(f.data()+h, llc, 8);
    f[h+8] = 2; f[h+9] = 3; // EAPOL version, Key
    f[h+10] = 0; f[h+11] = 95; // complete fixed key descriptor
    f[h+12] = 2;
}
int main() {
    auto f = frame(0x80,0,36);
    memcpy(f.data()+4, broadcast, 6); memcpy(f.data()+10, ap, 6);
    auto info = parseFrame(f.data(), f.size());
    assert(info.advertisement && memcmp(info.bssid, ap,6)==0 && !info.station);
    for (size_t n=0; n<36; ++n) assert(!parseFrame(f.data(),n).advertisement);
    f[0]=0x50; assert(parseFrame(f.data(),f.size()).advertisement);
    f[0]=0x84; assert(!parseFrame(f.data(),f.size()).advertisement);
    f=frame(0x08,1); key(f,24);
    info=parseFrame(f.data(),f.size());
    assert(info.eapol && memcmp(info.bssid,ap,6)==0 && memcmp(info.station,sta,6)==0);
    for (size_t n=0;n<24+8+4+95;++n) assert(!parseFrame(f.data(),n).eapol);
    f=frame(0x08,2); key(f,24);
    info=parseFrame(f.data(),f.size());
    assert(info.eapol && memcmp(info.bssid,ap,6)==0 && memcmp(info.station,sta,6)==0);
    f[1]=3; assert(!parseFrame(f.data(),f.size()).bssid); // WDS has no infrastructure BSSID
    f[1]=0; assert(!parseFrame(f.data(),f.size()).bssid); // no-DS is not a station/AP link
    f=frame(0x88,1); key(f,26); assert(parseFrame(f.data(),f.size()).eapol);
    f[1]|=0x80; assert(!parseFrame(f.data(),f.size()).eapol); // missing HT control shifts LLC
    f=frame(0x88,1); f[1]|=0x80; key(f,30); assert(parseFrame(f.data(),f.size()).eapol);
    f=frame(0x88,1); f[24]=0x80; key(f,26); assert(!parseFrame(f.data(),f.size()).eapol); // A-MSDU
    f=frame(0x08,1); key(f,24); f[1]|=0x40; assert(!parseFrame(f.data(),f.size()).eapol);
    f=frame(0x08,1); key(f,24); f[1]|=4; assert(!parseFrame(f.data(),f.size()).eapol);
    f=frame(0x08,1); key(f,24); f[22]=1; assert(!parseFrame(f.data(),f.size()).eapol);
    f=frame(0x48,1); key(f,24); assert(!parseFrame(f.data(),f.size()).eapol); // Null data
    f=frame(0x08,1); key(f,24); f[27]=1; assert(!parseFrame(f.data(),f.size()).eapol); // bad OUI
    f=frame(0x08,1); key(f,24); f[33]=0; assert(!parseFrame(f.data(),f.size()).eapol); // not Key
    f=frame(0x08,1); key(f,24); f[35]=94; assert(!parseFrame(f.data(),f.size()).eapol);
    f=frame(0x08,1); key(f,24); f[129]=20; assert(!parseFrame(f.data(),f.size()).eapol); // impossible key-data length
    f=frame(0x08,1); memcpy(f.data()+10,broadcast,6); assert(!parseFrame(f.data(),f.size()).station);
    f=frame(0x08,1); memcpy(f.data()+10,ap,6); assert(!parseFrame(f.data(),f.size()).station);
    f=frame(0xc0,0,26); assert(parseFrame(f.data(),f.size()).disconnect);
    assert(!parseFrame(f.data(),25).disconnect);
    assert(!parseFrame(nullptr,100).bssid);
    meowgotchi::PairTracker pairs;
    meowgotchi::FrameInfo a; a.bssid=ap; a.station=sta; a.replay=7;
    a.message=meowgotchi::KeyMessage::M1;
    assert(!pairs.observe(a)); assert(!pairs.observe(a)); // repeated M1 is not a handshake
    a.message=meowgotchi::KeyMessage::M2; a.replay=8; assert(!pairs.observe(a));
    a.replay=7; assert(pairs.observe(a)); assert(!pairs.observe(a));
    pairs.reset(); a.message=meowgotchi::KeyMessage::M2; assert(!pairs.observe(a));
    a.message=meowgotchi::KeyMessage::M3; a.replay=8; assert(pairs.observe(a));
    pairs.reset(); a.message=meowgotchi::KeyMessage::M1; a.replay=7; assert(!pairs.observe(a));
    a.message=meowgotchi::KeyMessage::M2; a.station=ap; assert(!pairs.observe(a));
    // Validate the descriptor classification used for pair tracking.
    f=frame(0x08,2); key(f,24); f[37]=0; f[38]=0x88; f[49]=1;
    assert(parseFrame(f.data(),f.size()).message==meowgotchi::KeyMessage::M1);
    f[1]=1; assert(parseFrame(f.data(),f.size()).message==meowgotchi::KeyMessage::None);
    f[37]=1; f[38]=8; f[113]=1;
    assert(parseFrame(f.data(),f.size()).message==meowgotchi::KeyMessage::M2);
    f[1]=2; assert(parseFrame(f.data(),f.size()).message==meowgotchi::KeyMessage::None);
    f[37]=3; f[38]=0xc8;
    assert(parseFrame(f.data(),f.size()).message==meowgotchi::KeyMessage::M3);
    f[1]=1; assert(parseFrame(f.data(),f.size()).message==meowgotchi::KeyMessage::None);
    struct Sink {
        std::vector<uint8_t> bytes;
        bool shortWrite=false;
        size_t write(const uint8_t* p,size_t n) { size_t k=shortWrite?n-1:n; bytes.insert(bytes.end(),p,p+k); return k; }
    } sink;
    assert(meowgotchi::pcap::start(sink));
    assert(sink.bytes.size()==24 && sink.bytes[0]==0xd4 && sink.bytes[20]==105);
    assert(meowgotchi::pcap::packet(sink,f.data(),f.size(),1234567));
    assert(sink.bytes[24]==1 && sink.bytes[28]==0x47 && sink.bytes[29]==0x94 && sink.bytes[30]==3);
    assert(sink.bytes[32]==f.size() && sink.bytes[36]==f.size());
    sink.shortWrite=true;
    assert(!meowgotchi::pcap::start(sink));
    assert(!meowgotchi::pcap::packet(sink,f.data(),f.size(),0));
    assert(!meowgotchi::pcap::packet(sink,nullptr,4,0));
    uint8_t tx[26]={0xc0,0,0,0,255,255,255,255,255,255};
    memcpy(tx+10,ap,6); memcpy(tx+16,ap,6); tx[24]=7;
    assert(meowgotchi::deauthFrame(tx,26,ap));
    assert(!meowgotchi::deauthFrame(tx,25,ap));
    assert(!meowgotchi::deauthFrame(tx,26,sta));
    for (size_t i=0;i<26;++i) { tx[i]^=1; assert(!meowgotchi::deauthFrame(tx,26,ap)); tx[i]^=1; }
    // Every truncation of random input must be safe, including oversized body lengths.
    std::array<uint8_t,256> bytes{};
    uint32_t random=0x12345678;
    for (int sample=0;sample<10000;++sample) {
        for (auto& b:bytes) {random=random*1664525+1013904223; b=random>>24;}
        for (size_t n=0;n<bytes.size();++n) (void)parseFrame(bytes.data(),n);
    }
    puts("MeowGotchi frame tests passed (direction, truncation, QoS, EAPOL, malformed input)");
}
