#ifndef APP_MAIN_H
#define APP_MAIN_H
#include "Com_Data.h"
#include "Int_QS100.h"
#include "Int_AT6558R.h"
#include "Int_DS3553.h"
#include "Inf_LoRa.h"

//接收GPS数据缓冲区 最大容量
#define GPS_BUFFER_SIZE 1024



/**
 * @file App_Main.h
 * @brief 应用层主流程相关的函数声明和类型定义。
 */

 extern Upload_Data_T g_upload_data;

// 初始化应用层的各个模块。
void App_Init(void);

//整理数据，上报数据到服务器
void App_CollectAndUploadData(void);



#endif /* APP_MAIN_H */
