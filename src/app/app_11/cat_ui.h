// Cat renderer adapted from FeralDevs/FeralCat at 1cf9fc9dd772b4acb42f9c2e4625722574a588a5.
// See README.md for attribution and the original source.
#pragma once
#include <LovyanGFX.hpp>
#include "../app_common/mk_tui.h"
namespace MeowGotchi {
enum class Mood : uint8_t { Sleep, Bored, Hunt, Excited, Cool, Sad };
template<typename LCD>
static inline void drawCat(LCD& lcd, int cx, int cy, Mood mood, bool blink)
{
    const uint32_t body = MK_PAL::ACCENT;      /* MeowKit green   */
    const uint32_t dark = MK_PAL::ACCENT_DIM;
    const uint32_t blk  = MK_PAL::BLACK;
    const uint32_t wht  = MK_PAL::WHITE;

    /* Ears (triangles) */
    lcd.fillTriangle(cx-40, cy-18, cx-40, cy-52, cx-12, cy-30, body);
    lcd.fillTriangle(cx+40, cy-18, cx+40, cy-52, cx+12, cy-30, body);
    lcd.fillTriangle(cx-34, cy-24, cx-34, cy-44, cx-18, cy-31, dark);
    lcd.fillTriangle(cx+34, cy-24, cx+34, cy-44, cx+18, cy-31, dark);

    /* Head */
    lcd.fillRoundRect(cx-42, cy-24, 84, 66, 22, body);

    /* Whiskers */
    for (int i = -1; i <= 1; i++) {
        lcd.drawLine(cx-24, cy+8 + i*6, cx-56, cy+2 + i*10, dark);
        lcd.drawLine(cx+24, cy+8 + i*6, cx+56, cy+2 + i*10, dark);
    }

    /* Eyes — position */
    const int ex = 17, ey = -2;

    if (blink && mood != Mood::Sleep) {
        lcd.drawFastHLine(cx-ex-8, cy+ey, 16, blk);
        lcd.drawFastHLine(cx+ex-8, cy+ey, 16, blk);
    } else switch (mood) {
        case Mood::Sleep:
            /* gentle closed arcs */
            lcd.drawLine(cx-ex-8, cy+ey, cx-ex, cy+ey+4, blk);
            lcd.drawLine(cx-ex, cy+ey+4, cx-ex+8, cy+ey, blk);
            lcd.drawLine(cx+ex-8, cy+ey, cx+ex, cy+ey+4, blk);
            lcd.drawLine(cx+ex, cy+ey+4, cx+ex+8, cy+ey, blk);
            break;
        case Mood::Excited:
            /* wide sparkly eyes */
            lcd.fillCircle(cx-ex, cy+ey, 8, wht);
            lcd.fillCircle(cx+ex, cy+ey, 8, wht);
            lcd.fillCircle(cx-ex, cy+ey, 4, blk);
            lcd.fillCircle(cx+ex, cy+ey, 4, blk);
            lcd.drawPixel(cx-ex+2, cy+ey-2, wht);
            lcd.drawPixel(cx+ex+2, cy+ey-2, wht);
            break;
        case Mood::Cool:
            /* sunglasses bar */
            lcd.fillRoundRect(cx-ex-11, cy+ey-6, 22, 12, 3, blk);
            lcd.fillRoundRect(cx+ex-11, cy+ey-6, 22, 12, 3, blk);
            lcd.drawFastHLine(cx-6, cy+ey, 12, blk);
            break;
        case Mood::Hunt:
            /* focused narrow eyes */
            lcd.fillRoundRect(cx-ex-7, cy+ey-2, 14, 6, 2, blk);
            lcd.fillRoundRect(cx+ex-7, cy+ey-2, 14, 6, 2, blk);
            break;
        case Mood::Sad:
            lcd.fillCircle(cx-ex, cy+ey+2, 5, blk);
            lcd.fillCircle(cx+ex, cy+ey+2, 5, blk);
            lcd.drawLine(cx-ex-8, cy+ey-6, cx-ex+2, cy+ey-3, dark);
            lcd.drawLine(cx+ex-2, cy+ey-3, cx+ex+8, cy+ey-6, dark);
            break;
        case Mood::Bored:
        default:
            lcd.fillCircle(cx-ex, cy+ey, 5, blk);
            lcd.fillCircle(cx+ex, cy+ey, 5, blk);
            lcd.drawFastHLine(cx-ex-7, cy+ey-6, 14, dark);
            lcd.drawFastHLine(cx+ex-7, cy+ey-6, 14, dark);
            break;
    }

    /* Nose */
    lcd.fillTriangle(cx-4, cy+12, cx+4, cy+12, cx, cy+17, dark);

    /* Mouth — cat "w" */
    if (mood == Mood::Excited) {
        lcd.drawLine(cx, cy+17, cx-8, cy+26, blk);
        lcd.drawLine(cx, cy+17, cx+8, cy+26, blk);
        lcd.drawFastHLine(cx-8, cy+26, 16, blk);   /* open happy mouth */
    } else if (mood == Mood::Sad) {
        lcd.drawLine(cx-7, cy+26, cx, cy+21, blk);
        lcd.drawLine(cx, cy+21, cx+7, cy+26, blk);
    } else {
        lcd.drawLine(cx, cy+17, cx-6, cy+23, blk);
        lcd.drawLine(cx, cy+17, cx+6, cy+23, blk);
    }
}


} // namespace MeowGotchi
