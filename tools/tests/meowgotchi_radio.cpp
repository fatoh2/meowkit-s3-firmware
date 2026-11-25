#include "fakes/platform.h"
#include "../../src/app/app_11/wifi_hunter.h"
#include "../../src/ui/ui_wifi_bridge.h"
#include <cassert>
#include <filesystem>
#include <csignal>
#include <sys/resource.h>

extern "C" int __wrap_ieee80211_raw_frame_sanity_check(int32_t,const uint8_t*,int32_t,bool);
extern "C" int ieee80211_raw_frame_sanity_check(int32_t,const uint8_t*,int32_t,bool) {
    ++fake::sdkChecks; return ESP_ERR_INVALID_ARG;
}
int esp_wifi_80211_tx(int iface,const uint8_t* frame,int len,bool sequence) {
    ++fake::sends; return __wrap_ieee80211_raw_frame_sanity_check(iface,frame,len,sequence);
}
extern "C" bool ui_wifi_bridge_radio_busy() { return fake::busy; }
extern "C" void ui_wifi_bridge_disconnect() { fake::connected=false; }
extern "C" wifi_bridge_status_t ui_wifi_bridge_get_status() { return fake::connected?WIFI_BRIDGE_CONNECTED:WIFI_BRIDGE_IDLE; }
extern "C" bool ui_wifi_bridge_resume_station() { ++fake::connects; fake::connected=true; return true; }

static wifi_promiscuous_pkt_t beacon() {
    wifi_promiscuous_pkt_t p;
    p.rx_ctrl.sig_len=43; // 36-byte header + SSID IE + one character + FCS
    p.payload[0]=0x80; p.payload[16]=2; p.payload[21]=5;
    p.payload[36]=0; p.payload[37]=1; p.payload[38]='x'; return p;
}
int main() {
    using meowgotchi::WifiHunter;
    char directory[]="/tmp/meowgotchi-radio-XXXXXX";
    assert(mkdtemp(directory));
    WifiHunter hunter;
    hunter.begin(false);
    assert(!hunter.running() && !hunter.capturing() && !hunter.aggressive());
    assert(!hunter.setCapture(true) && hunter.error()[0]);
    fake::busy=true; assert(!hunter.resume() && !fake::promiscuous); fake::busy=false;
    fake::mode=WIFI_MODE_AP; assert(!hunter.resume() && fake::mode==WIFI_MODE_AP); fake::mode=WIFI_MODE_STA;
    fake::connected=true;
    assert(hunter.resume() && fake::promiscuous && !fake::reconnect && fake::power==WIFI_PS_NONE);
    assert(!hunter.setAggressive(true)); // no selected AP
    auto packet=beacon(); fake::receive(&packet,WIFI_PKT_MGMT); hunter.loop();
    assert(hunter.stats().aps==1 && fake::files.empty());
    hunter.pause();
    assert(!fake::promiscuous && !fake::receive && fake::connected && fake::connects==1);
    assert(fake::reconnect && fake::power==WIFI_PS_MIN_MODEM && fake::filter.filter_mask==32);
    fake::connected=false; fake::channel=6;
    assert(hunter.resume()); fake::clock+=400; hunter.loop(); hunter.pause();
    assert(fake::channel==6); // an idle STA recovers its original channel
    assert(hunter.resume()); fake::stopError=ESP_FAIL; hunter.pause();
    assert(hunter.error()[0]); // failed cleanup remains visible and is retried
    fake::stopError=ESP_OK; hunter.stop(); assert(!fake::promiscuous);
    hunter.begin(true,directory);
    assert(hunter.resume());
    assert(hunter.setCapture(true));
    fake::receive(&packet,WIFI_PKT_MGMT); hunter.loop();
    assert(hunter.stats().saved==0); // buffered records have not yet been synced
    fake::clock+=1000; hunter.loop(); assert(hunter.stats().saved==1);
    hunter.pause(); assert(hunter.capturing());
    assert(hunter.resume()); fake::receive(&packet,WIFI_PKT_MGMT); hunter.loop();
    assert(hunter.stats().saved==1); // second PCAP still buffered
    fake::clock+=1000; hunter.loop(); assert(hunter.stats().saved==2);
    assert(hunter.selectTarget(0) && hunter.setAggressive(true));
    fake::clock+=150; hunter.loop();
    assert(hunter.stats().sent==1 && fake::sends==1 && !fake::sdkChecks);
    assert(__wrap_ieee80211_raw_frame_sanity_check(WIFI_IF_STA,packet.payload,26,false)==ESP_ERR_INVALID_ARG);
    assert(fake::sdkChecks==1); // ordinary caller preserves the real SDK validator
    hunter.pause(); assert(!hunter.aggressive());
    assert(hunter.resume() && !hunter.aggressive());
    // The next exclusive file open fails: exercise actual storage, not a mocked write.
    std::filesystem::rename(directory,std::string(directory)+"-moved");
    FILE* blocked=fopen(directory,"wb"); assert(blocked); fclose(blocked);
    fake::receive(&packet,WIFI_PKT_MGMT); hunter.loop();
    assert(!hunter.capturing() && hunter.stats().dropped==1 && hunter.error()[0]);
    unlink(directory); std::filesystem::rename(std::string(directory)+"-moved",directory);
    // Start a fresh session to re-probe SD availability after the error.
    hunter.stop(); hunter.begin(true,directory); assert(hunter.resume());
    assert(hunter.setCapture(true));
    // Fill the queue without draining: repeated APs need only one advertisement.
    for (unsigned n=0;n<20;++n) { packet.payload[21]=n; fake::receive(&packet,WIFI_PKT_MGMT); }
    hunter.loop(); assert(hunter.stats().dropped==4);
    hunter.stop(); assert(!fake::promiscuous && !fake::receive);
    hunter.begin(true,directory); assert(hunter.resume() && hunter.setCapture(true));
    // Actual VFS buffering failure: a record is accepted by fwrite, then the
    // kernel rejects the flush. It must never enter the synced/saved counter.
    struct rlimit originalLimit; assert(getrlimit(RLIMIT_FSIZE,&originalLimit)==0);
    const auto signalHandler=signal(SIGXFSZ,SIG_IGN);
    struct rlimit smallLimit=originalLimit; smallLimit.rlim_cur=64;
    assert(setrlimit(RLIMIT_FSIZE,&smallLimit)==0);
    fake::receive(&packet,WIFI_PKT_MGMT); hunter.loop();
    assert(hunter.stats().saved==0);
    fake::clock+=1000; hunter.loop();
    assert(!hunter.capturing() && !hunter.sdReady() && hunter.stats().saved==0 && hunter.stats().dropped==1);
    assert(setrlimit(RLIMIT_FSIZE,&originalLimit)==0); signal(SIGXFSZ,signalHandler);
    hunter.stop();
    fake::connected=false; fake::mode=WIFI_MODE_NULL; fake::reconnect=false;
    hunter.begin(false); assert(hunter.resume()); hunter.pause();
    assert(fake::mode==WIFI_MODE_NULL && !fake::connected && !fake::reconnect);
    fake::channelError=ESP_FAIL;
    assert(!hunter.resume() && !fake::promiscuous && !fake::receive && fake::mode==WIFI_MODE_NULL);
    hunter.stop();
    std::filesystem::remove_all(directory);
    puts("MeowGotchi radio tests passed (ownership, restore, PCAP resume, drops, SD failure)");
}
