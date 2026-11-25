#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <SD_MMC.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include "capture_file.h"

namespace meowgotchi {
struct HunterStats {
    uint16_t aps=0, stations=0;
    uint32_t eapol=0, pairs=0, disconnects=0, sent=0, txErrors=0, saved=0, savedKeys=0, dropped=0;
    uint8_t channel=1;
};
struct AccessPoint {
    uint8_t bssid[6]{};
    char ssid[33]{};
    uint8_t channel=1;
    int8_t rssi=0;
};
class WifiHunter {
public:
    void begin(bool sdReady, const char* captureDirectory="/sdcard/handshakes");
    void stop();
    bool resume();
    void pause();
    void loop();
    bool running() const { return _running; }
    bool capturing() const { return _capture; }
    bool aggressive() const { return _aggressive; }
    bool sdReady() const { return _sdReady; }
    bool setCapture(bool enabled);
    bool setAggressive(bool enabled);
    bool selectTarget(unsigned index);
    bool accessPoint(unsigned index, AccessPoint& out) const;
    int targetIndex() const { return _target; }
    const char* error() const { return _error; }
    const char* path() const { return _path; }
    const HunterStats& stats() const { return _stats; }
    uint32_t uptime() const;
private:
    bool _running=false, _ownsRadio=false, _capture=false, _aggressive=false, _sdReady=false;
    bool _reconnect=false, _autoReconnect=false;
    wifi_mode_t _savedMode=WIFI_MODE_NULL;
    wifi_ps_type_t _savedPower=WIFI_PS_MIN_MODEM;
    wifi_promiscuous_filter_t _savedFilter{};
    bool _haveFilter=false;
    uint8_t _savedChannel=1;
    wifi_second_chan_t _savedSecondary=WIFI_SECOND_CHAN_NONE;
    bool _haveChannel=false;
    uint8_t _first=1, _last=11;
    int _target=-1;
    QueueHandle_t _queue=nullptr;
    CaptureFile _file;
    HunterStats _stats;
    uint32_t _lastHop=0, _lastTx=0, _runSince=0, _elapsed=0, _lastFlush=0;
    char _error[72]{}, _path[64]{};
    char _captureDirectory[128]{};
    uint32_t _pendingFrames=0, _pendingKeys=0;
    void snapshot();
    void drain(unsigned limit=16);
    bool openCapture();
    void closeCapture();
    bool syncCapture();
    void captureFailed();
    bool restoreRadio();
    void sendDeauth();
    bool fail(const char* action, esp_err_t code);
};
} // namespace meowgotchi
