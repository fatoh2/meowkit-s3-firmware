#pragma once
#include <mooncake.h>
#include "../../bsp/devices.h"
#include "wifi_hunter.h"

namespace MOONCAKE::APPS {
class App11 : public mooncake::AppAbility {
public:
    explicit App11(DEVICES* device);
    void onOpen() override;
    void onRunning() override;
    void onClose() override;
private:
    enum class Page : uint8_t { Face, Menu, Networks, Confirm, Info };
    DEVICES* _device;
    meowgotchi::WifiHunter _hunter;
    lgfx::LGFX_Sprite* _canvas=nullptr;
    bool _haveCanvas=false, _armed=false, _touch=false;
    Page _page=Page::Face;
    int _selected=0, _top=0, _touchAction=-1;
    int _touchX=0, _touchY=0;
    uint32_t _lastDraw=0, _lastActivity=0, _lastPair=0, _previousPairs=0;
    unsigned _previousActivity=0;
    bool _dirty=true;
    void page(Page value);
    void input();
    void activate();
    void back();
    void toggleRun();
    int hit(int x,int y) const;
    void present();
    template<class LCD> void render(LCD& lcd);
};
} // namespace MOONCAKE::APPS
