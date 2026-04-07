#ifndef INT_QS100_H
#define INT_QS100_H

#include "gpio.h"
#include "usart.h"
#include "Com_Delay.h"
#include "string.h"
#include "Com_Debug.h"

//定义枚举类型，表示QS100的网络状态
typedef enum
{
    QS100_NETWORK_CONNECTED= 0,     // 网络已连接
    QS100_NETWORK_ERROR,      // 网络连接错误
    QS100_NETWORK_TIMEOUT           // 网络连接超时
} QS100_NetworkStatus;

/**
 * @file Int_QS100.h
 * @brief QS100 模块接口层头文件
 *
 * 当前模块承担的职责比较基础，主要包括：
 * 1. 控制 QS100 的唤醒引脚；
 * 2. 通过 USART3 发送基础 AT 指令；
 * 3. 配合串口空闲中断接收 QS100 返回的变长数据；
 * 4. 将串口回调里拿到的一批数据交给 QS100 接口层处理。
 */

// 初始化 QS100 模块。
// 当前初始化流程包括：
// 1. 通过 WKUP 引脚唤醒模块；
// 2. 发送一条基础 AT 指令验证链路；
// 3. 打开 USART3 的不定长接收，等待后续回包。
void Int_QS100_Init(void);

// 对 QS100 输出一次唤醒脉冲。
// 该函数通过控制 WKUP 引脚的高低电平变化，让模块进入可通信状态。
void Int_QS100_WakeUp(void);

//封装风法查询QS100联网成功否
QS100_NetworkStatus Int_QS100_CheckNetworkStatus(void);


//封装一个方法，创建SOCKET通信通道
QS100_NetworkStatus Int_QS100_CreateSocket(void);


//封装链接远程服务器的方法，参数是服务器IP和端口号
QS100_NetworkStatus Int_QS100_ConnectServer(const char *ip, uint16_t port);

// QS100 提供的串口接收回调处理函数。
// 该函数在 USART3 的空闲中断回调中被调用，用于处理本次收到的一批数据。
// 需要注意：一次 receive_size 只代表本次空闲中断前收到的数据长度，
// 并不天然等于一条完整的 AT 响应长度。
void Int_QS100_ReceiveCallBack(uint16_t receive_size);

#endif /* INT_QS100_H */
