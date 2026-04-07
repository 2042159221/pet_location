#ifndef INT_DS3553_H
#define INT_DS3553_H
#include "i2c.h"
#include "gpio.h"
#include "Com_Delay.h"


//定义枚举，USER_SET寄存器的某一位数值
typedef enum {
    DS3553_SET = 0 ,
    DS3553_RESET = 1 
} DS3553_USER_SET_T;

//从机地址+读写标识
#define DEVICE_READ_ADDR 0x4F
#define DEVICE_WRITE_ADDR 0x4E

//将DS3553每一个寄存器的地址定义为宏
#define DS3553_CHIP_ID 0x01
#define DS3553_USER_SET 0xC3
#define DS3553_STEP_CNT_L 0xC4
#define DS3553_STEP_CNT_M 0xC5
#define DS3553_STEP_CNT_H 0xC6

//USER_SET寄存器的位定义
#define PEDO_0 (1 << 0) //步数计数使能位
#define PEDO_1 (1 << 1) //步数计数模式选择位
#define CLEAR_EN (1 << 2) //步数清零使能位
#define NOISE_DIS (1 << 3) //噪声过滤使能位
#define PULSE_EN (1 << 4) //脉冲计数使能位
#define RAISE_EN (1 << 5) //抬起计数使能位
#define SEND_DIS (1 << 6) //计步灵敏度控制
#define PWR_MOD (1 << 7) //电源使能位


/**
 * @file Int_DS3553.h
 * @brief description
 */

 //初始化DS3553芯片
 void Int_DS3553_Init(void);
// 封装一个函数 使得 DS3553 可以和 MCU 进行I2C通信,读取寄存器数据

uint8_t Int_DS3553_ReadRegister(uint8_t mem_addr);

// 封装一个函数用于获取 步数
uint32_t Int_DS3553_GetStepCount(void);


#endif /* INT_DS3553_H */
