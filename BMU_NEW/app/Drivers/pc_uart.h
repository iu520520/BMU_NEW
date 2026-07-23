#ifndef PC_UART_H
#define PC_UART_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PC_UART_RX_BUFFER_SIZE 128u

typedef enum
{
    PC_UART_STATUS_OK = 0,
    PC_UART_STATUS_INVALID_ARG,
    PC_UART_STATUS_RX_EMPTY,
    PC_UART_STATUS_TX_TIMEOUT,
} pc_uart_status_t;

typedef struct
{
    uint32_t rx_bytes;
    uint32_t tx_bytes;
    uint32_t rx_buffer_overflows;
    uint32_t hardware_overruns;
    uint32_t frame_errors;
    uint32_t noise_errors;
    uint32_t parity_errors;
} pc_uart_statistics_t;

/* UART4 的波特率、数据位和 PC10/PC11 复用配置由 NTFx 生成代码完成。 */
void pc_uart_init(void);

/* 由 UART4_IRQHandler 调用：读取硬件接收寄存器并写入环形缓冲区。 */
void pc_uart_irq_handler(void);

size_t pc_uart_available(void);
pc_uart_status_t pc_uart_read_byte(uint8_t *data);
size_t pc_uart_read(uint8_t *data, size_t capacity);
void pc_uart_clear_rx(void);

pc_uart_status_t pc_uart_write_byte(uint8_t data, uint32_t timeout_ms);
pc_uart_status_t pc_uart_write(const uint8_t *data, size_t length, uint32_t timeout_ms);
pc_uart_status_t pc_uart_write_string(const char *text, uint32_t timeout_ms);
pc_uart_status_t pc_uart_flush(uint32_t timeout_ms);

const pc_uart_statistics_t *pc_uart_get_statistics(void);
void pc_uart_clear_statistics(void);

#ifdef __cplusplus
}
#endif

#endif /* PC_UART_H */
