#include <mp2797.h>

#include <stddef.h>
#include <string.h>

#include <board_pins.h>
#include <n32g45x_gpio.h>
#include <soft_i2c.h>
#include <system_n32g45x.h>

/* MP2797 register addresses used by the first-stage voltage driver. */
#define MP2797_REG_CELLS_CTRL   0x00u
#define MP2797_REG_PWR_STATUS   0x01u
#define MP2797_REG_PACKFT_CTRL  0x34u
#define MP2797_REG_CELLFT_CTRL  0x35u
#define MP2797_REG_RD_VTOP      0x6Au
#define MP2797_REG_RD_VCELL1    0x6Cu
#define MP2797_REG_ADC_CTRL     0x99u
#define MP2797_REG_CC_CFG       0x9Au
#define MP2797_REG_HR_SCAN0     0x9Cu
#define MP2797_REG_HR_SCAN1     0x9Du
#define MP2797_REG_COMM_CFG     0xA3u
#define MP2797_REG_BAL_LIST     0xA5u
#define MP2797_REG_BAL_CTRL     0xA6u
#define MP2797_REG_BAL_CFG      0xA7u

#define MP2797_CELLS_CTRL_COUNT_MASK       0x000Fu

#define MP2797_PACKFT_VOLTAGE_ONLY_MASK    0x1B33u
#define MP2797_CELLFT_VOLTAGE_ONLY_MASK    0x0036u

#define MP2797_ADC_SCAN_GO                  (1u << 0)
#define MP2797_ADC_SCAN_DONE                (1u << 1)
#define MP2797_ADC_SCAN_ERROR               (1u << 2)

#define MP2797_CC_DISABLE_MASK              ((1u << 0) | (1u << 14))

#define MP2797_HR_SCAN0_CONTROL_MASK        0x037Fu
#define MP2797_HR_SCAN0_VCELL_VTOP_ONLY     ((1u << 0) | (1u << 1))

#define MP2797_COMM_USE_CRC                 (1u << 2)
#define MP2797_COMM_ADDRESS_MASK            0x7F00u
#define MP2797_COMM_ADDRESS_SHIFT           8u

#define MP2797_BALANCE_GO                   (1u << 0)
#define MP2797_BAL_CFG_MODE_MASK             0x0007u

#define MP2797_ADC_RESULT_MASK              0x7FFFu
#define MP2797_CELL_REGISTER_STRIDE         2u

#define MP2797_I2C_READ_BIT                 0x01u
#define MP2797_I2C_WRITE_BIT                0x00u
#define MP2797_CRC8_POLYNOMIAL              0x07u

#define MP2797_SHUTDOWN_DELAY_US            10000u
#define MP2797_WAKE_DELAY_US                5000u
#define MP2797_FUNCTION_COMMAND_DELAY_US    200u

static mp2797_config_t s_mp2797_config;
static bool s_mp2797_config_valid;
static bool s_mp2797_ready;

static mp2797_status_t mp2797_status_from_i2c(soft_i2c_status_t status)
{
    switch (status)
    {
        case SOFT_I2C_STATUS_OK:
            return MP2797_STATUS_OK;

        case SOFT_I2C_STATUS_INVALID_ARG:
            return MP2797_STATUS_INVALID_ARG;

        case SOFT_I2C_STATUS_NACK:
            return MP2797_STATUS_NACK;

        case SOFT_I2C_STATUS_BUS_BUSY:
            return MP2797_STATUS_BUS_BUSY;

        case SOFT_I2C_STATUS_TIMEOUT:
            return MP2797_STATUS_TIMEOUT;

        case SOFT_I2C_STATUS_ERROR:
        default:
            return MP2797_STATUS_ERROR;
    }
}

static void mp2797_default_delay_us(uint32_t delay_us)
{
    uint32_t cycles_per_us = SystemCoreClock / 1000000u;

    if (cycles_per_us == 0u)
    {
        cycles_per_us = 1u;
    }

    while (delay_us-- > 0u)
    {
        volatile uint32_t cycles = cycles_per_us;

        while (cycles-- > 0u)
        {
            __NOP();
        }
    }
}

static void mp2797_delay_us(uint32_t delay_us)
{
    if (s_mp2797_config.delay_us != NULL)
    {
        s_mp2797_config.delay_us(delay_us);
    }
    else
    {
        mp2797_default_delay_us(delay_us);
    }
}

static uint8_t mp2797_crc8_update(uint8_t crc, uint8_t data)
{
    crc ^= data;

    for (uint8_t bit = 0u; bit < 8u; bit++)
    {
        if ((crc & 0x80u) != 0u)
        {
            crc = (uint8_t)((crc << 1u) ^ MP2797_CRC8_POLYNOMIAL);
        }
        else
        {
            crc <<= 1u;
        }
    }

    return crc;
}

static mp2797_status_t mp2797_read_word_internal(uint8_t reg_addr, uint16_t *value)
{
    if (value == NULL)
    {
        return MP2797_STATUS_INVALID_ARG;
    }

    if (!s_mp2797_config_valid)
    {
        return MP2797_STATUS_NOT_READY;
    }

    uint8_t data[3] = {0u, 0u, 0u};
    size_t length = s_mp2797_config.use_crc ? 3u : 2u;
    soft_i2c_status_t i2c_status = soft_i2c_read_reg8(s_mp2797_config.i2c_address,
                                                       reg_addr,
                                                       data,
                                                       length);
    mp2797_status_t status = mp2797_status_from_i2c(i2c_status);

    if (status != MP2797_STATUS_OK)
    {
        return status;
    }

    if (s_mp2797_config.use_crc)
    {
        uint8_t crc = 0u;
        uint8_t address_write = (uint8_t)((s_mp2797_config.i2c_address << 1u)
                                          | MP2797_I2C_WRITE_BIT);
        uint8_t address_read = (uint8_t)((s_mp2797_config.i2c_address << 1u)
                                         | MP2797_I2C_READ_BIT);

        /*
         * MP2797 read CRC includes the register address a second time even
         * though that byte is not present after the repeated START on the bus.
         */
        crc = mp2797_crc8_update(crc, address_write);
        crc = mp2797_crc8_update(crc, reg_addr);
        crc = mp2797_crc8_update(crc, address_read);
        crc = mp2797_crc8_update(crc, reg_addr);
        crc = mp2797_crc8_update(crc, data[0]);
        crc = mp2797_crc8_update(crc, data[1]);

        if (crc != data[2])
        {
            return MP2797_STATUS_CRC_ERROR;
        }
    }

    /* MP2797 I2C reads return bits[7:0] first, then bits[15:8]. */
    *value = (uint16_t)data[0] | ((uint16_t)data[1] << 8u);
    return MP2797_STATUS_OK;
}

static mp2797_status_t mp2797_write_word_internal(uint8_t reg_addr, uint16_t value)
{
    if (!s_mp2797_config_valid)
    {
        return MP2797_STATUS_NOT_READY;
    }

    /*
     * MP2797 I2C writes use bits[15:8] first, then bits[7:0], which is
     * intentionally different from its read byte order.
     */
    uint8_t data[3];
    data[0] = (uint8_t)(value >> 8u);
    data[1] = (uint8_t)value;
    size_t length = 2u;

    if (s_mp2797_config.use_crc)
    {
        uint8_t crc = 0u;
        uint8_t address_write = (uint8_t)((s_mp2797_config.i2c_address << 1u)
                                          | MP2797_I2C_WRITE_BIT);

        crc = mp2797_crc8_update(crc, address_write);
        crc = mp2797_crc8_update(crc, reg_addr);
        crc = mp2797_crc8_update(crc, data[0]);
        crc = mp2797_crc8_update(crc, data[1]);
        data[2] = crc;
        length = 3u;
    }

    return mp2797_status_from_i2c(soft_i2c_write_reg8(s_mp2797_config.i2c_address,
                                                       reg_addr,
                                                       data,
                                                       length));
}

static mp2797_status_t mp2797_update_and_verify(uint8_t reg_addr,
                                                uint16_t mask,
                                                uint16_t value)
{
    uint16_t current = 0u;
    mp2797_status_t status = mp2797_read_word_internal(reg_addr, &current);

    if (status != MP2797_STATUS_OK)
    {
        return status;
    }

    uint16_t updated = (uint16_t)((current & (uint16_t)(~mask)) | (value & mask));

    if (updated != current)
    {
        status = mp2797_write_word_internal(reg_addr, updated);
        if (status != MP2797_STATUS_OK)
        {
            return status;
        }
    }

    uint16_t readback = 0u;
    status = mp2797_read_word_internal(reg_addr, &readback);
    if (status != MP2797_STATUS_OK)
    {
        return status;
    }

    if ((readback & mask) != (value & mask))
    {
        return MP2797_STATUS_CONFIG_MISMATCH;
    }

    return MP2797_STATUS_OK;
}

static mp2797_status_t mp2797_verify_communication_config(void)
{
    uint16_t comm_cfg = 0u;
    mp2797_status_t status = mp2797_read_word_internal(MP2797_REG_COMM_CFG,
                                                       &comm_cfg);

    if (status != MP2797_STATUS_OK)
    {
        return status;
    }

    bool device_uses_crc = (comm_cfg & MP2797_COMM_USE_CRC) != 0u;
    if (device_uses_crc != s_mp2797_config.use_crc)
    {
        return MP2797_STATUS_CONFIG_MISMATCH;
    }

    uint8_t device_address = (uint8_t)((comm_cfg & MP2797_COMM_ADDRESS_MASK)
                                       >> MP2797_COMM_ADDRESS_SHIFT);
    if (device_address != s_mp2797_config.i2c_address)
    {
        return MP2797_STATUS_CONFIG_MISMATCH;
    }

    return MP2797_STATUS_OK;
}

static mp2797_status_t mp2797_apply_voltage_only_config(void)
{
    mp2797_status_t status;

    /*
     * CELLS_CTRL encodes 8 cells as 0x7 through 16 cells as 0xF.
     * Values below 0x7 all select the device's minimum seven-cell setup.
     */
    uint16_t cell_count_code = (uint16_t)(s_mp2797_config.cell_count - 1u);
    status = mp2797_update_and_verify(MP2797_REG_CELLS_CTRL,
                                     MP2797_CELLS_CTRL_COUNT_MASK,
                                     cell_count_code);
    if (status != MP2797_STATUS_OK)
    {
        return status;
    }

    /* Stop all balancing requests before clearing the balancing list/mode. */
    status = mp2797_update_and_verify(MP2797_REG_BAL_CTRL,
                                     MP2797_BALANCE_GO,
                                     0u);
    if (status != MP2797_STATUS_OK)
    {
        return status;
    }

    status = mp2797_update_and_verify(MP2797_REG_BAL_LIST, 0xFFFFu, 0u);
    if (status != MP2797_STATUS_OK)
    {
        return status;
    }

    status = mp2797_update_and_verify(MP2797_REG_BAL_CFG,
                                     MP2797_BAL_CFG_MODE_MASK,
                                     0u);
    if (status != MP2797_STATUS_OK)
    {
        return status;
    }

    /* Disable coulomb counting and its back-to-back restart mode. */
    status = mp2797_update_and_verify(MP2797_REG_CC_CFG,
                                     MP2797_CC_DISABLE_MASK,
                                     0u);
    if (status != MP2797_STATUS_OK)
    {
        return status;
    }

    /*
     * Disable the lithium-oriented autonomous cell/pack voltage actions.
     * The MCU protection layer will later evaluate lead-carbon thresholds.
     */
    status = mp2797_update_and_verify(MP2797_REG_PACKFT_CTRL,
                                     MP2797_PACKFT_VOLTAGE_ONLY_MASK,
                                     0u);
    if (status != MP2797_STATUS_OK)
    {
        return status;
    }

    status = mp2797_update_and_verify(MP2797_REG_CELLFT_CTRL,
                                     MP2797_CELLFT_VOLTAGE_ONLY_MASK,
                                     0u);
    if (status != MP2797_STATUS_OK)
    {
        return status;
    }

    /*
     * Scan only cell voltages and VTOP. Keep CELL_1K_COMP unchanged because
     * it depends on the actual input filter resistance fitted on the PCB.
     */
    status = mp2797_update_and_verify(MP2797_REG_HR_SCAN0,
                                     MP2797_HR_SCAN0_CONTROL_MASK,
                                     MP2797_HR_SCAN0_VCELL_VTOP_ONLY);
    if (status != MP2797_STATUS_OK)
    {
        return status;
    }

    uint16_t enabled_cells = (s_mp2797_config.cell_count == 16u)
                                 ? 0xFFFFu
                                 : (uint16_t)((1u << s_mp2797_config.cell_count) - 1u);

    return mp2797_update_and_verify(MP2797_REG_HR_SCAN1,
                                    0xFFFFu,
                                    enabled_cells);
}

void mp2797_get_default_config(mp2797_config_t *config)
{
    if (config == NULL)
    {
        return;
    }

    config->cell_count = MP2797_MAX_CELL_COUNT;
    config->i2c_address = MP2797_DEFAULT_I2C_ADDRESS;
    config->scan_poll_limit = MP2797_DEFAULT_SCAN_POLL_LIMIT;
    config->use_crc = false;
    config->voltage_only_mode = true;
    config->delay_us = NULL;
}

mp2797_status_t mp2797_init(const mp2797_config_t *config)
{
    if (config == NULL)
    {
        return MP2797_STATUS_INVALID_ARG;
    }

    if ((config->cell_count < MP2797_MIN_CELL_COUNT)
        || (config->cell_count > MP2797_MAX_CELL_COUNT))
    {
        return MP2797_STATUS_INVALID_ARG;
    }

    if (config->i2c_address > 0x7Fu)
    {
        return MP2797_STATUS_INVALID_ARG;
    }

    s_mp2797_config = *config;
    if (s_mp2797_config.i2c_address == 0u)
    {
        s_mp2797_config.i2c_address = MP2797_DEFAULT_I2C_ADDRESS;
    }
    if (s_mp2797_config.scan_poll_limit == 0u)
    {
        s_mp2797_config.scan_poll_limit = MP2797_DEFAULT_SCAN_POLL_LIMIT;
    }

    s_mp2797_config_valid = true;
    s_mp2797_ready = false;

    /*
     * Force a known shutdown state first. The datasheet specifies up to
     * 8 ms to enter shutdown; 10 ms leaves margin before the 5 ms wake wait.
     */
    GPIO_ResetBits(BOARD_AFE_NSHDN_PORT, BOARD_AFE_NSHDN_PIN);
    mp2797_delay_us(MP2797_SHUTDOWN_DELAY_US);
    GPIO_SetBits(BOARD_AFE_NSHDN_PORT, BOARD_AFE_NSHDN_PIN);
    mp2797_delay_us(MP2797_WAKE_DELAY_US);

    soft_i2c_init();
    if (!soft_i2c_is_bus_idle())
    {
        soft_i2c_recover_bus();
        if (!soft_i2c_is_bus_idle())
        {
            return MP2797_STATUS_BUS_BUSY;
        }
    }

    uint16_t power_status = 0u;
    mp2797_status_t status = mp2797_read_word_internal(MP2797_REG_PWR_STATUS,
                                                       &power_status);
    (void)power_status;
    if (status != MP2797_STATUS_OK)
    {
        return status;
    }

    status = mp2797_verify_communication_config();
    if (status != MP2797_STATUS_OK)
    {
        return status;
    }

    if (s_mp2797_config.voltage_only_mode)
    {
        status = mp2797_apply_voltage_only_config();
        if (status != MP2797_STATUS_OK)
        {
            return status;
        }
    }

    s_mp2797_ready = true;
    return MP2797_STATUS_OK;
}

mp2797_status_t mp2797_probe(void)
{
    if (!s_mp2797_config_valid)
    {
        return MP2797_STATUS_NOT_READY;
    }

    uint16_t power_status = 0u;
    return mp2797_read_word_internal(MP2797_REG_PWR_STATUS, &power_status);
}

mp2797_status_t mp2797_configure_voltage_only(void)
{
    if (!s_mp2797_ready)
    {
        return MP2797_STATUS_NOT_READY;
    }

    mp2797_status_t status = mp2797_apply_voltage_only_config();
    if (status == MP2797_STATUS_OK)
    {
        s_mp2797_config.voltage_only_mode = true;
    }

    return status;
}

mp2797_status_t mp2797_read_register(uint8_t reg_addr, uint16_t *value)
{
    return mp2797_read_word_internal(reg_addr, value);
}

mp2797_status_t mp2797_start_voltage_scan(void)
{
    if (!s_mp2797_ready)
    {
        return MP2797_STATUS_NOT_READY;
    }

    uint16_t adc_ctrl = 0u;
    mp2797_status_t status = mp2797_read_word_internal(MP2797_REG_ADC_CTRL,
                                                       &adc_ctrl);
    if (status != MP2797_STATUS_OK)
    {
        return status;
    }

    bool scan_is_active = ((adc_ctrl & MP2797_ADC_SCAN_GO) != 0u)
                          && ((adc_ctrl & (MP2797_ADC_SCAN_DONE
                                          | MP2797_ADC_SCAN_ERROR)) == 0u);
    if (scan_is_active)
    {
        return MP2797_STATUS_BUSY;
    }

    if ((adc_ctrl & MP2797_ADC_SCAN_GO) != 0u)
    {
        status = mp2797_write_word_internal(MP2797_REG_ADC_CTRL, 0u);
        if (status != MP2797_STATUS_OK)
        {
            return status;
        }
        mp2797_delay_us(MP2797_FUNCTION_COMMAND_DELAY_US);
    }

    status = mp2797_write_word_internal(MP2797_REG_ADC_CTRL,
                                        MP2797_ADC_SCAN_GO);
    if (status != MP2797_STATUS_OK)
    {
        return status;
    }

    /*
     * The datasheet requires the serial bus to remain idle for at least
     * 200 us after enabling a functional command in safe mode.
     */
    mp2797_delay_us(MP2797_FUNCTION_COMMAND_DELAY_US);
    return MP2797_STATUS_OK;
}

mp2797_status_t mp2797_get_voltage_scan_state(mp2797_scan_state_t *state)
{
    if (state == NULL)
    {
        return MP2797_STATUS_INVALID_ARG;
    }

    if (!s_mp2797_ready)
    {
        return MP2797_STATUS_NOT_READY;
    }

    uint16_t adc_ctrl = 0u;
    mp2797_status_t status = mp2797_read_word_internal(MP2797_REG_ADC_CTRL,
                                                       &adc_ctrl);
    if (status != MP2797_STATUS_OK)
    {
        return status;
    }

    if ((adc_ctrl & MP2797_ADC_SCAN_ERROR) != 0u)
    {
        *state = MP2797_SCAN_STATE_ERROR;
    }
    else if ((adc_ctrl & MP2797_ADC_SCAN_DONE) != 0u)
    {
        *state = MP2797_SCAN_STATE_DONE;
    }
    else if ((adc_ctrl & MP2797_ADC_SCAN_GO) != 0u)
    {
        *state = MP2797_SCAN_STATE_BUSY;
    }
    else
    {
        *state = MP2797_SCAN_STATE_IDLE;
    }

    return MP2797_STATUS_OK;
}

mp2797_status_t mp2797_wait_voltage_scan(void)
{
    if (!s_mp2797_ready)
    {
        return MP2797_STATUS_NOT_READY;
    }

    for (uint16_t poll = 0u; poll < s_mp2797_config.scan_poll_limit; poll++)
    {
        mp2797_scan_state_t state = MP2797_SCAN_STATE_IDLE;
        mp2797_status_t status = mp2797_get_voltage_scan_state(&state);
        if (status != MP2797_STATUS_OK)
        {
            return status;
        }

        if ((state == MP2797_SCAN_STATE_DONE)
            || (state == MP2797_SCAN_STATE_ERROR))
        {
            status = mp2797_write_word_internal(MP2797_REG_ADC_CTRL, 0u);
            if (status != MP2797_STATUS_OK)
            {
                return status;
            }

            mp2797_delay_us(MP2797_FUNCTION_COMMAND_DELAY_US);
            return (state == MP2797_SCAN_STATE_DONE)
                       ? MP2797_STATUS_OK
                       : MP2797_STATUS_SCAN_ERROR;
        }

        mp2797_delay_us(MP2797_FUNCTION_COMMAND_DELAY_US);
    }

    return MP2797_STATUS_TIMEOUT;
}

uint16_t mp2797_cell_raw_to_mv(uint16_t raw)
{
    uint32_t code = (uint32_t)(raw & MP2797_ADC_RESULT_MASK);

    /* Full scale is 5000 mV over 32768 codes; round to nearest millivolt. */
    return (uint16_t)((code * 5000u + 16384u) / 32768u);
}

uint32_t mp2797_pack_raw_to_mv(uint16_t raw)
{
    uint32_t code = (uint32_t)(raw & MP2797_ADC_RESULT_MASK);

    /* Full scale is 80000 mV over 32768 codes; round to nearest millivolt. */
    return (code * 80000u + 16384u) / 32768u;
}

mp2797_status_t mp2797_read_cell_voltage(uint8_t cell_number,
                                         uint16_t *raw,
                                         uint16_t *millivolts)
{
    if ((raw == NULL) && (millivolts == NULL))
    {
        return MP2797_STATUS_INVALID_ARG;
    }

    if (!s_mp2797_ready)
    {
        return MP2797_STATUS_NOT_READY;
    }

    if ((cell_number == 0u) || (cell_number > s_mp2797_config.cell_count))
    {
        return MP2797_STATUS_INVALID_ARG;
    }

    uint8_t reg_addr = (uint8_t)(MP2797_REG_RD_VCELL1
                                 + ((cell_number - 1u)
                                    * MP2797_CELL_REGISTER_STRIDE));
    uint16_t result = 0u;
    mp2797_status_t status = mp2797_read_word_internal(reg_addr, &result);
    if (status != MP2797_STATUS_OK)
    {
        return status;
    }

    result &= MP2797_ADC_RESULT_MASK;
    if (raw != NULL)
    {
        *raw = result;
    }
    if (millivolts != NULL)
    {
        *millivolts = mp2797_cell_raw_to_mv(result);
    }

    return MP2797_STATUS_OK;
}

mp2797_status_t mp2797_read_pack_voltage(uint16_t *raw, uint32_t *millivolts)
{
    if ((raw == NULL) && (millivolts == NULL))
    {
        return MP2797_STATUS_INVALID_ARG;
    }

    if (!s_mp2797_ready)
    {
        return MP2797_STATUS_NOT_READY;
    }

    uint16_t result = 0u;
    mp2797_status_t status = mp2797_read_word_internal(MP2797_REG_RD_VTOP,
                                                       &result);
    if (status != MP2797_STATUS_OK)
    {
        return status;
    }

    result &= MP2797_ADC_RESULT_MASK;
    if (raw != NULL)
    {
        *raw = result;
    }
    if (millivolts != NULL)
    {
        *millivolts = mp2797_pack_raw_to_mv(result);
    }

    return MP2797_STATUS_OK;
}

mp2797_status_t mp2797_read_cell_voltages(mp2797_cell_voltages_t *voltages)
{
    if (voltages == NULL)
    {
        return MP2797_STATUS_INVALID_ARG;
    }

    memset(voltages, 0, sizeof(*voltages));

    if (!s_mp2797_ready)
    {
        return MP2797_STATUS_NOT_READY;
    }

    voltages->cell_count = s_mp2797_config.cell_count;

    mp2797_status_t status = mp2797_start_voltage_scan();
    if (status != MP2797_STATUS_OK)
    {
        return status;
    }

    status = mp2797_wait_voltage_scan();
    if (status != MP2797_STATUS_OK)
    {
        return status;
    }

    status = mp2797_read_pack_voltage(&voltages->pack_raw,
                                      &voltages->pack_mv);
    if (status != MP2797_STATUS_OK)
    {
        return status;
    }

    for (uint8_t cell = 0u; cell < voltages->cell_count; cell++)
    {
        status = mp2797_read_cell_voltage((uint8_t)(cell + 1u),
                                          &voltages->cell_raw[cell],
                                          &voltages->cell_mv[cell]);
        if (status != MP2797_STATUS_OK)
        {
            return status;
        }

        voltages->cell_sum_mv += voltages->cell_mv[cell];
    }

    voltages->valid = true;
    return MP2797_STATUS_OK;
}

void mp2797_shutdown(void)
{
    GPIO_ResetBits(BOARD_AFE_NSHDN_PORT, BOARD_AFE_NSHDN_PIN);
    s_mp2797_ready = false;
    s_mp2797_config_valid = false;
}

bool mp2797_is_ready(void)
{
    return s_mp2797_ready;
}
