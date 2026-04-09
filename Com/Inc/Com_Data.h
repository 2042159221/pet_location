#include <cstdint>
#ifndef COM_DATA_H
#define COM_DATA_H

/**
 * @file Com_Data.h
 * @brief 处理数据相关的函数声明和类型定义。
 */

 //定义enumerate,用于存储上报数据
 typedef struct 
 {
    //北京时间
    char time[20];
    //纬度 度
    float latitude;
    //纬度方向-JSON数据格式当中没有字符概念
    char latitude_direction[2];
    //经度 度
    float longitude;
    //经度方向-JSON数据格式当中没有字符概念
    char longitude_direction[2];
    //速度
    float speed;
    //步数
    uint32_t step_count;

    //定义一个数组，存储最终要上传的数据，格式为JSON字符串
    //cubeMX 要配置堆扩容
    char json_data[512];

 } Upload_Data_T;
#endif /* COM_DATA_H */
