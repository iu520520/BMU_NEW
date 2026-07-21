#ifndef RS232_H
#define RS232_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RS232_RX_BUFFER_SIZE 128u

typedef enum
{
    RS232_STATUS_OK = 0,
    RS232_STATUS_INVALID_ARG,
    RS232_STATUS_RX_EMPTY,
    RS232_STATUS_TX_TIMEOUT,
} rs232_status_t;

typedef struct
{
    uint32_t rx_bytes;
    uint32_t tx_bytes;
    uint32_t rx_buffer_overflows;
    uint32_t hardware_overruns;
    uint32_t frame_errors;
    uint32_t noise_errors;
    uint32_t parity_errors;
} rs232_statistics_t;

/*
 * USART1的115200-8-N-1参数已由NTFx配置，此函数只初始化软件缓冲区。
 */
void rs232_init(void);

/*
 * 轮询USART1并把收到的数据放入环形缓冲区。
 * 当前未启用接收中断，因此主循环必须高频调用该函数。
 */
void rs232_poll(void);

size_t rs232_available(void);
rs232_status_t rs232_read_byte(uint8_t *data);
size_t rs232_read(uint8_t *data, size_t capacity);
void rs232_clear_rx(void);

rs232_status_t rs232_write_byte(uint8_t data, uint32_t timeout_ms);
rs232_status_t rs232_write(const uint8_t *data, size_t length, uint32_t timeout_ms);
rs232_status_t rs232_write_string(const char *text, uint32_t timeout_ms);
rs232_status_t rs232_flush(uint32_t timeout_ms);

const rs232_statistics_t *rs232_get_statistics(void);
void rs232_clear_statistics(void);

#ifdef __cplusplus
}
#endif

#endif /* RS232_H */
