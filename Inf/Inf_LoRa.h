#ifndef INF_LORA_H
#define INF_LORA_H

/**
 * @file Inf_LoRa.h
 * @brief Lora接口
 */
#include "ebyte_core.h"
#include "main.h"

//初始化
void Inf_LoRa_Init(void);

void Inf_LoRa_SenData(uint8_t data[],uint8_t len);

void Inf_LoRa_ReadData(void);


#endif /* INF_LORA_H */