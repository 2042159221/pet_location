#include "Int_AT6558R.h"

#define MAXSIZE 512

// 封装一个函数，用于给 GPS 模块发送命令
static void Int_AT6558R_SendCommand(const char *command);

// 准备一个数组，用于接收 GPS 模块发回来的变长数据
static uint8_t gps_rx_buffer[MAXSIZE] = {0};
// 准备一个数组，用于保存一帧已经接收完成的 GPS 数据
static uint8_t gps_frame_buffer[MAXSIZE] = {0};

// 保存最新一帧 GPS 数据的长度
static volatile uint16_t gps_data_length = 0;

void Int_AT6558R_Init(void)
{
    // AT6558R 初始化
    // 1. 主控芯片通过 PB3 给 GPS 模块供电使能
    HAL_GPIO_WritePin(GPS_EN_GPIO_Port, GPS_EN_Pin, GPIO_PIN_SET);
    // 模块上电后需要一点稳定时间，再发送配置命令
    Com_Delay_ms(3);

    // 2. 清接收现场
    memset(gps_rx_buffer, 0, MAXSIZE);
    memset(gps_frame_buffer, 0, MAXSIZE);
    gps_data_length = 0;

    __HAL_UART_CLEAR_IDLEFLAG(&huart2);
    __HAL_UART_CLEAR_OREFLAG(&huart2);
    __HAL_UART_CLEAR_FEFLAG(&huart2);
    __HAL_UART_CLEAR_NEFLAG(&huart2);

    // 3. GPS 模块通过串口 2 回传数据
    // 项目采用不定长接收，使用空闲中断方式接收一帧 GPS 数据
    if (HAL_UARTEx_ReceiveToIdle_IT(&huart2, gps_rx_buffer, MAXSIZE) != HAL_OK)
    {
        COM_DEBUG_LN("Initial GPS receive start failed. RxState=%u ErrorCode=0x%08lX",
                     huart2.RxState,
                     (unsigned long)huart2.ErrorCode);
        return;
    }
    Com_Delay_ms(1);
    // 2. 主控芯片通过串口 2 给 GPS 模块下达一些命令：
    // 设置波特率、配置工作模式、设置输出频率
    Int_AT6558R_SendCommand("PCAS01,1");
    Com_Delay_ms(1);
    Int_AT6558R_SendCommand("PCAS04,2");
    Com_Delay_ms(1);
    Int_AT6558R_SendCommand("PCAS02,1000");
    Com_Delay_ms(1);
}

static void Int_AT6558R_SendCommand(const char *command)
{
    uint8_t full_command[64] = {0};
    // 计算命令体的校验和，不包含起始符 '$'
    uint8_t check_sum = command[0];

    for (size_t i = 1; command[i] != '\0'; i++)
    {
        check_sum ^= command[i];
    }


    // 拼接完整命令：$ + 命令体 + * + 校验和 + \r\n
    snprintf((char *)full_command, sizeof(full_command), "$%s*%02X\r\n", command, check_sum);


    // 通过串口 2 把完整命令发送给 GPS 模块
    HAL_UART_Transmit(&huart2, full_command, strlen((char *)full_command), 1000);
}

void Int_AT6558R_CallBack(uint16_t Size)
{
    // 这里处理 GPS 模块发回来的数据
    // 为了便于按字符串打印，需要在有效数据末尾补 '\0'
    if (Size >= MAXSIZE)
    {
        Size = MAXSIZE - 1;
    }

    gps_rx_buffer[Size] = '\0';

    // 保存一帧已经接收完成的数据，避免主循环读取时和串口接收冲突
    memcpy(gps_frame_buffer, gps_rx_buffer, Size + 1);
    gps_data_length = Size;

    // 不定长接收模式下，一次接收完成后需要重新开启下一次接收
    if (HAL_UARTEx_ReceiveToIdle_IT(&huart2, gps_rx_buffer, MAXSIZE) != HAL_OK)
    {
        COM_DEBUG_LN("Restart GPS receive failed.");
    }
}

// 此方法用于获取 GPS 模块的数据
Status_For_Get Int_AT6558R_GetGPSData(uint8_t *received_data, uint16_t received_buffer_size, uint16_t *length)
{
    uint16_t copy_length = 0;
    uint16_t data_length = 0;
    uint32_t primask = 0;

    // received_data 要清零，准备接收数据
    if (received_data == NULL || length == NULL || received_buffer_size == 0)
    {
        return GET_INVALID_PARAM;
    }

    *length = 0;

    // 至少要放一个结束符
    if (received_buffer_size < 2)
    {
        received_data[0] = '\0';
        return GET_BUFFER_TOO_SMALL;
    }

    // 将已经接收完成的一帧数据拷贝到用户提供的缓冲区
    // 拷贝时要限制长度，避免用户提供的缓冲区被写越界
    primask = __get_PRIMASK();
    __disable_irq();
    data_length = gps_data_length;

    if (primask == 0U)
    {
        __enable_irq();
    }

    if (data_length == 0U)
    {
        received_data[0] = '\0';
        return GET_NO_DATA;
    }
    /* 限制拷贝长度，预留 '\0' */
    copy_length = data_length;
    if (copy_length >= received_buffer_size)
    {
        copy_length = received_buffer_size - 1;
    }

    memcpy(received_data, gps_frame_buffer, copy_length);
    received_data[copy_length] = '\0';
    *length = copy_length;

    if (data_length >= received_buffer_size)
    {
        return GET_BUFFER_TOO_SMALL;
    }

    return GET_OK;
}

//进入低功耗
void Int_AT6558R_EnterLowPower(void){
    HAL_GPIO_WritePin(GPS_EN_GPIO_Port,GPS_EN_Pin,GPIO_PIN_RESET);

}

//唤醒
void Int_AT6558R_LeaveLowPower(void){
    HAL_GPIO_WritePin(GPS_EN_GPIO_Port,GPS_EN_Pin,GPIO_PIN_SET);
}
