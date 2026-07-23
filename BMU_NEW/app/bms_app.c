#include "bms_app.h"

#include "n32g45x_cfg.h"
#include "console.h"
#include "pc_uart.h"
#include "rs232.h"

#define BMS_VOLTAGE_SAMPLE_PERIOD_MS 1000u

extern __IO uint32_t mwTick;

static mp2797_status_t s_afe_status = MP2797_STATUS_NOT_READY;
static mp2797_config_t s_afe_config;
static mp2797_cell_voltages_t s_voltages;
static uint32_t s_last_sample_ms;
static bool s_afe_sampling_enabled;

void bms_app_init(void)
{
    rs232_init();
    pc_uart_init();
    console_init();

    cell_balance_config_t balance_config;
    cell_balance_get_default_config(&balance_config);
    (void)cell_balance_init(&balance_config);

    mp2797_get_default_config(&s_afe_config);
    mp2797_shutdown();
    s_afe_status = MP2797_STATUS_NOT_READY;
    s_voltages.valid = false;
    s_afe_sampling_enabled = false;
    s_last_sample_ms = mwTick;
}

void bms_app_task(void)
{
    /* 必须放在周期任务判断之前，避免人工输入命令时接收寄存器溢出。 */
    rs232_poll();
    pc_uart_poll();
    console_task();

    /* 未收到 startafe 命令时不访问 AFE，保证调试串口始终可响应。 */
    if (!s_afe_sampling_enabled)
    {
        return;
    }

    uint32_t now_ms = mwTick;
    if ((uint32_t)(now_ms - s_last_sample_ms) < BMS_VOLTAGE_SAMPLE_PERIOD_MS)
    {
        return;
    }
    s_last_sample_ms = now_ms;

    if (!mp2797_is_ready())
    {
        cell_balance_stop(CELL_BALANCE_STOP_AFE_ERROR, now_ms);
        s_afe_status = MP2797_STATUS_NOT_READY;
        s_voltages.valid = false;
        s_afe_sampling_enabled = false;
        return;
    }

    s_afe_status = mp2797_read_cell_voltages(&s_voltages);
    if ((s_afe_status != MP2797_STATUS_OK) || !s_voltages.valid)
    {
        cell_balance_stop(CELL_BALANCE_STOP_AFE_ERROR, now_ms);
        s_voltages.valid = false;
        s_afe_sampling_enabled = false;
        mp2797_shutdown();
        return;
    }

    (void)cell_balance_process(s_voltages.cell_mv,
                               s_voltages.cell_count,
                               now_ms);
}

mp2797_status_t bms_app_start_afe(void)
{
    if (s_afe_sampling_enabled && mp2797_is_ready())
    {
        return MP2797_STATUS_OK;
    }

    s_afe_sampling_enabled = false;
    s_voltages.valid = false;
    s_afe_status = mp2797_init(&s_afe_config);
    if (s_afe_status != MP2797_STATUS_OK)
    {
        mp2797_shutdown();
        return s_afe_status;
    }

    s_afe_sampling_enabled = true;
    s_last_sample_ms = mwTick - BMS_VOLTAGE_SAMPLE_PERIOD_MS;
    return MP2797_STATUS_OK;
}

void bms_app_stop_afe(void)
{
    s_afe_sampling_enabled = false;
    cell_balance_manual_stop(mwTick);
    mp2797_shutdown();
    s_voltages.valid = false;
    s_afe_status = MP2797_STATUS_NOT_READY;
}

cell_balance_status_t bms_app_start_balance(uint8_t cell_number)
{
    if ((s_afe_status != MP2797_STATUS_OK) || !s_voltages.valid)
    {
        return CELL_BALANCE_STATUS_VOLTAGE_INVALID;
    }

    return cell_balance_manual_start(cell_number,
                                     s_voltages.cell_mv,
                                     s_voltages.cell_count,
                                     mwTick);
}

void bms_app_stop_balance(void)
{
    cell_balance_manual_stop(mwTick);
}

mp2797_status_t bms_app_get_afe_status(void)
{
    return s_afe_status;
}

const mp2797_cell_voltages_t *bms_app_get_voltages(void)
{
    return &s_voltages;
}

const cell_balance_state_t *bms_app_get_balance_state(void)
{
    return cell_balance_get_state();
}
