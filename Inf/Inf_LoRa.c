#include "Inf_LoRa.h"
#include "stdio.h"
#include "spi.h"
#include "Com_Debug.h"

void Inf_LoRa_Init(void){

    MX_SPI1_Init();
    Ebyte_RF.Init();
}

void Inf_LoRa_SenData(uint8_t data[],uint8_t len)
{
    Ebyte_RF.Send(data,len,0);
}

void Inf_LoRa_ReadData(void){
    Ebyte_RF.StartPollTask();
}

void LoRa_ReceiveSuccessCallback(uint8e_t *buffer, uint8e_t length)
{
    COM_DEBUG_LN("收到数据: 长度=%d, 内容=%.*s\r\n", length, length, buffer);
}

void LoRa_TransmitSuccessCallback(void)
{
    COM_DEBUG_LN("发送成功....\r\n");
}