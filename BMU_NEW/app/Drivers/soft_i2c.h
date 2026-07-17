#ifndef SOFT_I2C_H
#define SOFT_I2C_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    SOFT_I2C_STATUS_OK = 0,
    SOFT_I2C_STATUS_ERROR,
    SOFT_I2C_STATUS_INVALID_ARG,//参数错误，比如传入了空指针
    SOFT_I2C_STATUS_NACK,//从机没有回复
    SOFT_I2C_STATUS_BUS_BUSY,//总线忙
    SOFT_I2C_STATUS_TIMEOUT,//总线超时
} soft_i2c_status_t;

void soft_i2c_init(void);
bool soft_i2c_is_bus_idle(void);
void soft_i2c_recover_bus(void);

soft_i2c_status_t soft_i2c_write(uint8_t dev_addr_7bit, const uint8_t *data, size_t len);
soft_i2c_status_t soft_i2c_read(uint8_t dev_addr_7bit, uint8_t *data, size_t len);
soft_i2c_status_t soft_i2c_write_read(uint8_t dev_addr_7bit,
                                      const uint8_t *tx_data,
                                      size_t tx_len,
                                      uint8_t *rx_data,
                                      size_t rx_len);

soft_i2c_status_t soft_i2c_write_reg8(uint8_t dev_addr_7bit,
                                      uint8_t reg_addr,
                                      const uint8_t *data,
                                      size_t len);
soft_i2c_status_t soft_i2c_read_reg8(uint8_t dev_addr_7bit,
                                     uint8_t reg_addr,
                                     uint8_t *data,
                                     size_t len);

#ifdef __cplusplus
}
#endif

#endif /* SOFT_I2C_H */
