#include "Int_QS100.h"

// QS100 原始接收缓冲区大小。
// 当前版本采用固定长度缓冲区，配合串口空闲中断接收一批变长数据。
#define INT_QS100_RX_BUFFER_SIZE 128U
#define INT_QS100_FINAL_RESPONSE_SIZE 512U
// USART3 的原始接收缓冲区。
// 每次调用 HAL_UARTEx_ReceiveToIdle_IT 后，串口收到的数据都会先进入这个数组。
static uint8_t qs100_rx_buffer[INT_QS100_RX_BUFFER_SIZE] = {0};

// 预留的 QS100 响应汇总缓冲区。
// 从命名上看，这个变量原本用于保存整合后的响应数据，
// 当前版本中暂未对其做进一步处理，但先保留这块存储空间。
static char qs100_response_data[INT_QS100_FINAL_RESPONSE_SIZE] = {0};

// 记录最近一次接收到的数据长度。
// 该长度表示本次空闲中断触发时，接收缓冲区中有效数据的字节数。
static volatile uint16_t qs100_receive_length = 0U;

//存储最终接收到数据的长度
static volatile uint16_t qs100_final_data_length = 0U;

// 封装一个内部函数，统一通过 USART3 向 QS100 发送 AT 命令。
// 1. 发送一条 AT 指令。
//   2. QS100 的响应可能不是一次性回完，可能分成多批到达。
//   3. 每来一批，HAL_UARTEx_RxEventCallback() 被触发一次。
//   4. 你要把这些批次拼起来。
//   5. 直到收到了完整响应，比如 OK 或 ERROR，才算这一条命令结束。
//   6. 如果超时还没收完，就退出。
static void Int_QS100_SendATCMD(const char *cmd)
{
    //清零上一次的接收数据和长度，准备迎接新的响应。
    memset(qs100_response_data, 0, INT_QS100_FINAL_RESPONSE_SIZE);
    memset(qs100_rx_buffer, 0, INT_QS100_RX_BUFFER_SIZE);
    qs100_final_data_length = 0U;
    qs100_receive_length = 0U;
    HAL_UART_Transmit(&huart3, (uint8_t *)cmd, strlen(cmd), 1000);
    // 需要判断接收是否成功，当前版本暂未实现重试机制或错误处理。
    //循环需要防止一直循环卡死，使用系统滴答定时器，最多等3秒
    uint32_t timeout = HAL_GetTick() + 3000;

    //优化：避免竞态，循环退出条件只保留“超时”
    while (HAL_GetTick() < timeout)
    {
        // 等待接收回调更新 qs100_receive_length。
        // 这里简单地使用轮询方式等待，实际应用中可能需要更复杂的同步机制。
       // 需要注意大部分AT命令得响应，结束会返回"OK" or "ERROR"，可以在回调函数中判断是否接收到了完整的响应，从而决定是否继续等待或重试。
        if (qs100_receive_length == 0U)
        {
            continue;
        }
        else
        {
            // 这里说明本次接收到了数据，qs100_receive_length 已经被更新了。
        //分批内容返回了
        COM_DEBUG_LN("%s", qs100_rx_buffer);
        memcpy(&qs100_response_data[qs100_final_data_length], qs100_rx_buffer, qs100_receive_length);
        qs100_final_data_length += qs100_receive_length;
        //重置本次接收长度和数据，准备下一批数据的接收。
        qs100_receive_length = 0U;
        memset(qs100_rx_buffer, 0, INT_QS100_RX_BUFFER_SIZE);

        if(strstr(qs100_response_data, "OK") != NULL || strstr(qs100_response_data, "ERROR") != NULL)
        {
            //接收到了完整的响应，退出等待循环
            break;
        }
        }
    }
    //接收完成后，打印最终汇总的响应数据和总长度。
    COM_DEBUG_LN("Final received response: %s , Total Length: %d", qs100_response_data, qs100_final_data_length);

}

void Int_QS100_ReceiveCallBack(uint16_t receive_size)
{
    // HAL 的不定长接收回调中，receive_size 表示本次实际收到的数据长度。
    // 为了后续可以直接按字符串方式打印，需要给 '\0' 预留一个位置。
    if (receive_size >= INT_QS100_RX_BUFFER_SIZE)
    {
        receive_size = INT_QS100_RX_BUFFER_SIZE - 1U;
    }

    // 在本次有效数据的末尾补 '\0'，便于作为 C 字符串调试输出。
    qs100_rx_buffer[receive_size] = '\0';

    // 保存本次接收的数据长度，供调试或其他上层逻辑参考。
    qs100_receive_length = receive_size;


    // 不定长接收模式下，一次回调结束后必须重新开启下一次接收。
    // 否则 QS100 后续再发送数据时，MCU 将无法继续接收。
    if (HAL_UARTEx_ReceiveToIdle_IT(&huart3, qs100_rx_buffer, INT_QS100_RX_BUFFER_SIZE) != HAL_OK)
    {
        COM_DEBUG_LN("Restart QS100 receive failed.");
    }
}

void Int_QS100_Init(void)
{
    HAL_StatusTypeDef status = HAL_ERROR;

    // 1. 先通过 WKUP 引脚唤醒 QS100 模块。
    Int_QS100_WakeUp();

    // 2. 打开 USART3 的不定长接收。
    // 当前实现采用循环重试的方式，直到 HAL 返回 HAL_OK 为止。
    while (status != HAL_OK)
    {
        status = HAL_UARTEx_ReceiveToIdle_IT(&huart3, qs100_rx_buffer, INT_QS100_RX_BUFFER_SIZE);
    }

    // 3. 向 QS100 发送一条基础 AT 指令，用于测试当前串口链路是否可用。
    Int_QS100_SendATCMD("AT+RB\r\n");

    

}

void Int_QS100_WakeUp(void)
{
    // QS100 的唤醒时序：
    // 1. 将 WKUP 引脚拉高；
    // 2. 保持一段时间，确保模块能够识别；
    // 3. 再将引脚拉低，形成一次有效唤醒脉冲。
    HAL_GPIO_WritePin(QS100_WKUP_GPIO_Port, QS100_WKUP_Pin, GPIO_PIN_SET);
    Com_Delay_ms(100U);
    HAL_GPIO_WritePin(QS100_WKUP_GPIO_Port, QS100_WKUP_Pin, GPIO_PIN_RESET);
}

QS100_NetworkStatus Int_QS100_CheckNetworkStatus(void)
{
    // 发送 AT 指令查询网络状态,指令是 AT+CGATT?，该指令用于查询模块的  附着状态。
    Int_QS100_SendATCMD("AT+CGATT?\r\n");

    // 解析响应数据，判断网络状态
    if (strstr((char *)qs100_response_data, "OK") != NULL)
    {
        return QS100_NETWORK_CONNECTED;
    }
    else if (strstr((char *)qs100_response_data, "ERROR") != NULL)
    {
        return QS100_NETWORK_ERROR;
    }
    else
    {
        return QS100_NETWORK_TIMEOUT;
    }
}

QS100_NetworkStatus Int_QS100_CreateSocket(void)
{
    //AT+NSOCR=STREAM,6,10005,1\r\n
    Int_QS100_SendATCMD("AT+NSOCR=STREAM,6,0,0\r\n");
    if (strstr((char *)qs100_response_data, "OK") != NULL)
    {
        return QS100_NETWORK_CONNECTED;
    }
    else if (strstr((char *)qs100_response_data, "ERROR") != NULL)
    {
        return QS100_NETWORK_ERROR;
    }
    else
    {
        return QS100_NETWORK_TIMEOUT;
    }
}


QS100_NetworkStatus Int_QS100_ConnectServer(const char *ip, uint16_t port)
{
    //链接远程服务器命令：AT+NSOCO=0,139.224.112.6,10005\r\n
    //AT+NSOCO=0,"<IP_ADDRESS>",10005\r\n
    uint8_t cmd_buffer[64] = {0};
   snprintf((char *)cmd_buffer, sizeof(cmd_buffer),"AT+NSOCO=0,\"%s\",%d\r\n", ip, port);
    Int_QS100_SendATCMD(cmd_buffer);
    if (strstr((char *)qs100_response_data, "OK") != NULL)
    {
        return QS100_NETWORK_CONNECTED;
    }
    else if (strstr((char *)qs100_response_data, "ERROR") != NULL)
    {
        return QS100_NETWORK_ERROR;
    }
    else
    {
        return QS100_NETWORK_TIMEOUT;
    }
}