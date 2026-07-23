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

static void bms_app_handle_afe_failure(mp2797_status_t status,
                                       uint32_t now_ms)
{
    s_afe_status = status;
    s_voltages.valid = false;
    cell_balance_stop(CELL_BALANCE_STOP_AFE_ERROR, now_ms);
}

void bms_app_init(void)
{
    rs232_init();
    pc_uart_init();
    console_init();

    cell_balance_config_t balance_config;
    cell_balance_get_default_config(&balance_config);
    (void)cell_balance_init(&balance_config);

    mp2797_get_default_config(&s_afe_config);
    s_voltages.valid = false;
    s_afe_status = mp2797_init(&s_afe_config);
    s_last_sample_ms = mwTick - BMS_VOLTAGE_SAMPLE_PERIOD_MS;
}

void bms_app_task(void)
{
    /* RS232仍采用轮询；PC调试串口已经由UART4接收中断写入环形缓冲区。 */
    rs232_poll();
    console_task();

    uint32_t now_ms = mwTick;

    /*
     * 每次主循环只推进采样状态机一步。BUSY仅表示本轮采样尚未完成，
     * 不是AFE故障，因此保留上一帧状态和值，并马上把执行权交还主循环。
     */
    if (mp2797_sample_is_active())
    {
        mp2797_status_t sample_status =
            mp2797_sample_process(now_ms, &s_voltages);

        if (sample_status == MP2797_STATUS_BUSY)
        {
            return;
        }

        if ((sample_status != MP2797_STATUS_OK) || !s_voltages.valid)
        {
            bms_app_handle_afe_failure(sample_status, now_ms);
            return;
        }

        s_afe_status = MP2797_STATUS_OK;
        (void)cell_balance_process(s_voltages.cell_mv,
                                   s_voltages.cell_count,
                                   now_ms);
        return;
    }

    if ((uint32_t)(now_ms - s_last_sample_ms) < BMS_VOLTAGE_SAMPLE_PERIOD_MS)
    {
        return;
    }
    s_last_sample_ms = now_ms;

    if (!mp2797_is_ready())
    {
        /* AFE 始终保持唤醒；初始化失败时每秒重新尝试建立通信。 */
        s_afe_status = mp2797_init(&s_afe_config);
        if (s_afe_status != MP2797_STATUS_OK)
        {
            bms_app_handle_afe_failure(s_afe_status, now_ms);
            return;
        }
    }

    mp2797_status_t start_status = mp2797_sample_begin();
    if ((start_status == MP2797_STATUS_OK)
        || (start_status == MP2797_STATUS_BUSY))
    {
        return;
    }

    bms_app_handle_afe_failure(start_status, now_ms);
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

bool bms_app_is_afe_sampling(void)
{
    return mp2797_sample_is_active();
}

const mp2797_cell_voltages_t *bms_app_get_voltages(void)
{
    return &s_voltages;
}

const cell_balance_state_t *bms_app_get_balance_state(void)
{
    return cell_balance_get_state();
}
