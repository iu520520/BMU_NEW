#include "pc_uart.h"

#include <stdbool.h>
#include <string.h>

#include "board_pins.h"

#define PC_UART_POLL_MAX_BYTES 64u
#define PC_UART_ERROR_FLAGS                                                   \
    (USART_FLAG_OREF | USART_FLAG_FEF | USART_FLAG_NEF | USART_FLAG_PEF)

extern __IO uint32_t mwTick;

static uint8_t s_rx_buffer[PC_UART_RX_BUFFER_SIZE];
static size_t s_rx_head;
static size_t s_rx_tail;
static pc_uart_statistics_t s_statistics;

static size_t pc_uart_next_index(size_t index)
{
    index++;
    return (index >= PC_UART_RX_BUFFER_SIZE) ? 0u : index;
}

static bool pc_uart_timeout_reached(uint32_t start_ms, uint32_t timeout_ms)
{
    return ((timeout_ms == 0u) || ((uint32_t)(mwTick - start_ms) >= timeout_ms));
}

static void pc_uart_store_received_byte(uint8_t data)
{
    size_t next_head = pc_uart_next_index(s_rx_head);
    if (next_head == s_rx_tail)
    {
        /* 缓冲区已满时丢弃新数据，避免覆盖尚未处理的命令。 */
        s_statistics.rx_buffer_overflows++;
        return;
    }

    s_rx_buffer[s_rx_head] = data;
    s_rx_head = next_head;
    s_statistics.rx_bytes++;
}

static void pc_uart_record_hardware_errors(uint16_t flags)
{
    if ((flags & USART_FLAG_OREF) != 0u) { s_statistics.hardware_overruns++; }
    if ((flags & USART_FLAG_FEF) != 0u)  { s_statistics.frame_errors++; }
    if ((flags & USART_FLAG_NEF) != 0u)  { s_statistics.noise_errors++; }
    if ((flags & USART_FLAG_PEF) != 0u)  { s_statistics.parity_errors++; }
}

void pc_uart_init(void)
{
    s_rx_head = 0u;
    s_rx_tail = 0u;
    memset(&s_statistics, 0, sizeof(s_statistics));

    /* 清除上电期间可能残留的接收数据和错误状态。 */
    while ((BOARD_PC_UART->STS & (USART_FLAG_RXDNE | PC_UART_ERROR_FLAGS)) != 0u)
    {
        (void)USART_ReceiveData(BOARD_PC_UART);
    }
}

void pc_uart_poll(void)
{
    for (uint32_t count = 0u; count < PC_UART_POLL_MAX_BYTES; count++)
    {
        uint16_t flags = (uint16_t)BOARD_PC_UART->STS;
        uint16_t errors = (uint16_t)(flags & PC_UART_ERROR_FLAGS);

        if (errors != 0u)
        {
            pc_uart_record_hardware_errors(errors);
            (void)USART_ReceiveData(BOARD_PC_UART);
            continue;
        }
        if ((flags & USART_FLAG_RXDNE) == 0u)
        {
            break;
        }
        pc_uart_store_received_byte((uint8_t)USART_ReceiveData(BOARD_PC_UART));
    }
}

size_t pc_uart_available(void)
{
    pc_uart_poll();
    if (s_rx_head >= s_rx_tail)
    {
        return s_rx_head - s_rx_tail;
    }
    return PC_UART_RX_BUFFER_SIZE - s_rx_tail + s_rx_head;
}

pc_uart_status_t pc_uart_read_byte(uint8_t *data)
{
    if (data == NULL)
    {
        return PC_UART_STATUS_INVALID_ARG;
    }
    pc_uart_poll();
    if (s_rx_head == s_rx_tail)
    {
        return PC_UART_STATUS_RX_EMPTY;
    }
    *data = s_rx_buffer[s_rx_tail];
    s_rx_tail = pc_uart_next_index(s_rx_tail);
    return PC_UART_STATUS_OK;
}

size_t pc_uart_read(uint8_t *data, size_t capacity)
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

void pc_uart_clear_rx(void)
{
    s_rx_head = 0u;
    s_rx_tail = 0u;
    while ((BOARD_PC_UART->STS & (USART_FLAG_RXDNE | PC_UART_ERROR_FLAGS)) != 0u)
    {
        (void)USART_ReceiveData(BOARD_PC_UART);
    }
}

pc_uart_status_t pc_uart_write_byte(uint8_t data, uint32_t timeout_ms)
{
    uint32_t start_ms = mwTick;
    while (USART_GetFlagStatus(BOARD_PC_UART, USART_FLAG_TXDE) == RESET)
    {
        if (pc_uart_timeout_reached(start_ms, timeout_ms))
        {
            return PC_UART_STATUS_TX_TIMEOUT;
        }
    }
    USART_SendData(BOARD_PC_UART, data);
    s_statistics.tx_bytes++;
    return PC_UART_STATUS_OK;
}

pc_uart_status_t pc_uart_write(const uint8_t *data, size_t length, uint32_t timeout_ms)
{
    uint32_t start_ms = mwTick;
    if ((data == NULL) && (length != 0u))
    {
        return PC_UART_STATUS_INVALID_ARG;
    }
    for (size_t index = 0u; index < length; index++)
    {
        while (USART_GetFlagStatus(BOARD_PC_UART, USART_FLAG_TXDE) == RESET)
        {
            if (pc_uart_timeout_reached(start_ms, timeout_ms))
            {
                return PC_UART_STATUS_TX_TIMEOUT;
            }
        }
        USART_SendData(BOARD_PC_UART, data[index]);
        s_statistics.tx_bytes++;
    }
    return PC_UART_STATUS_OK;
}

pc_uart_status_t pc_uart_write_string(const char *text, uint32_t timeout_ms)
{
    if (text == NULL)
    {
        return PC_UART_STATUS_INVALID_ARG;
    }
    return pc_uart_write((const uint8_t *)text, strlen(text), timeout_ms);
}

pc_uart_status_t pc_uart_flush(uint32_t timeout_ms)
{
    uint32_t start_ms = mwTick;
    while (USART_GetFlagStatus(BOARD_PC_UART, USART_FLAG_TXC) == RESET)
    {
        if (pc_uart_timeout_reached(start_ms, timeout_ms))
        {
            return PC_UART_STATUS_TX_TIMEOUT;
        }
    }
    return PC_UART_STATUS_OK;
}

const pc_uart_statistics_t *pc_uart_get_statistics(void)
{
    return &s_statistics;
}

void pc_uart_clear_statistics(void)
{
    memset(&s_statistics, 0, sizeof(s_statistics));
}
