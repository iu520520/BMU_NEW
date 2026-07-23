#include "bms_app.h"

#include <string.h>

#include "FreeRTOS.h"
#include "semphr.h"

#include "n32g45x_cfg.h"
#include "console.h"
#include "pc_uart.h"
#include "rs232.h"

#define BMS_VOLTAGE_SAMPLE_PERIOD_MS 1000u

extern __IO uint32_t mwTick;

static mp2797_status_t s_afe_status = MP2797_STATUS_NOT_READY;
static mp2797_config_t s_afe_config;
static mp2797_cell_voltages_t s_published_voltages;
static mp2797_cell_voltages_t s_sample_voltages;
static bool s_afe_sampling;
static uint32_t s_last_sample_ms;
static SemaphoreHandle_t s_balance_mutex;

static uint32_t bms_app_enter_critical(void)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    return primask;
}

static void bms_app_exit_critical(uint32_t primask)
{
    if (primask == 0u)
    {
        __enable_irq();
    }
}

static bool bms_app_balance_lock(void)
{
    if (s_balance_mutex == NULL)
    {
        return false;
    }

    return xSemaphoreTake(s_balance_mutex, portMAX_DELAY) == pdTRUE;
}

static void bms_app_balance_unlock(void)
{
    (void)xSemaphoreGive(s_balance_mutex);
}

static void bms_app_set_sampling(bool sampling)
{
    uint32_t primask = bms_app_enter_critical();
    s_afe_sampling = sampling;
    bms_app_exit_critical(primask);
}

static void bms_app_publish_success(void)
{
    uint32_t primask = bms_app_enter_critical();
    s_published_voltages = s_sample_voltages;
    s_afe_status = MP2797_STATUS_OK;
    s_afe_sampling = false;
    bms_app_exit_critical(primask);
}

static void bms_app_handle_afe_failure(mp2797_status_t status,
                                       uint32_t now_ms)
{
    uint32_t primask = bms_app_enter_critical();
    s_afe_status = status;
    s_afe_sampling = false;
    s_published_voltages.valid = false;
    bms_app_exit_critical(primask);

    if (bms_app_balance_lock())
    {
        cell_balance_stop(CELL_BALANCE_STOP_AFE_ERROR, now_ms);
        bms_app_balance_unlock();
    }
}

void bms_app_init(void)
{
    cell_balance_config_t balance_config;

    rs232_init();
    pc_uart_init();
    console_init();

    cell_balance_get_default_config(&balance_config);
    (void)cell_balance_init(&balance_config);

    mp2797_get_default_config(&s_afe_config);
    memset(&s_published_voltages, 0, sizeof(s_published_voltages));
    memset(&s_sample_voltages, 0, sizeof(s_sample_voltages));
    s_afe_status = MP2797_STATUS_NOT_READY;
    s_afe_sampling = false;
    s_last_sample_ms = mwTick - BMS_VOLTAGE_SAMPLE_PERIOD_MS;
    s_balance_mutex = NULL;
}

bool bms_app_rtos_init(void)
{
    if (s_balance_mutex != NULL)
    {
        return true;
    }

    s_balance_mutex = xSemaphoreCreateMutex();
    return s_balance_mutex != NULL;
}

void bms_app_console_task(void)
{
    console_task();
}

void bms_app_afe_task(void)
{
    uint32_t now_ms;
    mp2797_status_t status;

    /* 原有隔离RS232接口仍保留轮询，AFE状态机不会再阻塞Console。 */
    rs232_poll();
    now_ms = mwTick;

    if (mp2797_sample_is_active())
    {
        status = mp2797_sample_process(now_ms, &s_sample_voltages);
        if (status == MP2797_STATUS_BUSY)
        {
            return;
        }

        if ((status != MP2797_STATUS_OK) || !s_sample_voltages.valid)
        {
            bms_app_handle_afe_failure(status, now_ms);
            return;
        }

        bms_app_publish_success();

        if (bms_app_balance_lock())
        {
            (void)cell_balance_process(s_sample_voltages.cell_mv,
                                       s_sample_voltages.cell_count,
                                       now_ms);
            bms_app_balance_unlock();
        }
        return;
    }

    if ((uint32_t)(now_ms - s_last_sample_ms)
        < BMS_VOLTAGE_SAMPLE_PERIOD_MS)
    {
        return;
    }
    s_last_sample_ms = now_ms;

    if (!mp2797_is_ready())
    {
        /*
         * 初始化也放在低优先级AFE任务中。初始化期间标记为正在处理，
         * Console收到字符后仍可抢占，且不会误报上一轮错误。
         */
        bms_app_set_sampling(true);
        status = mp2797_init(&s_afe_config);
        if (status != MP2797_STATUS_OK)
        {
            bms_app_handle_afe_failure(status, now_ms);
            return;
        }
    }

    status = mp2797_sample_begin();
    if ((status == MP2797_STATUS_OK) || (status == MP2797_STATUS_BUSY))
    {
        bms_app_set_sampling(true);
        return;
    }

    bms_app_handle_afe_failure(status, now_ms);
}

cell_balance_status_t bms_app_start_balance(uint8_t cell_number)
{
    bms_app_afe_snapshot_t snapshot;
    cell_balance_status_t status;

    if (!bms_app_balance_lock())
    {
        return CELL_BALANCE_STATUS_NOT_INITIALIZED;
    }

    /*
     * 必须在取得均衡互斥锁后再读取快照，防止等待锁期间AFE已经报错，
     * 却仍然依据旧电压重新启动功率通道。
     */
    bms_app_get_afe_snapshot(&snapshot);
    if ((snapshot.status != MP2797_STATUS_OK)
        || !snapshot.voltages.valid)
    {
        bms_app_balance_unlock();
        return CELL_BALANCE_STATUS_VOLTAGE_INVALID;
    }

    status = cell_balance_manual_start(cell_number,
                                       snapshot.voltages.cell_mv,
                                       snapshot.voltages.cell_count,
                                       mwTick);
    bms_app_balance_unlock();
    return status;
}

void bms_app_stop_balance(void)
{
    if (bms_app_balance_lock())
    {
        cell_balance_manual_stop(mwTick);
        bms_app_balance_unlock();
    }
}

void bms_app_get_afe_snapshot(bms_app_afe_snapshot_t *snapshot)
{
    uint32_t primask;

    if (snapshot == NULL)
    {
        return;
    }

    primask = bms_app_enter_critical();
    snapshot->status = s_afe_status;
    snapshot->sampling = s_afe_sampling;
    snapshot->voltages = s_published_voltages;
    bms_app_exit_critical(primask);
}
