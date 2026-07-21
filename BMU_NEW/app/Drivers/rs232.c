#include "rs232.h"

#include <string.h>

#include "board_pins.h"

#define RS232_POLL_MAX_BYTES 64u
#define RS232_ERROR_FLAGS                                                   \
    (USART_FLAG_OREF | USART_FLAG_FEF | USART_FLAG_NEF | USART_FLAG_PEF)

extern __IO uint32_t mwTick;

static uint8_t s_rx_buffer[RS232_RX_BUFFER_SIZE];
static size_t s_rx_head;
static size_t s_rx_tail;
static rs232_statistics_t s_statistics;

static size_t rs232_next_index(size_t index)
{
    index++;
    if (index >= RS232_RX_BUFFER_SIZE)
    {
        index = 0u;
    }
    return index;
}

static bool rs232_timeout_reached(uint32_t start_ms, uint32_t timeout_ms)
{
    return ((timeout_ms == 0u) || ((uint32_t)(mwTick - start_ms) >= timeout_ms));
}

static void rs232_store_received_byte(uint8_t data)
{
    size_t next_head = rs232_next_index(s_rx_head);
    if (next_head == s_rx_tail)
    {
        /* 缓冲区满时丢弃新数据，保留已收到的完整命令前缀。 */
        s_statistics.rx_buffer_overflows++;
        return;
    }

    s_rx_buffer[s_rx_head] = data;
    s_rx_head = next_head;
    s_statistics.rx_bytes++;
}

static void rs232_record_hardware_errors(uint16_t flags)
{
    if ((flags & USART_FLAG_OREF) != 0u)
    {
        s_statistics.hardware_overruns++;
    }
    if ((flags & USART_FLAG_FEF) != 0u)
    {
        s_statistics.frame_errors++;
    }
    if ((flags & USART_FLAG_NEF) != 0u)
    {
        s_statistics.noise_errors++;
    }
    if ((flags & USART_FLAG_PEF) != 0u)
    {
        s_statistics.parity_errors++;
    }
}

void rs232_init(void)
{
    s_rx_head = 0u;
    s_rx_tail = 0u;
    memset(&s_statistics, 0, sizeof(s_statistics));

    /* 清除上电或调试器连接期间可能遗留的接收数据和错误状态。 */
    while ((BOARD_RS232_USART->STS & (USART_FLAG_RXDNE | RS232_ERROR_FLAGS)) != 0u)
    {
        (void)USART_ReceiveData(BOARD_RS232_USART);
    }
}

void rs232_poll(void)
{
    for (uint32_t count = 0u; count < RS232_POLL_MAX_BYTES; count++)
    {
        uint16_t flags = (uint16_t)BOARD_RS232_USART->STS;
        uint16_t errors = (uint16_t)(flags & RS232_ERROR_FLAGS);

        if (errors != 0u)
        {
            /* 按芯片要求先读状态寄存器，再读数据寄存器清除错误标志。 */
            rs232_record_hardware_errors(errors);
            (void)USART_ReceiveData(BOARD_RS232_USART);
            continue;
        }

        if ((flags & USART_FLAG_RXDNE) == 0u)
        {
            break;
        }

        rs232_store_received_byte((uint8_t)USART_ReceiveData(BOARD_RS232_USART));
    }
}

size_t rs232_available(void)
{
    rs232_poll();

    if (s_rx_head >= s_rx_tail)
    {
        return s_rx_head - s_rx_tail;
    }
    return RS232_RX_BUFFER_SIZE - s_rx_tail + s_rx_head;
}

rs232_status_t rs232_read_byte(uint8_t *data)
{
    if (data == NULL)
    {
        return RS232_STATUS_INVALID_ARG;
    }

    rs232_poll();
    if (s_rx_head == s_rx_tail)
    {
        return RS232_STATUS_RX_EMPTY;
    }

    *data = s_rx_buffer[s_rx_tail];
    s_rx_tail = rs232_next_index(s_rx_tail);
    return RS232_STATUS_OK;
}

size_t rs232_read(uint8_t *data, size_t capacity)
{
    size_t count = 0u;

    if ((data == NULL) && (capacity != 0u))
    {
        return 0u;
    }

    rs232_poll();
    while ((count < capacity) && (s_rx_head != s_rx_tail))
    {
        data[count] = s_rx_buffer[s_rx_tail];
        s_rx_tail = rs232_next_index(s_rx_tail);
        count++;
    }
    return count;
}

void rs232_clear_rx(void)
{
    s_rx_head = 0u;
    s_rx_tail = 0u;

    while ((BOARD_RS232_USART->STS & (USART_FLAG_RXDNE | RS232_ERROR_FLAGS)) != 0u)
    {
        (void)USART_ReceiveData(BOARD_RS232_USART);
    }
}

rs232_status_t rs232_write_byte(uint8_t data, uint32_t timeout_ms)
{
    uint32_t start_ms = mwTick;
    while (USART_GetFlagStatus(BOARD_RS232_USART, USART_FLAG_TXDE) == RESET)
    {
        if (rs232_timeout_reached(start_ms, timeout_ms))
        {
            return RS232_STATUS_TX_TIMEOUT;
        }
    }

    USART_SendData(BOARD_RS232_USART, data);
    s_statistics.tx_bytes++;
    return RS232_STATUS_OK;
}

rs232_status_t rs232_write(const uint8_t *data, size_t length, uint32_t timeout_ms)
{
    uint32_t start_ms = mwTick;

    if ((data == NULL) && (length != 0u))
    {
        return RS232_STATUS_INVALID_ARG;
    }

    for (size_t index = 0u; index < length; index++)
    {
        while (USART_GetFlagStatus(BOARD_RS232_USART, USART_FLAG_TXDE) == RESET)
        {
            if (rs232_timeout_reached(start_ms, timeout_ms))
            {
                return RS232_STATUS_TX_TIMEOUT;
            }
        }

        USART_SendData(BOARD_RS232_USART, data[index]);
        s_statistics.tx_bytes++;
    }

    return RS232_STATUS_OK;
}

rs232_status_t rs232_write_string(const char *text, uint32_t timeout_ms)
{
    if (text == NULL)
    {
        return RS232_STATUS_INVALID_ARG;
    }

    return rs232_write((const uint8_t *)text, strlen(text), timeout_ms);
}

rs232_status_t rs232_flush(uint32_t timeout_ms)
{
    uint32_t start_ms = mwTick;
    while (USART_GetFlagStatus(BOARD_RS232_USART, USART_FLAG_TXC) == RESET)
    {
        if (rs232_timeout_reached(start_ms, timeout_ms))
        {
            return RS232_STATUS_TX_TIMEOUT;
        }
    }

    return RS232_STATUS_OK;
}

const rs232_statistics_t *rs232_get_statistics(void)
{
    return &s_statistics;
}

void rs232_clear_statistics(void)
{
    memset(&s_statistics, 0, sizeof(s_statistics));
}
