#include "meowgotchi.h"
#include "cat_ui.h"
#include "../app_common/mk_tui.h"
#include <algorithm>
#include <new>

namespace MOONCAKE::APPS {
using namespace meowgotchi;
App11::App11(DEVICES* device) : _device(device) { setAppInfo().name="MeowGotchi"; }

void App11::onOpen() {
    _hunter.begin(_device->sd.begin());
    _canvas=new (std::nothrow) lgfx::LGFX_Sprite(&_device->Lcd);
    _haveCanvas=false;
    if (_canvas) {
        _canvas->setPsram(true); _canvas->setColorDepth(16);
        _haveCanvas=_canvas->createSprite(320,240)!=nullptr;
    }
    _lastDraw=_lastActivity=_lastPair=_previousPairs=_previousActivity=0;
    _touch=false; page(Page::Face); present();
}
void App11::onClose() {
    _hunter.stop();
    if (_canvas) { _canvas->deleteSprite(); delete _canvas; _canvas=nullptr; }
    _haveCanvas=false;
}
void App11::page(Page value) {
    _page=value; _selected=_top=0; _armed=false; _touchAction=-1; _dirty=true;
}
void App11::toggleRun() {
    if (_hunter.running()) _hunter.pause(); else _hunter.resume();
    _dirty=true;
}
void App11::activate() {
    switch (_page) {
        case Page::Face: toggleRun(); break;
        case Page::Menu:
            switch (_selected) {
                case 0: toggleRun(); break;
                case 1:
                    if (!_hunter.capturing()) _hunter.setStorageReady(_device->sd.begin());
                    _hunter.setCapture(!_hunter.capturing()); break;
                case 2: page(Page::Networks); break;
                case 3:
                    if (_hunter.aggressive()) _hunter.setAggressive(false);
                    else page(Page::Confirm);
                    break;
                case 4: page(Page::Info); break;
            }
            break;
        case Page::Networks:
            if (_hunter.selectTarget(_selected)) page(Page::Menu);
            break;
        case Page::Confirm:
            _hunter.setAggressive(true); page(Page::Menu); break;
        case Page::Info: break;
    }
    _dirty=true;
}
void App11::back() {
    if (_page==Page::Face) page(Page::Menu);
    else if (_page==Page::Menu) page(Page::Face);
    else page(Page::Menu);
}
int App11::hit(int x,int y) const {
    if (x<0 || x>=320 || y<0 || y>=240) return -1;
    if (y>=220) return x<160 ? 100 : 101;
    if (_page==Page::Menu && y>=25 && y<185) return (y-25)/32;
    if (_page==Page::Networks && y>=25 && y<205) {
        const int index=_top+(y-25)/36;
        return index<_hunter.stats().aps ? index : -1;
    }
    return -1;
}
void App11::input() {
    auto& b=_device->button; b.update(); b.tick();
    bool a=b.A.pressed(), backPressed=b.B.pressed();
    const bool up=b.Up.pressed(), down=b.Down.pressed();
    int x=-1,y=-1;
    const bool touched=_device->ctp.readPoint(x,y) && x>=0 && x<320 && y>=0 && y<240;
    if (!_armed) {
        _touch=touched;
        if (!touched && b.A.state()==Button_Class::RELEASED && b.B.state()==Button_Class::RELEASED &&
            b.Up.state()==Button_Class::RELEASED && b.Down.state()==Button_Class::RELEASED &&
            b.Left.state()==Button_Class::RELEASED && b.Right.state()==Button_Class::RELEASED) _armed=true;
        return;
    }
    if (touched && !_touch) { _touchX=x; _touchY=y; _touchAction=hit(x,y); }
    if (touched && _touch && (abs(x-_touchX)>15 || abs(y-_touchY)>15)) _touchAction=-1;
    if (!touched && _touch && _touchAction>=0) {
        if (_touchAction==100) a=true;
        else if (_touchAction==101) backPressed=true;
        else { _selected=_touchAction; a=true; }
        _touchAction=-1;
    }
    _touch=touched;
    if (backPressed) { back(); return; }
    if (_page==Page::Menu || _page==Page::Networks) {
        const int count=_page==Page::Menu ? 5 : _hunter.stats().aps;
        if (count && (up || down)) {
            _selected=std::max(0,std::min(count-1,_selected+(up?-1:1))); _dirty=true;
        }
        if (_selected<_top) _top=_selected;
        if (_selected>=_top+5) _top=_selected-4;
    }
    if (a) activate();
}
void App11::onRunning() {
    input(); _hunter.loop();
    const auto& s=_hunter.stats(); const uint32_t now=millis();
    if (s.pairs>_previousPairs) { _previousPairs=s.pairs; _lastPair=now; }
    if (unsigned(s.aps+s.stations)>_previousActivity) {
        _previousActivity=s.aps+s.stations; _lastActivity=now;
    }
    if (_dirty || now-_lastDraw>=200) present();
    delay(10);
}

template<class LCD> void App11::render(LCD& lcd) {
    const auto& s=_hunter.stats();
    MK_TUI::clearScreen(lcd); lcd.setTextWrap(false,false);
    MK_TUI::drawHeader(lcd,"MeowGotchi");
    auto text=[&](int y,const char* line,uint32_t color=MK_PAL::TEXT_PRI) {
        lcd.setFont(&fonts::efontCN_16); lcd.setTextColor(color,uint32_t(MK_PAL::BLACK));
        lcd.setCursor(8,y); lcd.printf("%.38s",line);
    };
    auto field=[&](int y,const char* name,uint32_t value) {
        char line[48]; snprintf(line,sizeof(line),"%-17s %lu",name,(unsigned long)value); text(y,line);
    };
    if (_page==Page::Face) {
        char line[64]; snprintf(line,sizeof(line),"CH:%u   AP:%u   STA:%u",s.channel,s.aps,s.stations); text(30,line,MK_PAL::TEXT_SEC);
        const auto now=millis();
        auto mood=MeowGotchi::Mood::Sleep;
        if (_hunter.error()[0]) mood=MeowGotchi::Mood::Sad;
        else if (_hunter.aggressive()) mood=MeowGotchi::Mood::Cool;
        else if (_hunter.running()) {
            if (_lastPair && now-_lastPair<5000) mood=MeowGotchi::Mood::Excited;
            else if (_lastActivity && now-_lastActivity<4000) mood=MeowGotchi::Mood::Hunt;
            else mood=MeowGotchi::Mood::Bored;
        }
        MeowGotchi::drawCat(lcd,160,96,mood,now%3200<140);
        text(154,_hunter.error()[0] ? _hunter.error() :
            !_hunter.running() ? "Paused - press A to start" :
            _hunter.aggressive() ? "Active on selected network" :
            (s.aps && !s.eapol) ? "Networks found; waiting for logins" : "Listening to nearby Wi-Fi",MK_PAL::ACCENT);
        snprintf(line,sizeof(line),"EAPOL:%lu  Pairs:%lu",(unsigned long)s.eapol,(unsigned long)s.pairs); text(180,line);
        snprintf(line,sizeof(line),"%lum %02lus   PCAP %s",(unsigned long)(_hunter.uptime()/60),
            (unsigned long)(_hunter.uptime()%60),_hunter.capturing()?"ON":"OFF"); text(201,line,MK_PAL::TEXT_SEC);
        MK_TUI::drawFooter(lcd,_hunter.running()?"Pause":"Start","Menu");
    } else if (_page==Page::Menu) {
        MK_TUI::drawMenuItem(lcd,0,"Listen",_hunter.running()?"PAUSE":"START",_selected==0);
        MK_TUI::drawMenuItem(lcd,1,"PCAP capture",_hunter.capturing()?"ON":"OFF",_selected==1);
        MK_TUI::drawMenuItem(lcd,2,"Target network",_hunter.targetIndex()>=0?"SELECTED":"NONE",_selected==2);
        MK_TUI::drawMenuItem(lcd,3,"Active test",_hunter.aggressive()?"ON":"OFF",_selected==3);
        MK_TUI::drawMenuItem(lcd,4,"Counters / errors","OPEN",_selected==4);
        text(194,_hunter.error()[0]?_hunter.error():"Hold B to exit",_hunter.error()[0]?MK_PAL::ERR:MK_PAL::TEXT_SEC);
        MK_TUI::drawFooter(lcd,"Select","Face");
    } else if (_page==Page::Networks) {
        if (!s.aps) { text(65,"No networks seen yet."); text(90,"Start listening from the menu."); }
        for (int row=0;row<5;++row) {
            AccessPoint ap;
            if (!_hunter.accessPoint(_top+row,ap)) break;
            const int y=25+row*36;
            const bool selected=_selected==_top+row;
            const uint32_t bg=selected?MK_PAL::SEL_BG:MK_PAL::BLACK;
            const uint32_t fg=selected?MK_PAL::SEL_TEXT:MK_PAL::TEXT_PRI;
            lcd.fillRect(0,y,320,36,bg); lcd.setFont(&fonts::efontCN_16); lcd.setTextColor(fg,bg);
            lcd.setCursor(8,y+1); lcd.printf("%.32s",ap.ssid[0]?ap.ssid:"<hidden>");
            lcd.setCursor(8,y+18); lcd.printf("%02X:%02X:%02X:%02X:%02X:%02X CH%u %ddBm",
                ap.bssid[0],ap.bssid[1],ap.bssid[2],ap.bssid[3],ap.bssid[4],ap.bssid[5],ap.channel,ap.rssi);
        }
        MK_TUI::drawFooter(lcd,"Target","Back");
    } else if (_page==Page::Confirm) {
        AccessPoint ap;
        text(42,"Active test sends deauthentication.",MK_PAL::WARN);
        text(67,"Use only on an authorized network.");
        if (_hunter.targetIndex()>=0 && _hunter.accessPoint(_hunter.targetIndex(),ap)) {
            text(100,ap.ssid[0]?ap.ssid:"<hidden>",MK_PAL::ACCENT);
            char line[48]; snprintf(line,sizeof(line),"%02X:%02X:%02X:%02X:%02X:%02X  CH%u",
                ap.bssid[0],ap.bssid[1],ap.bssid[2],ap.bssid[3],ap.bssid[4],ap.bssid[5],ap.channel); text(124,line);
            text(161,"A enables; Pause/exit stops TX.");
        } else { text(108,"Select a target network first.",MK_PAL::WARN); }
        MK_TUI::drawFooter(lcd,"Enable","Cancel");
    } else {
        char line[96];
        snprintf(line,sizeof(line),"EAPOL %lu   Candidate pairs %lu",(unsigned long)s.eapol,(unsigned long)s.pairs); text(30,line);
        snprintf(line,sizeof(line),"PCAP %lu frames / %lu keys",(unsigned long)s.saved,(unsigned long)s.savedKeys); text(51,line);
        snprintf(line,sizeof(line),"Dropped %lu  TX %lu  Errors %lu",(unsigned long)s.dropped,(unsigned long)s.sent,(unsigned long)s.txErrors); text(72,line);
        text(93,_hunter.sdReady()?"SD ready at open; /handshakes/":"SD missing/error; reopen to retry",MK_PAL::TEXT_SEC);
        text(114,_hunter.path()[0]?_hunter.path()+12:"No capture file yet",MK_PAL::TEXT_SEC);
        text(141,"Pairs are candidates, not verified.",MK_PAL::TEXT_SEC);
        const char* error=_hunter.error();
        text(170,error[0]?error:"No error",error[0]?MK_PAL::ERR:MK_PAL::OK);
        if (strlen(error)>38) text(189,error+38,MK_PAL::ERR);
        MK_TUI::drawFooter(lcd,"","Back");
    }
}
void App11::present() {
    if (_haveCanvas) { render(*_canvas); _canvas->pushSprite(&_device->Lcd,0,0); }
    else render(_device->Lcd);
    _dirty=false; _lastDraw=millis();
}
} // namespace MOONCAKE::APPS
