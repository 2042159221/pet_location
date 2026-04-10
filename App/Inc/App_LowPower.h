#ifndef APP_LOWPOWER_H
#define APP_LOWPOWER_H

#include "Int_AT6558R.h"
#include "Int_QS100.h"
#include  "rtc.h"
/**
 * @file App_LowPower.h
 * @brief description
 */

 //进入低功耗
 void App_EnterLowPower(void);

 
//退出低功耗
void App_LeaveLowPower(void);

#endif /* APP_LOWPOWER_H */

