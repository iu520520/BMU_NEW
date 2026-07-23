#include "freertos_app.h"

#include <stdint.h>

#include "FreeRTOS.h"
#include "task.h"

#include "bms_app.h"

#define BMS_CONSOLE_TASK_PRIORITY   (tskIDLE_PRIORITY + 3u)
#define BMS_AFE_TASK_PRIORITY       (tskIDLE_PRIORITY + 2u)
#define BMS_CONSOLE_STACK_WORDS     768u
#define BMS_AFE_STACK_WORDS         512u
#define BMS_CONSOLE_IDLE_WAIT_MS    10u
#define BMS_AFE_STEP_PERIOD_MS      1u

static TaskHandle_t s_console_task_handle;
static volatile bool s_console_task_started;

static void freertos_console_task(void *argument)
{
    (void)argument;
    s_console_task_started = true;

    for (;;)
    {
        /*
         * 先处理环形缓冲区中的全部字符，再阻塞等待UART中断通知。
         * 10 ms超时用于继续检查每秒一次的AFE电压打印。
         */
        bms_app_console_task();
        (void)ulTaskNotifyTake(pdTRUE,
                               pdMS_TO_TICKS(BMS_CONSOLE_IDLE_WAIT_MS));
    }
}

static void freertos_afe_task(void *argument)
{
    (void)argument;

    for (;;)
    {
        /* MP2797每次只推进一个状态，随后主动让出CPU。 */
        bms_app_afe_task();
        vTaskDelay(pdMS_TO_TICKS(BMS_AFE_STEP_PERIOD_MS));
    }
}

bool freertos_app_init(void)
{
    BaseType_t result;

    s_console_task_handle = NULL;
    s_console_task_started = false;

    if (!bms_app_rtos_init())
    {
        return false;
    }

    result = xTaskCreate(freertos_console_task,
                         "Console",
                         BMS_CONSOLE_STACK_WORDS,
                         NULL,
                         BMS_CONSOLE_TASK_PRIORITY,
                         &s_console_task_handle);
    if (result != pdPASS)
    {
        s_console_task_handle = NULL;
        return false;
    }

    result = xTaskCreate(freertos_afe_task,
                         "AFE",
                         BMS_AFE_STACK_WORDS,
                         NULL,
                         BMS_AFE_TASK_PRIORITY,
                         NULL);
    return result == pdPASS;
}

void freertos_app_notify_console_from_isr(void)
{
    BaseType_t higher_priority_task_woken = pdFALSE;

    /*
     * 调度器启动前收到的字符仍保存在pc_uart环形缓冲区中。
     * Console真正开始运行后才调用FromISR接口，避免启动阶段触发PendSV。
     */
    if ((s_console_task_handle == NULL) || !s_console_task_started)
    {
        return;
    }

    vTaskNotifyGiveFromISR(s_console_task_handle,
                           &higher_priority_task_woken);
    portYIELD_FROM_ISR(higher_priority_task_woken);
}

void vApplicationMallocFailedHook(void)
{
    taskDISABLE_INTERRUPTS();
    for (;;)
    {
        __asm volatile ("nop");
    }
}

void vApplicationStackOverflowHook(TaskHandle_t task,
                                   char *task_name)
{
    (void)task;
    (void)task_name;
    taskDISABLE_INTERRUPTS();
    for (;;)
    {
        __asm volatile ("nop");
    }
}

void bms_freertos_assert_failed(const char *file, uint32_t line)
{
    (void)file;
    (void)line;
    taskDISABLE_INTERRUPTS();
    for (;;)
    {
        __asm volatile ("nop");
    }
}
