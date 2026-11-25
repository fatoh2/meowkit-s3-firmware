#pragma once
// Deterministic single-task hardware boundary for native WifiHunter tests.
// Production firmware never includes this directory.
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <deque>
#include <map>
#include <memory>
#include <string>
#include <vector>

using esp_err_t=int;
constexpr int ESP_OK=0, ESP_FAIL=-1, ESP_ERR_INVALID_STATE=1, ESP_ERR_INVALID_ARG=2,
    ESP_ERR_NO_MEM=3, ESP_ERR_NOT_FOUND=4, WIFI_SCAN_RUNNING=-1;
inline const char* esp_err_to_name(int) { return "test error"; }
enum wifi_mode_t { WIFI_MODE_NULL=0, WIFI_STA=1, WIFI_MODE_STA=1, WIFI_MODE_AP=2 };
enum wifi_ps_type_t { WIFI_PS_NONE, WIFI_PS_MIN_MODEM };
enum wifi_promiscuous_pkt_type_t { WIFI_PKT_MGMT, WIFI_PKT_DATA, WIFI_PKT_CTRL };
constexpr int WIFI_IF_STA=0, WIFI_SECOND_CHAN_NONE=0;
using wifi_second_chan_t=int;
constexpr unsigned WIFI_PROMIS_FILTER_MASK_MGMT=1, WIFI_PROMIS_FILTER_MASK_DATA=2;
struct wifi_promiscuous_filter_t { unsigned filter_mask=0; };
struct wifi_country_t { uint8_t schan=1,nchan=11; };
struct wifi_promiscuous_pkt_t {
    struct { uint16_t sig_len=0; uint8_t rx_state=0,channel=1; int8_t rssi=-42; } rx_ctrl;
    uint8_t payload[2400]{};
};
using RxCallback=void(*)(void*,wifi_promiscuous_pkt_type_t);
namespace fake {
inline uint32_t clock=0, random=0;
inline wifi_mode_t mode=WIFI_MODE_STA;
inline wifi_ps_type_t power=WIFI_PS_MIN_MODEM;
inline wifi_promiscuous_filter_t filter{32};
inline bool connected=false, reconnect=true, promiscuous=false, busy=false, shortWrite=false;
inline unsigned connects=0, sends=0, sdkChecks=0;
inline uint8_t channel=6;
inline esp_err_t channelError=ESP_OK;
inline esp_err_t stopError=ESP_OK;
inline RxCallback receive=nullptr;
inline std::map<std::string,std::shared_ptr<std::vector<uint8_t>>> files;
}
inline uint32_t millis() { return fake::clock; }
inline uint32_t esp_random() { return ++fake::random; }
inline uint64_t esp_timer_get_time() { return uint64_t(fake::clock)*1000; }
inline struct SerialType {
    template<class... A> void printf(const char*,A...) {}
    void println(const char*) {}
} Serial;
inline struct WifiType {
    wifi_mode_t getMode() const { return fake::mode; }
    bool mode(wifi_mode_t m) { fake::mode=m; return true; }
    bool isConnected() const { return fake::connected; }
    int scanComplete() const { return -2; }
    bool getAutoReconnect() const { return fake::reconnect; }
    void setAutoReconnect(bool value) { fake::reconnect=value; }
} WiFi;
inline int esp_wifi_get_promiscuous(bool* p) { *p=fake::promiscuous; return ESP_OK; }
inline int esp_wifi_set_promiscuous(bool p) { if (!p && fake::stopError) return fake::stopError; fake::promiscuous=p; return ESP_OK; }
inline int esp_wifi_get_channel(uint8_t* p,wifi_second_chan_t* s) { *p=fake::channel; *s=0; return ESP_OK; }
inline int esp_wifi_get_promiscuous_filter(wifi_promiscuous_filter_t* p) { *p=fake::filter; return ESP_OK; }
inline int esp_wifi_set_promiscuous_filter(const wifi_promiscuous_filter_t* p) { fake::filter=*p; return ESP_OK; }
inline int esp_wifi_get_ps(wifi_ps_type_t* p) { *p=fake::power; return ESP_OK; }
inline int esp_wifi_set_ps(wifi_ps_type_t p) { fake::power=p; return ESP_OK; }
inline int esp_wifi_get_country(wifi_country_t* p) { *p={1,11}; return ESP_OK; }
inline int esp_wifi_set_channel(uint8_t p,int) { if (fake::channelError) return fake::channelError; fake::channel=p; return ESP_OK; }
inline int esp_wifi_set_promiscuous_rx_cb(RxCallback p) { fake::receive=p; return ESP_OK; }
int esp_wifi_80211_tx(int,const uint8_t*,int,bool);
using portMUX_TYPE=int;
#define portMUX_INITIALIZER_UNLOCKED 0
inline void portENTER_CRITICAL(portMUX_TYPE*) {}
inline void portEXIT_CRITICAL(portMUX_TYPE*) {}
using TaskHandle_t=void*;
inline TaskHandle_t xTaskGetCurrentTaskHandle() { return &fake::clock; }
constexpr int pdTRUE=1;
struct TestQueue { unsigned capacity,size; std::deque<std::vector<uint8_t>> values; };
using QueueHandle_t=TestQueue*;
inline QueueHandle_t xQueueCreate(unsigned capacity,unsigned size) { return new TestQueue{capacity,size,{}}; }
inline int xQueueSend(QueueHandle_t q,const void* data,int) {
    if (q->values.size()>=q->capacity) return 0;
    const auto* p=static_cast<const uint8_t*>(data); q->values.emplace_back(p,p+q->size); return pdTRUE;
}
inline int xQueueReceive(QueueHandle_t q,void* data,int) {
    if (q->values.empty()) return 0;
    memcpy(data,q->values.front().data(),q->size); q->values.pop_front(); return pdTRUE;
}
inline void vQueueDelete(QueueHandle_t q) { delete q; }
namespace fs {
class File {
    std::shared_ptr<std::vector<uint8_t>> _bytes;
public:
    File()=default;
    explicit File(std::shared_ptr<std::vector<uint8_t>> bytes) : _bytes(bytes) {}
    explicit operator bool() const { return bool(_bytes); }
    size_t write(const uint8_t* data,size_t n) {
        if (!_bytes || fake::shortWrite) return 0;
        _bytes->insert(_bytes->end(),data,data+n); return n;
    }
    void flush() {}
    void close() { _bytes.reset(); }
};
}
constexpr int CARD_NONE=0, FILE_WRITE=1;
inline struct SdType {
    bool ready=true;
    int cardType() const { return ready?1:CARD_NONE; }
    bool exists(const char* path) const { return fake::files.count(path)!=0; }
    bool mkdir(const char*) { return true; }
    fs::File open(const char* path,int) {
        if (!ready) return {};
        auto bytes=std::make_shared<std::vector<uint8_t>>(); fake::files[path]=bytes; return fs::File(bytes);
    }
} SD_MMC;
