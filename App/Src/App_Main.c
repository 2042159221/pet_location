#include "App_Main.h"

// 存放gps数据相关
static uint8_t gps_Info[GPS_BUFFER_SIZE];
static uint16_t gps_Info_Len;
static Status_For_Get status_For_Get;

void App_Init(void)
{
    // 1. GPS初始化
    Int_AT6558R_Init();

    // 2. QS100初始化
    Int_QS100_Init();

    // 3.DS3553初始化
    Int_DS3553_Init();

    // 4.lora 初始化
}

/**
* @description: 获取gps信息 并上报
* $GNGGA,070822.000,4006.81888,N,11621.89413,E,1,05,25.5,30.2,M,-9.6,M,,*58
  $GNGLL,4006.81888,N,11621.89413,E,070822.000,A,A*49
  $GNGSA,A,3,05,12,25,,,,,,,,,,25.5,25.5,25.5,1*02
   $GNGSA,A,3,10,13,,,,,,,,,,,25.5,25.5,25.5,4*05
   $GPGSV,3,1,11,05,54,263,24,06,32,098,,09,20,043,,11,68,061,,0*61
   $GPGSV,3,2,11,12,14,237,17,13,23,176,,15,05,204,,19,08,152,,0*60
   $GPGSV,3,3,11,20,75,327,,25,13,265,23,29,28,314,,0*51
   $BDGSV,2,1,05,08,64,301,,10,47,216,15,13,56,283,10,27,59,133,,0*78
   $BDGSV,2,2,05,38,72,326,,0*48
   $GNRMC,070822.000,A,4006.81888,N,11621.89413,E,0.81,359.02,020624,,,A,V*02
   $GNVTG,359.02,T,,M,0.81,N,1.51,K,A*22
   $GNZDA,070822.000,02,06,2024,00,00*47
   $GPTXT,01,01,01,ANTENNA OPEN*25
*/
void App_CollectAndUploadData(void)
{
    // 1.先整理对应GPS、步数相应的数据
    // 持续获取GPS数据，直到获取到有效数据以后，整理完毕，退出循环

    // 定义指针接收gps 的 信息
    char *gnrmc = NULL;
    while (1)
    {
        status_For_Get = Int_AT6558R_GetGPSData(gps_Info, GPS_BUFFER_SIZE, &gps_Info_Len);
        if (!status_For_Get)
        {
            // 真实数据获取
            // gnrmc = strstr((char *)gps_Info, "$GNRMC");
            // 假数据,开发调试阶段使用
            gnrmc = "$GNRMC,095200.993,A,3028.09020,N,11423.28576,E,0.24,0.00,270225,,,E,V*0C";

            // 检测数据有效性
            char flag = '\0';
            // 取第一个A or V
            sscanf(gnrmc, "%*[^AV]%c", &flag);

            if (flag == 'A')
            {
                COM_DEBUG_LN("读取到有效数据: %s", gnrmc);
                break;
            }
            else if (flag == 'V')
            {
                COM_DEBUG_LN("无效数据： %s", gnrmc);
                break;
            }
            else
            {
                COM_DEBUG_LN("未知数据： %s", gnrmc);
                break;
            }
        }
    }
    // 解析
    uint8_t dtime[7];
    uint8_t date[7];
    sscanf(
        gnrmc,
        //$GNRMC,095200.993,A,3028.09020,N,11423.28576,E,0.24,0.00,270225,,,E,V*0C
        "$GNRMC,%6c%*7c%f,%c,%f,%c,%f,%*f,%6c",
        dtime,
        &g_upload_data.latitude,
        g_upload_data.latitude_direction,
        &g_upload_data.longitude,
        g_upload_data.longitude_direction,
        &g_upload_data.speed,
        date);

    // 更改经纬度格式
    g_upload_data.latitude = (int)(g_upload_data.latitude / 100) +
                             (g_upload_data.latitude - (int)(g_upload_data.latitude / 100) * 100) / 60;
    g_upload_data.longitude = (int)(g_upload_data.longitude / 100) +
                              (g_upload_data.longitude - (int)(g_upload_data.longitude / 100) * 100) / 60;

    // 时间处理
    sprintf(g_upload_data.time, "20%c%c-%c%c-%c%c %c%c:%c%c:%c%c",
            date[4],
            date[5],
            date[2],
            date[3],
            date[0],
            date[1],
            dtime[0],
            dtime[1],
            dtime[2],
            dtime[3],
            dtime[4],
            dtime[5]);
    COM_DEBUG_LN("%s", g_upload_data.time);

    Com_Data_utc2BJ(g_upload_data.time, g_upload_data.time);
    COM_DEBUG_LN("%s", g_upload_data.time);

    // 获取步数
    g_upload_data.step_count = Int_DS3553_GetStepCount();

    COM_DEBUG_LN("GPS信息:");

    COM_DEBUG_LN("经度:%f, 方向:%s",
                 g_upload_data.longitude,
                 g_upload_data.longitude_direction);

    COM_DEBUG_LN("纬度:%f, 方向:%s",
                 g_upload_data.latitude,
                 g_upload_data.latitude_direction);

    COM_DEBUG_LN("对地速度:%f 节",
                 g_upload_data.speed);

    COM_DEBUG_LN("定位时间:%s",
                 g_upload_data.time);

    COM_DEBUG_LN("运动步数:%d",
                 g_upload_data.step_count);
    
    //数据转换为json
    UploadData2JsonString();

    COM_DEBUG_LN("%s",g_upload_data.json_data);

    //上传数据
    Int_QS100_UploadData("8.135.10.183", 38975, strlen(g_upload_data.json_data), (const uint8_t *)g_upload_data.json_data);
}
