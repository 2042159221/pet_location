#include "App_LowPower.h"

 void App_EnterLowPower(void){

    //GPS进入低功耗
    Int_AT6558R_EnterLowPower();
    //QS100进入低功耗
    Int_QS100_EnterLowPower();

    //计步模块不进入低功耗

    //设置RTC 闹钟，在待机之前设置
    //RTC ：BIM BCD  34 BIN:0010,0010 BCD:0011,0100

    //获取当前时间
    RTC_TimeTypeDef currentTime ;
    HAL_RTC_GetTime(&hrtc,&currentTime,RTC_FORMAT_BIN);

    RTC_AlarmTypeDef alarm;
    alarm.AlarmTime.Hours = currentTime.Hours;
    alarm.AlarmTime.Minutes = currentTime.Minutes;
    alarm.AlarmTime.Seconds = currentTime.Seconds + 10;
    HAL_RTC_SetAlarm(&hrtc,&alarm,RTC_FORMAT_BIN);

    //判断当前主控STM32C8T6是不是待机模式
    if(__HAL_PWR_GET_FLAG(PWR_FLAG_SB)){
        //手动清除标志位
        __HAL_PWR_CLEAR_FLAG(PWR_FLAG_SB);
    }

    if(__HAL_PWR_CLEAR_FLAG(PWR_FLAG_WU)){

        //手动清楚标志
        __HAL_PWR_CLEAR_FLAG(PWR_FLAG_WU);
    }

    //主机进入低功耗-待机模式
    HAL_PWR_EnterSTANDBYMode();
 }

 //退出低功耗
 void App_LeaveLowPower(void){
    Int_QS100_LeaveLowPower();
    Int_AT6558R_LeaveLowPower();

 }

 