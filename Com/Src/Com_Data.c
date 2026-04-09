#include "Com_Data.h"

Upload_Data_T g_upload_data;

//$GNRMC,095200.993,A,3028.09020,N,11423.28576,E,0.24,0.00,270225,,,E,V*0C
void Com_Data_utc2BJ(char *utc, char beijing[])
{
    // 1.先转时间按戳
    // hhmmss.sss:095200.993
    // 定义结构体存储解析后的时间
    struct tm utcTm = {0};
    // 解析并存储
    sscanf(utc, "%d-%d-%d %d:%d:%d",
           &utcTm.tm_year,
           &utcTm.tm_mon,
           &utcTm.tm_mday,
           &utcTm.tm_hour,
           &utcTm.tm_min,
           &utcTm.tm_sec);
    //year字段从1900开始计数，需要减去1900
    utcTm.tm_year -=1900;
    //tm_mon从0开始计数，所以要减去1
    utcTm.tm_mon -=1;

    //utcTm 结构体转换为时间戳，单位秒
    time_t _utc = mktime(&utcTm);

    //北京时间 东八区，utc+8,单位 秒
    time_t _beijing = _utc + 8 * 3600;

    //北京时间戳2本地时间结构体
    struct  tm *beijingTm = localtime(&_beijing);
    
    sprintf(beijing,
        "%04d-%02d-%02d %02d:%02d:%02d",
        beijingTm->tm_year + 1900,
        beijingTm->tm_mon +1 ,
        beijingTm->tm_mday,
        beijingTm->tm_hour,
        beijingTm->tm_min,
        beijingTm->tm_sec
    );

}
