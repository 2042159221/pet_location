#include "Int_DS3553.h"
#include "Com_Debug.h"

//设置写方法私有,因为用户不需要直接操作寄存器的位，而是通过更高层的函数来控制功能
static void Int_DS3553_WriteRegister(DS3553_USER_SET_T bit_val, uint8_t bit_mask);
static void Int_DS3553_ClearStepCount(void);

void Int_DS3553_Init(void)
{
    // DS3553初始化
    Int_DS3553_WriteRegister(DS3553_RESET,PULSE_EN);

    //开发阶段，需要清零一下
    //Int_DS3553_ClearStepCount();

    COM_DEBUG_LN("AFTER INIT USER SET NUM: %#02x",Int_DS3553_ReadRegister(DS3553_USER_SET));

}
uint8_t Int_DS3553_ReadRegister(uint8_t mem_addr)

{
    uint8_t data[2] = {0};
    // 1. 先让DS3553 的使能拉低
    HAL_GPIO_WritePin(DS3553_CS_GPIO_Port, DS3553_CS_Pin, GPIO_PIN_RESET);
    // 2.开始通信前 需要延迟3毫秒
    Com_Delay_ms(3);
    //3. 开始进行I2C 通信 
    //句柄hi2c1 设备地址 DEVICE_READ_ADDR 寄存器地址 mem_addr 寄存器地址长度 I2C_MEMADD_SIZE_8BIT 数据缓冲区 data 数据长度 size 超时时间 HAL_MAX_DELAY
    HAL_I2C_Mem_Read(&hi2c1, DEVICE_READ_ADDR, mem_addr, I2C_MEMADD_SIZE_8BIT, data, 1, 1000);

    //4. 结束通信后 需要将DS3553 的使能拉高
    HAL_GPIO_WritePin(DS3553_CS_GPIO_Port, DS3553_CS_Pin, GPIO_PIN_SET);
    //5.按照芯片手册，需要演示10毫秒
    Com_Delay_ms(10);

    return data[0];
}

static void Int_DS3553_WriteRegister(DS3553_USER_SET_T bit_val, uint8_t bit_mask)
{
    uint8_t user_set_default = Int_DS3553_ReadRegister(DS3553_USER_SET);

    if (bit_val == DS3553_SET)
    {
        user_set_default |= bit_mask; // 设置指定的位
    }
    else
    {
        user_set_default &= ~bit_mask; // 清除指定的位
    }

    //寄存器需要写入数值，通过I2C 通信写入
    HAL_GPIO_WritePin(DS3553_CS_GPIO_Port, DS3553_CS_Pin, GPIO_PIN_RESET);
    Com_Delay_ms(3);
    HAL_I2C_Mem_Write(&hi2c1, DEVICE_WRITE_ADDR, DS3553_USER_SET, I2C_MEMADD_SIZE_8BIT, &user_set_default, 1, 1000);
    HAL_GPIO_WritePin(DS3553_CS_GPIO_Port, DS3553_CS_Pin, GPIO_PIN_SET);
    Com_Delay_ms(10);
}



uint32_t Int_DS3553_GetStepCount(void){
    uint8_t step_cnt_l = Int_DS3553_ReadRegister(DS3553_STEP_CNT_L);
    uint8_t step_cnt_m = Int_DS3553_ReadRegister(DS3553_STEP_CNT_M);
    uint8_t step_cnt_h = Int_DS3553_ReadRegister(DS3553_STEP_CNT_H);

    uint32_t step_count = 0 ;

    step_count = ((uint32_t)step_cnt_h << 16) | ((uint32_t)step_cnt_m << 8) | step_cnt_l;

    return step_count;
}

static void Int_DS3553_ClearStepCount(void)
{
    Int_DS3553_WriteRegister(DS3553_RESET,CLEAR_EN);
    Com_Delay_ms(10);
    Int_DS3553_WriteRegister(DS3553_SET,CLEAR_EN);

}



