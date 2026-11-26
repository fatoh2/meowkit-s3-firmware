#pragma once
#include "fake_state.h"
inline struct SerialType {
 template<class... A> void printf(const char*,A...){if(msc_fake::callbackThread)++msc_fake::callbackLogs;}
 void println(const char*){if(msc_fake::callbackThread)++msc_fake::callbackLogs;}
} Serial;

inline uint32_t micros(){return 0;}
