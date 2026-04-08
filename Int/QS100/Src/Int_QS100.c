#include "Int_QS100.h"
#include <stdio.h>
#include <string.h>

// QS100 当前版本使用的单批接收缓冲区大小。
// 这里的“单批”指的是：USART3 一次空闲中断回调里交上来的那一段数据。
// 这个缓冲区不是最终响应，而是 HAL 当前这一轮接收时直接写入的工作区。
#define INT_QS100_RX_BUFFER_SIZE            128U

// 一次 AT 命令完整响应的汇总缓冲区大小。
// 因为 QS100 的响应可能分多批返回，所以需要把每一批拼接起来，
// 最后统一放到这个总缓冲区里，供后续判断 OK / ERROR / +CME ERROR。
#define INT_QS100_FINAL_RESPONSE_SIZE       512U

// 常规 AT 命令默认等待超时时间。
// 这里主要覆盖普通查询命令，例如 AT、AT+CGATT?、AT+CEREG?、AT+NSOCR 等。
#define INT_QS100_COMMAND_TIMEOUT_MS        3000U

// 是否开启逐条 AT 细粒度日志。
// 0 表示默认只保留阶段性日志和失败诊断，便于日常调试；
// 1 表示打开每条 AT 的响应细节，适合疑难问题排查。
#define INT_QS100_VERBOSE_LOG_ENABLE        0U

// 上电后等待基础 AT 链路可用的超时时间。
// 模块刚被唤醒时，不一定立刻能回 AT，因此这里单独留更长一点的时间。
#define INT_QS100_AT_READY_TIMEOUT_MS       5000U

// 等待网络就绪的总超时时间。
// “网络就绪”在当前实现里指：
// 1. AT+CGATT? 显示已经附着；
// 2. AT+CEREG? 显示已经注册到网络。
#define INT_QS100_NETWORK_READY_TIMEOUT_MS 60000U

// 等待网络就绪时，两轮查询之间的间隔。
// 这里不连续狂刷命令，而是每隔一段时间再轮询一次。
#define INT_QS100_NETWORK_POLL_INTERVAL_MS 1000U

// 查询 socket 发送状态时，两轮轮询之间的间隔。
// NB-IoT 空口时延通常明显高于普通蜂窝数据链路，这里不适合轮询得太猛。
#define INT_QS100_SEND_STATUS_POLL_INTERVAL_MS 1000U

// 等待一条带 sequence 的 socket 数据完成物理层发送确认的总超时时间。
// 当前既不追求极限低延迟，也不希望“命令回 OK 就直接宣布发送成功”。
#define INT_QS100_SEND_STATUS_TIMEOUT_MS 20000U

// QS100 手册里的 TCP 示例使用固定本地端口创建 STREAM socket。
// 这里先按手册示例使用 10005，避免继续使用 listenport=0 带来实现差异。
#define INT_QS100_TCP_LOCAL_PORT          10005U

// 保存模块当前拿到的 IPv4 地址。
// 当前只用于在建 socket 时显式绑定本机 IP，避免模块默认绑定行为不符合预期。
#define INT_QS100_IPV4_ADDR_SIZE           40U

// 无效 socket_id 的占位值。
// 在还没有成功执行 NSOCR 之前，socket_id 统一记成 -1，
// 便于后续连接、发送阶段先判断“socket 是否已经创建”。
#define INT_QS100_SOCKET_INVALID            (-1)

// USART3 当前这一批接收数据的临时缓冲区。
// HAL_UARTEx_ReceiveToIdle_IT 每开启一次接收，后续这一批数据就先进入这里。
// 回调函数里再把这批数据复制到最终响应总缓冲区中。
static uint8_t qs100_rx_buffer[INT_QS100_RX_BUFFER_SIZE] = {0};

// 一次 AT 命令会话的完整响应汇总缓冲区。
// 前台发送一条命令后，后续若响应分多批返回，就依次拼到这里。
// 最后所有状态判断都基于这个总缓冲区，而不是只看某一批原始数据。
static char qs100_response_data[INT_QS100_FINAL_RESPONSE_SIZE] = {0};

// 最近一批接收数据的长度。
// 这个值更多是为了调试辅助，方便知道本次中断到底收到了多少字节。
static volatile uint16_t qs100_receive_length = 0U;

// 当前完整响应已经累计的总长度。
// 每有一批新数据拼接进来，这个长度就会增加。
static volatile uint16_t qs100_final_data_length = 0U;

// 标记当前是否正处于一次 AT 会话中。
// 只有当这个标志为 1 时，回调里收到的数据才会被当成当前命令的响应来拼接。
// 这样可以避免把“命令会话之外”的杂散上报码错误混进当前响应里。
static volatile uint8_t qs100_session_active = 0U;

// 标记当前响应是否已经收完。
// 这里的“收完”并不是 UART 不再有字节，而是已经识别到响应结束条件，
// 比如尾部出现 OK / ERROR，或者响应中出现 +CME ERROR。
static volatile uint8_t qs100_response_done = 0U;

// 标记当前完整响应是否发生缓冲区溢出。
// 如果总响应太长，放不进 INT_QS100_FINAL_RESPONSE_SIZE，就置位该标志。
static volatile uint8_t qs100_response_overflow = 0U;

// 标记在回调中重启下一轮接收是否失败。
// 因为空闲中断模式下，一次回调结束后必须重新开启下一轮接收，
// 否则后续数据就再也收不到了。
static volatile uint8_t qs100_rx_restart_failed = 0U;

// 记录当前已经创建成功的 socket_id。
// 注意：这个值不是硬编码的，而是从 +NSOCR:<socket> 的响应里解析出来。
static int8_t qs100_socket_id = INT_QS100_SOCKET_INVALID;

// 记录当前 socket 是否已经完成过 NSOCO 连接。
// 这样 UploadData 在周期性上报时，就不会每次都重复执行一次连接。
static uint8_t qs100_socket_connected = 0U;

// 缓存最近一次从 CGPADDR 解析出来的 IPv4 地址。
static char qs100_ipv4_addr[INT_QS100_IPV4_ADDR_SIZE] = {0};

// 记录下一个准备使用的 socket sequence。
// QS100 手册规定 sequence 的取值范围是 1~255，这里循环使用，避免长期运行后卡在固定值。
static uint8_t qs100_next_sequence = 1U;

#if INT_QS100_VERBOSE_LOG_ENABLE == 1U
#define INT_QS100_VERBOSE_LN(...) COM_DEBUG_LN(__VA_ARGS__)
#else
#define INT_QS100_VERBOSE_LN(...) do { } while (0)
#endif

// 判断当前完整响应是否以某个后缀结尾。
// 例如我们会用它来判断是否以 "\r\nOK\r\n" 或 "ERROR\r\n" 结尾。
// 这样做比直接在整个字符串里盲目搜索 "OK" 更稳，因为响应中间也可能出现这些字符。
static uint8_t Int_QS100_ResponseEndsWith(const char *suffix)
{
    size_t suffix_length = strlen(suffix);

    if ((size_t)qs100_final_data_length < suffix_length)
    {
        return 0U;
    }

    if (memcmp(&qs100_response_data[qs100_final_data_length - suffix_length],
               suffix,
               suffix_length) == 0)
    {
        return 1U;
    }

    return 0U;
}

// 判断当前完整响应中是否包含某个关键字。
// 这个函数适合查找类似 "+CME ERROR:"、"+CGATT:"、"+CEREG:" 这种片段。
static uint8_t Int_QS100_ResponseContains(const char *token)
{
    if (strstr(qs100_response_data, token) != NULL)
    {
        return 1U;
    }

    return 0U;
}

// 判断当前 CGPADDR 响应里是否已经拿到可用于 AF_INET 的 IPv4 地址。
// 当前上传链路使用的是 NSOCR 默认的 AF_INET，因此这里只把“带点分十进制”的地址当成 ready。
static uint8_t Int_QS100_IsIpv4Ready(void)
{
    const char *addr_ptr = strstr(qs100_response_data, "+CGPADDR:");
    uint16_t ip_length = 0U;

    memset(qs100_ipv4_addr, 0, sizeof(qs100_ipv4_addr));

    if (addr_ptr == NULL)
    {
        return 0U;
    }

    addr_ptr = strchr(addr_ptr, ',');
    if (addr_ptr == NULL)
    {
        return 0U;
    }

    addr_ptr++;
    while (*addr_ptr == ' ' || *addr_ptr == '"')
    {
        addr_ptr++;
    }

    while (*addr_ptr != '\0' &&
           *addr_ptr != '\r' &&
           *addr_ptr != '\n' &&
           *addr_ptr != ',' &&
           *addr_ptr != '"')
    {
        if (((*addr_ptr >= '0') && (*addr_ptr <= '9')) || (*addr_ptr == '.'))
        {
            if (ip_length >= (uint16_t)(sizeof(qs100_ipv4_addr) - 1U))
            {
                memset(qs100_ipv4_addr, 0, sizeof(qs100_ipv4_addr));
                return 0U;
            }

            qs100_ipv4_addr[ip_length++] = *addr_ptr;
        }
        else
        {
            memset(qs100_ipv4_addr, 0, sizeof(qs100_ipv4_addr));
            return 0U;
        }

        addr_ptr++;
    }

    if (ip_length == 0U || strchr(qs100_ipv4_addr, '.') == NULL)
    {
        memset(qs100_ipv4_addr, 0, sizeof(qs100_ipv4_addr));
        return 0U;
    }

    return 1U;
}

// 判断当前 QREGSWT 是否已经处于“禁用 IoT 平台注册”的模式。
// 直连 TCP/UDP 场景下，官方建议把它设成 2，否则可能影响后续 socket 业务。
static uint8_t Int_QS100_IsQregswtDisabled(void)
{
    if (Int_QS100_ResponseContains("+QREGSWT:2") == 1U ||
        Int_QS100_ResponseContains("+QREGSWT: 2") == 1U)
    {
        return 1U;
    }

    return 0U;
}

// 从 AT+SEQUENCE 的响应里解析出发送状态。
// 典型响应格式类似：
// 0,5,1
// OK
// 其中最后一个数字就是该 sequence 当前的发送状态。
static int8_t Int_QS100_ParseSequenceStatus(void)
{
    const char *cursor = qs100_response_data;

    while (cursor != NULL && *cursor != '\0')
    {
        int socket_id = 0;
        int sequence = 0;
        int send_status = 0;

        if (sscanf(cursor, "%d,%d,%d", &socket_id, &sequence, &send_status) == 3)
        {
            if (send_status >= -1 && send_status <= 2)
            {
                return (int8_t)send_status;
            }
        }

        cursor = strchr(cursor, '\n');
        if (cursor != NULL)
        {
            cursor++;
        }
    }

    return -2;
}

// 生成下一次发送使用的 sequence。
// 当 sequence 走到 255 后，再回到 1，避免出现 0 这个“无需确认”的保留值。
static uint8_t Int_QS100_AllocSequence(void)
{
    uint8_t sequence = qs100_next_sequence;

    qs100_next_sequence++;
    if (qs100_next_sequence == 0U)
    {
        qs100_next_sequence = 1U;
    }

    return sequence;
}

// 判断当前 AT 响应是否已经形成“完整结束条件”。
// 当前实现认为以下几种情况可以视为本轮响应结束：
// 1. 以 OK 结尾；
// 2. 以 ERROR 结尾；
// 3. 响应中出现 +CME ERROR。
// 这样前台等待循环就不需要一直傻等到超时，而是能在正确时机提前退出。
static uint8_t Int_QS100_IsResponseDone(void)
{
    if (Int_QS100_ResponseEndsWith("\r\nOK\r\n") == 1U ||
        Int_QS100_ResponseEndsWith("OK\r\n") == 1U)
    {
        return 1U;
    }

    if (Int_QS100_ResponseEndsWith("\r\nERROR\r\n") == 1U ||
        Int_QS100_ResponseEndsWith("ERROR\r\n") == 1U)
    {
        return 1U;
    }

    if (Int_QS100_ResponseContains("\r\n+CME ERROR:") == 1U ||
        Int_QS100_ResponseContains("+CME ERROR:") == 1U)
    {
        return 1U;
    }

    return 0U;
}

// 清理一次 AT 会话相关的接收状态。
// 每次发送新命令之前，都要先把上一次命令留下来的总缓冲、长度和状态位清掉。
// 否则不同命令之间的响应数据会相互污染。
static void Int_QS100_ResetSessionState(void)
{
    qs100_session_active = 0U;
    memset(qs100_response_data, 0, sizeof(qs100_response_data));
    qs100_final_data_length = 0U;
    qs100_receive_length = 0U;
    qs100_response_done = 0U;
    qs100_response_overflow = 0U;
    qs100_rx_restart_failed = 0U;
}

// 根据当前完整响应的结果，给出统一的状态码。
// 这里的优先级大致是：
// 1. 先看接收过程有没有出错，例如溢出、重启失败；
// 2. 再看是不是超时；
// 3. 再看响应内容到底是 OK / ERROR / +CME ERROR；
// 4. 如果都不符合，就归为“响应格式不符合预期”。
static QS100_NetworkStatus Int_QS100_GetResponseStatus(void)
{
    if (qs100_response_overflow == 1U)
    {
        return QS100_NETWORK_RESPONSE_OVERFLOW;
    }

    if (qs100_rx_restart_failed == 1U)
    {
        return QS100_NETWORK_RX_RESTART_FAILED;
    }

    if (qs100_response_done == 0U)
    {
        return QS100_NETWORK_TIMEOUT;
    }

    if (Int_QS100_ResponseContains("\r\n+CME ERROR:") == 1U ||
        Int_QS100_ResponseContains("+CME ERROR:") == 1U)
    {
        return QS100_NETWORK_CME_ERROR;
    }

    if (Int_QS100_ResponseEndsWith("\r\nERROR\r\n") == 1U ||
        Int_QS100_ResponseEndsWith("ERROR\r\n") == 1U)
    {
        return QS100_NETWORK_ERROR;
    }

    if (Int_QS100_ResponseEndsWith("\r\nOK\r\n") == 1U ||
        Int_QS100_ResponseEndsWith("OK\r\n") == 1U)
    {
        return QS100_NETWORK_CONNECTED;
    }

    return QS100_NETWORK_UNEXPECTED_RESPONSE;
}

// 从 +NSOCR:<socket> 的响应中解析出真实的 socket_id。
// 之所以需要单独做这一步，是因为 socket 不一定永远是 0，
// 如果后续连接、发数继续硬编码用 0，就可能把命令发到错误的 socket 上。
static int8_t Int_QS100_ParseSocketId(void)
{
    const char *socket_ptr = strstr(qs100_response_data, "+NSOCR:");
    int32_t socket_value = 0;

    if (socket_ptr == NULL)
    {
        return INT_QS100_SOCKET_INVALID;
    }

    socket_ptr += strlen("+NSOCR:");
    while (*socket_ptr == ' ')
    {
        socket_ptr++;
    }

    if (*socket_ptr < '0' || *socket_ptr > '9')
    {
        return INT_QS100_SOCKET_INVALID;
    }

    while (*socket_ptr >= '0' && *socket_ptr <= '9')
    {
        socket_value = (socket_value * 10) + (int32_t)(*socket_ptr - '0');
        if (socket_value > 127)
        {
            return INT_QS100_SOCKET_INVALID;
        }
        socket_ptr++;
    }

    return (int8_t)socket_value;
}

// 判断 AT+CGATT? 的结果是否显示“已经附着”。
// 对于当前项目来说，只有已经附着到分组域网络，后续建 socket 才有意义。
static uint8_t Int_QS100_IsAttachReady(void)
{
    if (Int_QS100_ResponseContains("+CGATT:1") == 1U ||
        Int_QS100_ResponseContains("+CGATT: 1") == 1U)
    {
        return 1U;
    }

    return 0U;
}

// 判断 AT+CEREG? 的结果是否显示“已经注册到网络”。
// 通常注册状态为 1 或 5 时，说明已经处于可以进一步联网的状态。
static uint8_t Int_QS100_IsCeregReady(void)
{
    const char *cereg_ptr = strstr(qs100_response_data, "+CEREG:");
    const char *status_ptr = NULL;

    if (cereg_ptr == NULL)
    {
        return 0U;
    }

    cereg_ptr = strchr(cereg_ptr, ':');
    if (cereg_ptr == NULL)
    {
        return 0U;
    }

    cereg_ptr++;
    while (*cereg_ptr == ' ')
    {
        cereg_ptr++;
    }

    // CEREG 的格式可能是：
    // +CEREG:<stat>
    // 或
    // +CEREG:<n>,<stat>
    // 所以这里优先找逗号后的状态位；如果没有逗号，就直接取当前值。
    status_ptr = strchr(cereg_ptr, ',');
    if (status_ptr != NULL)
    {
        status_ptr++;
    }
    else
    {
        status_ptr = cereg_ptr;
    }

    while (*status_ptr == ' ')
    {
        status_ptr++;
    }

    if (*status_ptr == '1' || *status_ptr == '5')
    {
        return 1U;
    }

    return 0U;
}

// 统一发送一条 AT 命令，并在指定超时时间内等待响应结束。
// 这是整个 QS100 层的核心发送函数。
// 它做的事情包括：
// 1. 清理上一次会话状态；
// 2. 发送当前命令；
// 3. 等待回调把分批数据拼成完整响应；
// 4. 判断本轮命令最后属于哪种状态；
// 5. 打印调试日志。
static QS100_NetworkStatus Int_QS100_SendATCMDInternal(const char *cmd, uint32_t timeout_ms)
{
    QS100_NetworkStatus status = QS100_NETWORK_TIMEOUT;
    uint32_t start_tick = HAL_GetTick();

    Int_QS100_ResetSessionState();
    qs100_session_active = 1U;

    HAL_UART_Transmit(&huart3, (uint8_t *)cmd, strlen(cmd), 1000U);

    while ((HAL_GetTick() - start_tick) < timeout_ms)
    {
        if (qs100_response_done == 1U)
        {
            break;
        }

        if (qs100_response_overflow == 1U || qs100_rx_restart_failed == 1U)
        {
            break;
        }
    }

    qs100_session_active = 0U;
    status = Int_QS100_GetResponseStatus();

    if (qs100_response_overflow == 1U)
    {
        COM_DEBUG_LN("QS100 response overflow, total length: %u", qs100_final_data_length);
    }
    else if (qs100_rx_restart_failed == 1U)
    {
        COM_DEBUG_LN("QS100 receive restart failed during response.");
    }
    else if (qs100_response_done == 0U && INT_QS100_VERBOSE_LOG_ENABLE == 1U)
    {
        COM_DEBUG_LN("QS100 wait response timeout, current length: %u", qs100_final_data_length);
    }

    // 常规运行时，不需要把每条 AT 的回包都刷到串口上，否则信息量会明显过大。
    // 因此这里采用“默认静默 + 失败时自动展开 + 需要时可手动打开 verbose”的策略。
    if (INT_QS100_VERBOSE_LOG_ENABLE == 1U ||
        status == QS100_NETWORK_ERROR ||
        status == QS100_NETWORK_CME_ERROR ||
        status == QS100_NETWORK_UNEXPECTED_RESPONSE ||
        status == QS100_NETWORK_RX_RESTART_FAILED ||
        status == QS100_NETWORK_RESPONSE_OVERFLOW)
    {
        COM_DEBUG_LN("Final received response: %s , Total Length: %u",
                     qs100_response_data,
                     qs100_final_data_length);
        COM_DEBUG_LN("QS100 response status: %s (%d)",
                     Int_QS100_StatusToString(status),
                     status);
    }

    return status;
}

// 使用默认超时时间发送一条普通 AT 命令。
// 这是一个轻量封装，避免每次都手动传 INT_QS100_COMMAND_TIMEOUT_MS。
static QS100_NetworkStatus Int_QS100_SendATCMD(const char *cmd)
{
    return Int_QS100_SendATCMDInternal(cmd, INT_QS100_COMMAND_TIMEOUT_MS);
}

// 上电或唤醒后，等待基础 AT 链路可用。
// 为什么要单独做这一步：
// 1. 模块刚被唤醒时，不一定马上就能通信；
// 2. 如果这里都不通，后面查附网、建 socket 都没有意义；
// 3. 先确认 AT 基础链路正常，后续问题更容易定位。
static QS100_NetworkStatus Int_QS100_WaitForAtReady(uint32_t timeout_ms)
{
    uint32_t start_tick = HAL_GetTick();
    QS100_NetworkStatus last_status = QS100_NETWORK_TIMEOUT;

    while ((HAL_GetTick() - start_tick) < timeout_ms)
    {
        last_status = Int_QS100_SendATCMD("AT\r\n");
        if (last_status == QS100_NETWORK_CONNECTED)
        {
            return QS100_NETWORK_CONNECTED;
        }

        // 这两种属于接收链路本身的问题，继续重试意义不大，直接返回。
        if (last_status == QS100_NETWORK_RX_RESTART_FAILED ||
            last_status == QS100_NETWORK_RESPONSE_OVERFLOW)
        {
            return last_status;
        }

        Com_Delay_ms(500U);
    }

    return last_status;
}

// 直接向模块下发 NRB 重启。
// 根据官方手册，AT+NRB 只返回 REBOOTING，不会再有最终 OK，因此这里不走常规 SendATCMD 流程。
static void Int_QS100_SendRebootCommand(void)
{
    const char *cmd = "AT+NRB\r\n";

    Int_QS100_ResetSessionState();
    HAL_UART_Transmit(&huart3, (uint8_t *)cmd, strlen(cmd), 1000U);

    COM_DEBUG_LN("QS100 reboot command sent: AT+NRB");

    // 给模块一点时间进入重启流程，避免立刻发下一个 AT。
    Com_Delay_ms(1500U);

    Int_QS100_ResetSessionState();
}

// 确保模块处于适合直连 socket 的注册模式。
// 官方手册说明：若不使用 IoT platform，应设置 AT+QREGSWT=2，并通过 AT+NRB 重启后生效。
static QS100_NetworkStatus Int_QS100_EnsureSocketRegistrationMode(void)
{
    QS100_NetworkStatus status = Int_QS100_SendATCMD("AT+QREGSWT?\r\n");

    if (status != QS100_NETWORK_CONNECTED)
    {
        COM_DEBUG_LN("QS100 QREGSWT query failed, status=%s (%d)",
                     Int_QS100_StatusToString(status),
                     status);
        return QS100_NETWORK_CONNECTED;
    }

    if (Int_QS100_IsQregswtDisabled() == 1U)
    {
        COM_DEBUG_LN("QS100 QREGSWT already disabled for direct socket mode.");
        return QS100_NETWORK_CONNECTED;
    }

    COM_DEBUG_LN("QS100 QREGSWT is not 2, update it for direct socket mode.");

    status = Int_QS100_SendATCMD("AT+QREGSWT=2\r\n");
    if (status != QS100_NETWORK_CONNECTED)
    {
        COM_DEBUG_LN("QS100 QREGSWT set failed, status=%s (%d)",
                     Int_QS100_StatusToString(status),
                     status);
        return QS100_NETWORK_CONNECTED;
    }

    qs100_socket_id = INT_QS100_SOCKET_INVALID;
    qs100_socket_connected = 0U;

    Int_QS100_SendRebootCommand();

    status = Int_QS100_WaitForAtReady(INT_QS100_AT_READY_TIMEOUT_MS + 8000U);
    COM_DEBUG_LN("QS100 AT ready after NRB status=%d(%s)",
                 status,
                 Int_QS100_StatusToString(status));

    return status;
}

// 在建 socket 前轮询等待网络真正就绪。
// 当前实现要求同时满足：
// 1. CGATT=1，说明已经附着；
// 2. CEREG 为 1 或 5，说明已经注册。
// 只有这两个都满足，才去执行 NSOCR。
static QS100_NetworkStatus Int_QS100_WaitForNetworkReady(uint32_t timeout_ms)
{
    uint32_t start_tick = HAL_GetTick();

    while ((HAL_GetTick() - start_tick) < timeout_ms)
    {
        uint8_t attach_ready = 0U;
        uint8_t cereg_ready = 0U;
        uint8_t ip_ready = 0U;
        QS100_NetworkStatus status = Int_QS100_SendATCMD("AT+CGATT?\r\n");

        if (status == QS100_NETWORK_CONNECTED)
        {
            attach_ready = Int_QS100_IsAttachReady();
        }
        else if (status != QS100_NETWORK_TIMEOUT)
        {
            return status;
        }

        status = Int_QS100_SendATCMD("AT+CEREG?\r\n");
        if (status == QS100_NETWORK_CONNECTED)
        {
            cereg_ready = Int_QS100_IsCeregReady();
        }
        else if (status != QS100_NETWORK_TIMEOUT)
        {
            return status;
        }

        status = Int_QS100_SendATCMD("AT+CGPADDR\r\n");
        if (status == QS100_NETWORK_CONNECTED)
        {
            ip_ready = Int_QS100_IsIpv4Ready();
        }
        else if (status == QS100_NETWORK_RX_RESTART_FAILED ||
                 status == QS100_NETWORK_RESPONSE_OVERFLOW)
        {
            return status;
        }

        INT_QS100_VERBOSE_LN("QS100 network pending: CGATT=%u, CEREG=%u, CGPADDR=%u",
                             attach_ready,
                             cereg_ready,
                             ip_ready);

        if (attach_ready == 1U && cereg_ready == 1U && ip_ready == 1U)
        {
            return QS100_NETWORK_CONNECTED;
        }

        Com_Delay_ms(INT_QS100_NETWORK_POLL_INTERVAL_MS);
    }

    return QS100_NETWORK_TIMEOUT;
}

// 查询指定 sequence 当前的发送状态。
// QS100 手册定义：
// 1. 1 表示发送成功；
// 2. 0 表示发送失败；
// 3. 2 表示发送中；
// 4. -1 表示该 sequence 当前未被使用。
static QS100_NetworkStatus Int_QS100_QuerySendStatus(uint8_t sequence, int8_t *send_status)
{
    char cmd_buffer[32] = {0};
    QS100_NetworkStatus status = QS100_NETWORK_TIMEOUT;
    int8_t parsed_status = -2;

    if (send_status == NULL)
    {
        return QS100_NETWORK_UNEXPECTED_RESPONSE;
    }

    if (qs100_socket_id == INT_QS100_SOCKET_INVALID)
    {
        return QS100_NETWORK_UNEXPECTED_RESPONSE;
    }

    snprintf(cmd_buffer,
             sizeof(cmd_buffer),
             "AT+SEQUENCE=%d,%u\r\n",
             qs100_socket_id,
             sequence);

    status = Int_QS100_SendATCMD(cmd_buffer);
    if (status != QS100_NETWORK_CONNECTED)
    {
        return status;
    }

    parsed_status = Int_QS100_ParseSequenceStatus();
    if (parsed_status < -1 || parsed_status > 2)
    {
        return QS100_NETWORK_UNEXPECTED_RESPONSE;
    }

    *send_status = parsed_status;
    return QS100_NETWORK_CONNECTED;
}

// 轮询等待指定 sequence 的发送状态真正落地。
// 只有当查询结果明确为 1 时，当前实现才把“发送成功”视为成立。
static QS100_NetworkStatus Int_QS100_WaitForSendConfirmed(uint8_t sequence)
{
    uint32_t start_tick = HAL_GetTick();
    int8_t send_status = -2;
    QS100_NetworkStatus status = QS100_NETWORK_TIMEOUT;

    while ((HAL_GetTick() - start_tick) < INT_QS100_SEND_STATUS_TIMEOUT_MS)
    {
        status = Int_QS100_QuerySendStatus(sequence, &send_status);
        if (status != QS100_NETWORK_CONNECTED)
        {
            COM_DEBUG_LN("QS100 query send status failed, status=%s (%d)",
                         Int_QS100_StatusToString(status),
                         status);
            return status;
        }

        if (INT_QS100_VERBOSE_LOG_ENABLE == 1U || send_status != 2)
        {
            COM_DEBUG_LN("QS100 send sequence status: socket=%d, sequence=%u, status=%d",
                         qs100_socket_id,
                         sequence,
                         send_status);
        }

        if (send_status == 1)
        {
            return QS100_NETWORK_CONNECTED;
        }

        if (send_status == 0)
        {
            return QS100_NETWORK_ERROR;
        }

        Com_Delay_ms(INT_QS100_SEND_STATUS_POLL_INTERVAL_MS);
    }

    return QS100_NETWORK_TIMEOUT;
}

// 在 NSOCR 失败时补查几个关键前置条件，缩小“为什么不能建 socket”的范围。
// 这些日志属于保留型故障定位信息，正式版仍可保留，但如果后续串口噪声过大，可再按需裁剪。
static void Int_QS100_LogSocketCreateDiagnostics(void)
{
    QS100_NetworkStatus diag_status = Int_QS100_SendATCMD("AT+QREGSWT?\r\n");
    COM_DEBUG_LN("QS100 socket diag QREGSWT status=%s (%d)",
                 Int_QS100_StatusToString(diag_status),
                 diag_status);

    diag_status = Int_QS100_SendATCMD("AT+CGPADDR\r\n");
    COM_DEBUG_LN("QS100 socket diag CGPADDR status=%s (%d), ipv4_ready=%u",
                 Int_QS100_StatusToString(diag_status),
                 diag_status,
                 (diag_status == QS100_NETWORK_CONNECTED) ? Int_QS100_IsIpv4Ready() : 0U);
    COM_DEBUG_LN("QS100 socket diag cached local IPv4=%s",
                 (qs100_ipv4_addr[0] != '\0') ? qs100_ipv4_addr : "<empty>");

    diag_status = Int_QS100_SendATCMD("AT+CGDCONT?\r\n");
    COM_DEBUG_LN("QS100 socket diag CGDCONT status=%s (%d)",
                 Int_QS100_StatusToString(diag_status),
                 diag_status);

    diag_status = Int_QS100_SendATCMD("AT+CGCONTRDP\r\n");
    COM_DEBUG_LN("QS100 socket diag CGCONTRDP status=%s (%d)",
                 Int_QS100_StatusToString(diag_status),
                 diag_status);
}

// 将内部状态码转换成可读字符串。
// 串口调试时，同时打印数字和字符串，比只看一个数字更直观。
const char *Int_QS100_StatusToString(QS100_NetworkStatus status)
{
    switch (status)
    {
        case QS100_NETWORK_CONNECTED:
            return "OK";
        case QS100_NETWORK_ERROR:
            return "ERROR";
        case QS100_NETWORK_TIMEOUT:
            return "TIMEOUT";
        case QS100_NETWORK_RX_RESTART_FAILED:
            return "RX_RESTART_FAILED";
        case QS100_NETWORK_RESPONSE_OVERFLOW:
            return "RESPONSE_OVERFLOW";
        case QS100_NETWORK_CME_ERROR:
            return "CME_ERROR";
        case QS100_NETWORK_UNEXPECTED_RESPONSE:
            return "UNEXPECTED_RESPONSE";
        default:
            return "UNKNOWN";
    }
}

// USART3 空闲中断回调里调用的接收处理函数。
// 这里的职责是：
// 1. 拿到本批收到的数据长度；
// 2. 在当前命令会话有效时，把这批数据拼到总缓冲区；
// 3. 判断是否已经形成完整响应；
// 4. 重新开启下一次 ReceiveToIdle。
void Int_QS100_ReceiveCallBack(uint16_t receive_size)
{
    qs100_receive_length = receive_size;

    if (receive_size > 0U &&
        qs100_session_active == 1U &&
        qs100_response_done == 0U &&
        qs100_response_overflow == 0U)
    {
        // 总缓冲区始终预留 1 个字节给 '\0'，
        // 这样后续可以直接把响应当作 C 字符串打印和解析。
        uint16_t available_space = (INT_QS100_FINAL_RESPONSE_SIZE - 1U) - qs100_final_data_length;
        uint16_t copy_length = receive_size;

        if (copy_length > available_space)
        {
            copy_length = available_space;
            qs100_response_overflow = 1U;
        }

        if (copy_length > 0U)
        {
            memcpy(&qs100_response_data[qs100_final_data_length], qs100_rx_buffer, copy_length);
            qs100_final_data_length += copy_length;
            qs100_response_data[qs100_final_data_length] = '\0';

            if (Int_QS100_IsResponseDone() == 1U)
            {
                qs100_response_done = 1U;
            }
        }
    }

    // 不定长接收模式下，每次回调结束后都必须重新开启下一轮接收。
    // 否则后面模块再有数据上来，MCU 就接不到了。
    if (HAL_UARTEx_ReceiveToIdle_IT(&huart3, qs100_rx_buffer, INT_QS100_RX_BUFFER_SIZE) != HAL_OK)
    {
        qs100_rx_restart_failed = 1U;
    }
}

// QS100 初始化函数。
// 当前正式版初始化流程的目标不是“尽量少做事”，而是“尽量让模组从可预期的干净状态开始”。
// 之所以最终改成现在这样，是因为前期现场已经验证出：
// 1. MCU 反复下载/复位时，QS100 内部可能残留旧的 socket/网络状态；
// 2. 若直接复用这些历史状态，NSOCR 可能持续报 CME ERROR；
// 3. 强制 NRB 后再重新附网，建链路径明显更稳定。
void Int_QS100_Init(void)
{
    HAL_StatusTypeDef status = HAL_ERROR;
    QS100_NetworkStatus at_status = QS100_NETWORK_TIMEOUT;

    qs100_socket_id = INT_QS100_SOCKET_INVALID;
    qs100_socket_connected = 0U;
    memset(qs100_ipv4_addr, 0, sizeof(qs100_ipv4_addr));

    // 1. 先通过 WKUP 引脚给模块一个唤醒脉冲。
    Int_QS100_WakeUp();
    Com_Delay_ms(500U);

    // 2. 打开 USART3 的不定长接收。
    // 当前实现仍沿用“循环重试直到 HAL_OK”的方式，避免首次启动偶发失败。
    while (status != HAL_OK)
    {
        status = HAL_UARTEx_ReceiveToIdle_IT(&huart3, qs100_rx_buffer, INT_QS100_RX_BUFFER_SIZE);
    }

    // 3. 轮询普通 AT，确认基础链路真的已经可用。
    at_status = Int_QS100_WaitForAtReady(INT_QS100_AT_READY_TIMEOUT_MS);
    COM_DEBUG_LN("QS100 init AT ready status=%d(%s)",
                 at_status,
                 Int_QS100_StatusToString(at_status));

    if (at_status == QS100_NETWORK_CONNECTED)
    {
        // 每次初始化都主动重启一次 QS100，优先清理残留 socket 和历史网络状态。
        // 当前问题已经收敛到 NSOCR 阶段，先保证模组从干净状态开始更有价值。
        Int_QS100_SendRebootCommand();

        at_status = Int_QS100_WaitForAtReady(INT_QS100_AT_READY_TIMEOUT_MS + 8000U);
        COM_DEBUG_LN("QS100 init AT ready after forced NRB status=%d(%s)",
                     at_status,
                     Int_QS100_StatusToString(at_status));
    }

    if (at_status == QS100_NETWORK_CONNECTED)
    {
        at_status = Int_QS100_EnsureSocketRegistrationMode();
        COM_DEBUG_LN("QS100 init socket registration mode status=%d(%s)",
                     at_status,
                     Int_QS100_StatusToString(at_status));
    }
}

// 对 QS100 输出一次唤醒脉冲。
// 当前时序是：
// 1. 将 WKUP 拉高；
// 2. 保持 100ms；
// 3. 再拉低。
// 这一步只是把模块拉到可通信状态，不代表它已经附网成功。
void Int_QS100_WakeUp(void)
{
    HAL_GPIO_WritePin(QS100_WKUP_GPIO_Port, QS100_WKUP_Pin, GPIO_PIN_SET);
    Com_Delay_ms(100U);
    HAL_GPIO_WritePin(QS100_WKUP_GPIO_Port, QS100_WKUP_Pin, GPIO_PIN_RESET);
}

// 查询当前网络附着状态。
// 注意：这个函数只关心 CGATT 是否为 1，
// 并不等价于“整个网络已经完全就绪”。
QS100_NetworkStatus Int_QS100_CheckNetworkStatus(void)
{
    QS100_NetworkStatus status = Int_QS100_SendATCMD("AT+CGATT?\r\n");

    if (status != QS100_NETWORK_CONNECTED)
    {
        return status;
    }

    if (Int_QS100_IsAttachReady() == 1U)
    {
        return QS100_NETWORK_CONNECTED;
    }

    return QS100_NETWORK_UNEXPECTED_RESPONSE;
}

// 创建 SOCKET 通道。
// 当前实现不再像旧版本那样“直接发 NSOCR 就完事”，
// 而是先等待网络附着和注册都就绪，再真正执行 NSOCR。
QS100_NetworkStatus Int_QS100_CreateSocket(void)
{
    QS100_NetworkStatus status = Int_QS100_WaitForNetworkReady(INT_QS100_NETWORK_READY_TIMEOUT_MS);
    int8_t socket_id = INT_QS100_SOCKET_INVALID;
    char cmd_buffer[48] = {0};

    if (status != QS100_NETWORK_CONNECTED)
    {
        return status;
    }

    // 按 QS100 手册使用固定本地端口创建 TCP socket。
    snprintf(cmd_buffer,
             sizeof(cmd_buffer),
             "AT+NSOCR=STREAM,6,%u,1\r\n",
             INT_QS100_TCP_LOCAL_PORT);
    COM_DEBUG_LN("QS100 create TCP socket with local port=%u", INT_QS100_TCP_LOCAL_PORT);
    status = Int_QS100_SendATCMD(cmd_buffer);

    if (status != QS100_NETWORK_CONNECTED)
    {
        if (status == QS100_NETWORK_CME_ERROR || status == QS100_NETWORK_ERROR)
        {
            COM_DEBUG_LN("QS100 socket create failed, start prerequisite diagnostics.");
            Int_QS100_LogSocketCreateDiagnostics();
        }
        return status;
    }

    socket_id = Int_QS100_ParseSocketId();
    if (socket_id == INT_QS100_SOCKET_INVALID)
    {
        return QS100_NETWORK_UNEXPECTED_RESPONSE;
    }

    qs100_socket_id = socket_id;
    qs100_socket_connected = 0U;
    COM_DEBUG_LN("QS100 socket created, socket_id=%d", qs100_socket_id);
    return QS100_NETWORK_CONNECTED;
}

// 连接远程服务器。
// 这里不再硬编码使用 socket 0，而是使用前面 NSOCR 真正返回的 socket_id。
QS100_NetworkStatus Int_QS100_ConnectServer(const char *ip, uint16_t port)
{
    char cmd_buffer[64] = {0};
    QS100_NetworkStatus status = QS100_NETWORK_TIMEOUT;

    if (qs100_socket_id == INT_QS100_SOCKET_INVALID)
    {
        COM_DEBUG_LN("QS100 connect skipped, socket has not been created.");
        return QS100_NETWORK_UNEXPECTED_RESPONSE;
    }

    snprintf(cmd_buffer,
             sizeof(cmd_buffer),
             "AT+NSOCO=%d,%s,%u\r\n",
             qs100_socket_id,
             ip,
             port);

    status = Int_QS100_SendATCMD(cmd_buffer);
    if (status == QS100_NETWORK_CONNECTED)
    {
        qs100_socket_connected = 1U;
    }
    else
    {
        qs100_socket_connected = 0U;
    }

    return status;
}

// 向远程服务器发送数据。
// 当前正式版不再把“NSOSD 命令立即回 OK”直接当成最终成功，
// 而是采用以下两阶段判断：
// 1. 先发 NSOSD，确认模组接受了发送请求；
// 2. 再通过 AT+SEQUENCE 轮询该 sequence 的真实发送状态。
// 只有 sequence 查询最终返回 1，才把这次发送判定为成功。
QS100_NetworkStatus Int_QS100_SendData2Sever(const uint8_t *data, uint16_t length)
{
    char hex_payload[1024] = {0};
    char cmd_buffer[1100] = {0};
    uint16_t i = 0U;
    QS100_NetworkStatus status = QS100_NETWORK_TIMEOUT;
    uint8_t sequence = 0U;

    if (qs100_socket_id == INT_QS100_SOCKET_INVALID)
    {
        COM_DEBUG_LN("QS100 send skipped, socket has not been created.");
        return QS100_NETWORK_UNEXPECTED_RESPONSE;
    }

    // 每个原始字节会被转成两个十六进制字符，
    // 所以这里需要先检查转换后的长度会不会把缓冲区撑爆。
    if ((uint32_t)length * 2U >= sizeof(hex_payload))
    {
        COM_DEBUG_LN("QS100 send data too long, length=%u", length);
        return QS100_NETWORK_RESPONSE_OVERFLOW;
    }

    for (i = 0U; i < length; i++)
    {
        snprintf(&hex_payload[i * 2U], 3U, "%02X", data[i]);
    }

    sequence = Int_QS100_AllocSequence();

    snprintf(cmd_buffer,
             sizeof(cmd_buffer),
             "AT+NSOSD=%d,%u,%s,0x200,%u\r\n",
             qs100_socket_id,
             length,
             hex_payload,
             sequence);

    status = Int_QS100_SendATCMD(cmd_buffer);
    if (status != QS100_NETWORK_CONNECTED)
    {
        qs100_socket_connected = 0U;
        return status;
    }

    // NSOSD 回 OK 只说明模组接受了“发送请求”，并不等价于服务器一定已经收到了数据。
    // 这里继续通过 SEQUENCE 把“请求已提交”和“发送已确认”分开。
    status = Int_QS100_WaitForSendConfirmed(sequence);
    if (status != QS100_NETWORK_CONNECTED)
    {
        COM_DEBUG_LN("QS100 send confirm failed, sequence=%u, status=%s (%d)",
                     sequence,
                     Int_QS100_StatusToString(status),
                     status);
        qs100_socket_connected = 0U;
    }

    return status;
}

// 将数据上传到远程服务器。
// 当前正式版把整个流程拆成四个清晰阶段：
// 1. 等待网络就绪；
// 2. 创建 TCP socket；
// 3. 连接服务器；
// 4. 发送数据并等待发送确认。
// 这样每个阶段失败时，都能明确知道故障到底卡在“网络、socket、connect、send”哪一层。
QS100_NetworkStatus Int_QS100_UploadData(const char *server,
                                         uint16_t port,
                                         uint16_t length,
                                         const uint8_t *data)
{
    QS100_NetworkStatus status = QS100_NETWORK_TIMEOUT;
    uint8_t retry_count = 0U;

    if (server == NULL || data == NULL || length == 0U)
    {
        COM_DEBUG_LN("QS100 upload skipped, invalid args. server_valid=%u, data_valid=%u, length=%u",
                     (server != NULL) ? 1U : 0U,
                     (data != NULL) ? 1U : 0U,
                     length);
        return QS100_NETWORK_UNEXPECTED_RESPONSE;
    }

    // 1. 先确认网络真的已经附着并注册完成。
    status = Int_QS100_WaitForNetworkReady(INT_QS100_NETWORK_READY_TIMEOUT_MS);
    if (status != QS100_NETWORK_CONNECTED)
    {
        COM_DEBUG_LN("QS100 upload data failed, network status=%s (%d)",
                     Int_QS100_StatusToString(status),
                     status);
        return status;
    }
    COM_DEBUG_LN("QS100 network is ready, start uploading data.");

    // 2. 如果还没有 socket，就创建一次；如果已有，就直接复用。
    if (qs100_socket_id == INT_QS100_SOCKET_INVALID)
    {
        for (retry_count = 0U; retry_count < 3U; retry_count++)
        {
            status = Int_QS100_CreateSocket();
            if (status == QS100_NETWORK_CONNECTED)
            {
                break;
            }

            COM_DEBUG_LN("QS100 create socket failed, retry=%u, status=%s (%d)",
                         (uint16_t)(retry_count + 1U),
                         Int_QS100_StatusToString(status),
                         status);
            Com_Delay_s(1U);
        }

        if (status != QS100_NETWORK_CONNECTED)
        {
            COM_DEBUG_LN("QS100 upload data failed, create socket status=%s (%d)",
                         Int_QS100_StatusToString(status),
                         status);
            return status;
        }

        COM_DEBUG_LN("QS100 socket is ready, start connecting server.");
    }
    else
    {
        INT_QS100_VERBOSE_LN("QS100 reuse existing socket, socket_id=%d", qs100_socket_id);
    }

    // 3. 如果该 socket 还没有建立 TCP 连接，则先连接服务器。
    if (qs100_socket_connected == 0U)
    {
        for (retry_count = 0U; retry_count < 3U; retry_count++)
        {
            status = Int_QS100_ConnectServer(server, port);
            if (status == QS100_NETWORK_CONNECTED)
            {
                break;
            }

            COM_DEBUG_LN("QS100 connect server failed, retry=%u, status=%s (%d)",
                         (uint16_t)(retry_count + 1U),
                         Int_QS100_StatusToString(status),
                         status);
            Com_Delay_s(1U);
        }

        if (status != QS100_NETWORK_CONNECTED)
        {
            COM_DEBUG_LN("QS100 upload data failed, connect server status=%s (%d)",
                         Int_QS100_StatusToString(status),
                         status);
            return status;
        }

        COM_DEBUG_LN("QS100 server connected, start sending data.");
    }
    else
    {
        INT_QS100_VERBOSE_LN("QS100 reuse existing server connection, socket_id=%d", qs100_socket_id);
    }

    // 4. 最后再真正发送负载。
    status = Int_QS100_SendData2Sever(data, length);
    if (status != QS100_NETWORK_CONNECTED)
    {
        COM_DEBUG_LN("QS100 upload data failed during send, status=%s (%d)",
                     Int_QS100_StatusToString(status),
                     status);
        return status;
    }

    COM_DEBUG_LN("QS100 upload data success, length=%u", length);
    return QS100_NETWORK_CONNECTED;
}
