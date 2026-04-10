#ifndef INT_AT6558R_H
#define INT_AT6558R_H

#include "gpio.h"
#include "usart.h"
#include "stdio.h"
#include "Com_Debug.h"
#include "Com_Delay.h"



typedef enum{
    GET_OK = 0,
    GET_NO_DATA,
    GET_INVALID_PARAM,
    GET_BUFFER_TOO_SMALL
}Status_For_Get;



void Int_AT6558R_Init(void);

// AT6558R 提供的函数，在串口空闲中断回调中调用这个函数处理 GPS 数据
void Int_AT6558R_CallBack(uint16_t Size);

// AT6558R 提供的函数，别的模块函数可以获取 GPS 模块的数据
Status_For_Get Int_AT6558R_GetGPSData(uint8_t received_data[], uint16_t received_buffer_size, uint16_t *length);


// 进入低功耗 断电
void Int_AT6558R_EnterLowPower(void);

// 退出低功耗 上电
void Int_AT6558R_LeaveLowPower(void);
#endif /* INT_AT6558R_H */
