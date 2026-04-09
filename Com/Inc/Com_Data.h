#include <cstdint>
#ifndef COM_DATA_H
#define COM_DATA_H

#include "time.h"
#include "stdio.h"

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

 // 字符串格式化时间: 2021-01-01 00:00:00
/**
 * @brief 将UTC时间字符串转换为北京时间字符串
 * 
 * 该函数接收一个UTC时间格式的字符串，将其转换为时间戳，
 * 然后加上8小时（北京时间比UTC时间快8小时），
 * 最后将得到的时间戳转换为北京时间格式的字符串。
 * 
 * @param utc 指向UTC时间字符串的指针，格式为 "YYYY-MM-DD HH:MM:SS"
 * @param beijing 用于存储转换后的北京时间字符串的字符数组
 */
void Com_Data_utc2BJ(char *utc , char beijing[]);
#endif /* COM_DATA_H */
