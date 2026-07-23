#include "pc_uart.h"
#include <stdbool.h>
#include <string.h>

#include "board_pins.h"

#define PC_UART_POLL_MAX_BYTES 64u//限制每次轮询最多读取 64 字节
#define PC_UART_ERROR_FLAGS                                                   \
    (USART_FLAG_OREF | USART_FLAG_FEF | USART_FLAG_NEF | USART_FLAG_PEF)//USART 接收硬件错误标志

extern __IO uint32_t mwTick;


static uint8_t s_rx_buffer[PC_UART_RX_BUFFER_SIZE];//用来暂存从 PC 串口收到但尚未被上层读取的字节
static size_t s_rx_head;//环形缓冲区写入位置，指向下一个可写入的下标
static size_t s_rx_tail;// 环形缓冲区读取位置，指向下一个待读取的下标。
static pc_uart_statistics_t s_statistics;


static size_t pc_uart_next_index(size_t index)//计算环形接收缓冲区中指定位置的下一个下标。
{ 
    index++; 
    return (index >= PC_UART_RX_BUFFER_SIZE) ? 0u : index; 
}


static bool pc_uart_timeout_reached(uint32_t start_ms, uint32_t timeout_ms)//判断是否超时
{ 
    return ((timeout_ms == 0u) || ((uint32_t)(mwTick - start_ms) >= timeout_ms));
} 


static void pc_uart_store_received_byte(uint8_t data)//把从串口收到的一个字节存入软件缓冲区
{ 
    size_t next_head = pc_uart_next_index(s_rx_head); 

    if (next_head == s_rx_tail) //如果缓冲区满，丢弃新数据，并记录数据溢出
    { 
        s_statistics.rx_buffer_overflows++; 
        return; 
    } 
    s_rx_buffer[s_rx_head] = data; 
    s_rx_head = next_head; 
    s_statistics.rx_bytes++; 
} 

/* 根据 USART 状态寄存器中的错误标志更新错误统计信息。 */
static void pc_uart_record_hardware_errors(uint16_t flags)
{ 
    if ((flags & USART_FLAG_OREF) != 0u)  { s_statistics.hardware_overruns++; } 
    if ((flags & USART_FLAG_FEF) != 0u)   { s_statistics.frame_errors++;} 
    if ((flags & USART_FLAG_NEF) != 0u)   { s_statistics.noise_errors++; } 
    if ((flags & USART_FLAG_PEF) != 0u)   { s_statistics.parity_errors++; } 
} 

/* 初始化 PC 串口的软件缓冲区和统计数据，并清除硬件遗留状态。 */
void pc_uart_init(void)
{ 
    s_rx_head = 0u; //读写指针复位
    s_rx_tail = 0u;
    memset(&s_statistics, 0, sizeof(s_statistics)); 

    while ((BOARD_PC_UART->STS & (USART_FLAG_RXDNE | PC_UART_ERROR_FLAGS)) != 0u) // 当硬件仍有接收数据或错误状态时继续读取
    { 
        (void)USART_ReceiveData(BOARD_PC_UART);  
    } 
} 

/* 轮询 USART 硬件，把当前可读数据搬运到软件环形缓冲区。 */
void pc_uart_poll(void)
{ 
    for (uint32_t count = 0u; count < PC_UART_POLL_MAX_BYTES; count++) 
    { 
        uint16_t flags = (uint16_t)BOARD_PC_UART->STS; // 读取一次 USART 状态寄存器
        uint16_t errors = (uint16_t)(flags & PC_UART_ERROR_FLAGS); //提取错误标志

        if (errors != 0u)
        { 
            pc_uart_record_hardware_errors(errors); //按错误类型更新串口错误统计
            (void)USART_ReceiveData(BOARD_PC_UART); //读取数据寄存器，清除本次接收错误状态
            continue; 
        } 

        if ((flags & USART_FLAG_RXDNE) == 0u) 
        { 
            break; 
        } 

        pc_uart_store_received_byte((uint8_t)USART_ReceiveData(BOARD_PC_UART)); 
    } 
} 


size_t pc_uart_available(void)// 返回软件接收缓冲区中尚未被上层读取的字节数量
{ 
    pc_uart_poll(); // 读取串口数据

    if (s_rx_head >= s_rx_tail) 
    { 
        return s_rx_head - s_rx_tail; 
    } 

    return PC_UART_RX_BUFFER_SIZE - s_rx_tail + s_rx_head; 
} 


pc_uart_status_t pc_uart_read_byte(uint8_t *data)// 从软件接收缓冲区读取一个字节
{ 
    if (data == NULL)
    { 
        return PC_UART_STATUS_INVALID_ARG; 
    } 

    pc_uart_poll(); //轮询串口，读取最新数据

    if (s_rx_head == s_rx_tail) //读写指针相等表示软件接收缓冲区为空
    { 
        return PC_UART_STATUS_RX_EMPTY; 
    } 

    *data = s_rx_buffer[s_rx_tail]; 
    s_rx_tail = pc_uart_next_index(s_rx_tail);
    return PC_UART_STATUS_OK; 
} 


size_t pc_uart_read(uint8_t *data, size_t capacity)//从缓冲区批量读取capacity个字节
{ 
    size_t count = 0u; 

    if ((data == NULL) && (capacity != 0u)) 
    { 
        return 0u; 
    } 

    pc_uart_poll(); 

    while ((count < capacity) && (s_rx_head != s_rx_tail)) 
    { 
        data[count++] = s_rx_buffer[s_rx_tail]; 
        s_rx_tail = pc_uart_next_index(s_rx_tail); 
    } 

    return count; 
} 


void pc_uart_clear_rx(void)//清空 PC 串口的软件接收缓冲区和 USART 硬件中的遗留接收状态
{ 
    s_rx_head = 0u; 
    s_rx_tail = 0u; 

    while ((BOARD_PC_UART->STS & (USART_FLAG_RXDNE | PC_UART_ERROR_FLAGS)) != 0u) // 当硬件仍存在接收数据或错误标志时继续清理
    { 
        (void)USART_ReceiveData(BOARD_PC_UART); // 读取并丢弃遗留字节，同时清除相关错误标志
    } 
} 


pc_uart_status_t pc_uart_write_byte(uint8_t data, uint32_t timeout_ms)//在指定超时时间内向 PC 串口发送一个字节
{ 
    uint32_t start_ms = mwTick; //保存开始等待发送寄存器的系统时间

    while (USART_GetFlagStatus(BOARD_PC_UART, USART_FLAG_TXDE) == RESET) //等待发送数据寄存器变为空闲
    { 
        if (pc_uart_timeout_reached(start_ms, timeout_ms)) //检查等待发送寄存器是否已经超时
        {
            return PC_UART_STATUS_TX_TIMEOUT; 
        } 
    } 

    USART_SendData(BOARD_PC_UART, data); //把待发送字节写入 USART 发送数据寄存器
    s_statistics.tx_bytes++; //累计成功写入 USART 的发送字节数
    return PC_UART_STATUS_OK; 
} 

/* 在指定时间内向 PC 串口连续发送一段字节数据。 */
pc_uart_status_t pc_uart_write(const uint8_t *data, size_t length, uint32_t timeout_ms)
{ 
    uint32_t start_ms = mwTick; //保存整段数据开始发送时的系统时间

    if ((data == NULL) && (length != 0u)) 
    { 
        return PC_UART_STATUS_INVALID_ARG;
    }

    for (size_t index = 0u; index < length; index++) //依次处理源缓冲区中的每一个待发送字节
    { 
        while (USART_GetFlagStatus(BOARD_PC_UART, USART_FLAG_TXDE) == RESET) //等待串口可以接收下一个发送字节
        { 
            if (pc_uart_timeout_reached(start_ms, timeout_ms)) //检查整段发送过程是否超过总超时时间
            { 
                return PC_UART_STATUS_TX_TIMEOUT; //超时后停止剩余数据发送并返回错误
            } 
        }
        USART_SendData(BOARD_PC_UART, data[index]); //把当前下标对应的一个字节写入发送寄存器
        s_statistics.tx_bytes++; 
        } 

    return PC_UART_STATUS_OK;
}
 

/* 将以空字符结尾的 C 字符串发送到 PC 串口。 */
pc_uart_status_t pc_uart_write_string(const char *text, uint32_t timeout_ms)
{  
    if (text == NULL)
    { 
        return PC_UART_STATUS_INVALID_ARG;
    } 

    return pc_uart_write((const uint8_t *)text, strlen(text), timeout_ms); /* 计算字符串长度后复用批量字节发送函数。 */
} 

/* 等待 USART 移位寄存器把最后一个字节完整发送出去。 */
pc_uart_status_t pc_uart_flush(uint32_t timeout_ms)
{ 
    uint32_t start_ms = mwTick; //保存开始等待发送完成标志的系统时间

    while (USART_GetFlagStatus(BOARD_PC_UART, USART_FLAG_TXC) == RESET) //等待 USART 发送完成标志置位。
    { /* 等待最后一个字节发送完成循环开始。 */
        if (pc_uart_timeout_reached(start_ms, timeout_ms)) // 检查等待完整发送是否已经超时
        { /* 等待发送完成超时分支开始。 */
            return PC_UART_STATUS_TX_TIMEOUT; 
        } 
    } 
    return PC_UART_STATUS_OK; 
} 

/* 获取只读的 PC 串口累计统计信息。 */
const pc_uart_statistics_t *pc_uart_get_statistics(void)
{ 
    return &s_statistics; /* 返回内部统计结构地址，调用者只能通过 const 指针读取。 */
} 

/* 将 PC 串口的全部累计统计计数清零。 */
void pc_uart_clear_statistics(void)
{
    memset(&s_statistics, 0, sizeof(s_statistics)); 
} 
