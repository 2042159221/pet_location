#ifndef INT_QS100_H
#define INT_QS100_H

#include "gpio.h"
#include "usart.h"
#include "Com_Delay.h"
#include "string.h"
#include "Com_Debug.h"

// 定义一次 QS100 AT 命令会话的结果类型。
typedef enum
{
    QS100_NETWORK_CONNECTED = 0,      // 收到 OK
    QS100_NETWORK_ERROR,              // 收到 ERROR
    QS100_NETWORK_TIMEOUT,            // 等待超时
    QS100_NETWORK_RX_RESTART_FAILED,  // 接收回调里重启接收失败
    QS100_NETWORK_RESPONSE_OVERFLOW,  // 响应缓冲区溢出
    QS100_NETWORK_CME_ERROR,          // 收到 +CME ERROR
    QS100_NETWORK_UNEXPECTED_RESPONSE // 收到响应但格式不符合预期
} QS100_NetworkStatus;

// 初始化 QS100 模块。
void Int_QS100_Init(void);

// 对 QS100 输出一次唤醒脉冲。
void Int_QS100_WakeUp(void);

// 查询当前网络附着状态。
QS100_NetworkStatus Int_QS100_CheckNetworkStatus(void);

// 创建 SOCKET 通道。
QS100_NetworkStatus Int_QS100_CreateSocket(void);

// 连接远程服务器。
QS100_NetworkStatus Int_QS100_ConnectServer(const char *ip, uint16_t port);

// 向远程服务器发送数据。
QS100_NetworkStatus Int_QS100_SendData2Sever(const uint8_t *data, uint16_t length);

// 将状态码转换成可读字符串，便于串口调试。
const char *Int_QS100_StatusToString(QS100_NetworkStatus status);

// USART3 空闲中断回调里调用的接收处理函数。
void Int_QS100_ReceiveCallBack(uint16_t receive_size);

// 将数据上传到远程服务器。
// 这个函数负责串起“网络就绪 -> socket 就绪 -> 连接服务器 -> 发送数据”整条链路。
QS100_NetworkStatus Int_QS100_UploadData(const char *server,
                                         uint16_t port,
                                         uint16_t length,
                                         const uint8_t *data);


// 进入低功耗

    //发送AT 命令
    //0 表示QS100 进入中断 是由外部中断唤醒

void Int_QS100_EnterLowPower(void);

// 退出低功耗
void Int_QS100_LeaveLowPower(void);


#endif /* INT_QS100_H */
