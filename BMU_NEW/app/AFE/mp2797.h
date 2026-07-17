#ifndef MP2797_H
#define MP2797_H

#include <stdbool.h>
#include <stdint.h>

#define MP2797_MIN_CELL_COUNT          7u
#define MP2797_MAX_CELL_COUNT          16u
#define MP2797_DEFAULT_I2C_ADDRESS     0x01u
#define MP2797_DEFAULT_SCAN_POLL_LIMIT 100u

typedef enum
{
    MP2797_STATUS_OK = 0,
    MP2797_STATUS_ERROR,
    MP2797_STATUS_TIMEOUT,
    MP2797_STATUS_INVALID_ARG,
    MP2797_STATUS_NOT_READY,
    MP2797_STATUS_NACK,
    MP2797_STATUS_BUS_BUSY,
    MP2797_STATUS_CRC_ERROR,
    MP2797_STATUS_SCAN_ERROR,
    MP2797_STATUS_BUSY,
    MP2797_STATUS_CONFIG_MISMATCH,
} mp2797_status_t;

typedef enum
{
    MP2797_SCAN_STATE_IDLE = 0,
    MP2797_SCAN_STATE_BUSY,
    MP2797_SCAN_STATE_DONE,
    MP2797_SCAN_STATE_ERROR,
} mp2797_scan_state_t;

typedef void (*mp2797_delay_us_fn_t)(uint32_t delay_us);

typedef struct
{
    /* MP2797 supports 7 to 16 series cells. */
    uint8_t cell_count;

    /* Set to 0 to use MP2797_DEFAULT_I2C_ADDRESS. */
    uint8_t i2c_address;

    /* Set to 0 to use MP2797_DEFAULT_SCAN_POLL_LIMIT. */
    uint16_t scan_poll_limit;

    /* Must match COMM_CFG.USE_COMM_CRC in the actual device. */
    bool use_crc;

    /*
     * When true, init applies and verifies the volatile bench-test setup:
     * - high-resolution cell and VTOP voltage scan enabled;
     * - synchronized current, coulomb counter and balancing disabled;
     * - cell/pack UV, OV, dead-cell and mismatch actions disabled.
     *
     * This does not turn the complete board into a safe production BMS.
     */
    bool voltage_only_mode;

    /* Optional timing hook. NULL selects the internal blocking delay. */
    mp2797_delay_us_fn_t delay_us;
} mp2797_config_t;

typedef struct
{
    uint16_t cell_raw[MP2797_MAX_CELL_COUNT];
    uint16_t cell_mv[MP2797_MAX_CELL_COUNT];
    uint16_t pack_raw;
    uint32_t pack_mv;
    uint32_t cell_sum_mv;
    uint8_t cell_count;
    bool valid;
} mp2797_cell_voltages_t;

/* Fill a safe first-stage configuration for this 16-cell project. */
void mp2797_get_default_config(mp2797_config_t *config);

/* Wake the AFE, verify communication, and apply the selected volatile setup. */
mp2797_status_t mp2797_init(const mp2797_config_t *config);

/* Read a harmless status register to verify that the AFE acknowledges. */
mp2797_status_t mp2797_probe(void);

/* Re-apply and read back the controlled voltage-only volatile settings. */
mp2797_status_t mp2797_configure_voltage_only(void);

/* Read-only diagnostic access; unrestricted register writes stay private. */
mp2797_status_t mp2797_read_register(uint8_t reg_addr, uint16_t *value);

mp2797_status_t mp2797_start_voltage_scan(void);
mp2797_status_t mp2797_get_voltage_scan_state(mp2797_scan_state_t *state);
mp2797_status_t mp2797_wait_voltage_scan(void);

/* cell_number is one-based: 1 to configured cell_count. */
mp2797_status_t mp2797_read_cell_voltage(uint8_t cell_number,
                                         uint16_t *raw,
                                         uint16_t *millivolts);
mp2797_status_t mp2797_read_pack_voltage(uint16_t *raw, uint32_t *millivolts);

/* Start one scan, wait with a bounded timeout, then publish a full snapshot. */
mp2797_status_t mp2797_read_cell_voltages(mp2797_cell_voltages_t *voltages);

uint16_t mp2797_cell_raw_to_mv(uint16_t raw);
uint32_t mp2797_pack_raw_to_mv(uint16_t raw);

/* Pull nSHDN low and invalidate the current driver state. */
void mp2797_shutdown(void);
bool mp2797_is_ready(void);

#endif /* MP2797_H */
