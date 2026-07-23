#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#include <stddef.h>
#include <stdint.h>

/* SystemCoreClock is updated from the real RCC registers before the scheduler starts. */
extern uint32_t SystemCoreClock;

#define configUSE_PREEMPTION                         1
#define configUSE_PORT_OPTIMISED_TASK_SELECTION      1
#define configUSE_TIME_SLICING                       1
#define configUSE_IDLE_HOOK                          0
#define configUSE_TICK_HOOK                          0
#define configCPU_CLOCK_HZ                           (SystemCoreClock)
#define configTICK_RATE_HZ                           ((TickType_t)1000u)
#define configMAX_PRIORITIES                         5
#define configMINIMAL_STACK_SIZE                     ((uint16_t)128u)
#define configTOTAL_HEAP_SIZE                        ((size_t)(16u * 1024u))
#define configMAX_TASK_NAME_LEN                      16
#define configUSE_16_BIT_TICKS                       0
#define configIDLE_SHOULD_YIELD                      1
#define configUSE_TASK_NOTIFICATIONS                 1
#define configUSE_MUTEXES                            1
#define configUSE_RECURSIVE_MUTEXES                  0
#define configUSE_COUNTING_SEMAPHORES                0
#define configQUEUE_REGISTRY_SIZE                    0
#define configUSE_TRACE_FACILITY                     0
#define configUSE_APPLICATION_TASK_TAG               0
#define configGENERATE_RUN_TIME_STATS                0
#define configUSE_NEWLIB_REENTRANT                   0

#define configSUPPORT_STATIC_ALLOCATION              0
#define configSUPPORT_DYNAMIC_ALLOCATION             1
#define configUSE_MALLOC_FAILED_HOOK                 1
#define configCHECK_FOR_STACK_OVERFLOW               2

#define configUSE_TIMERS                             0
#define configUSE_CO_ROUTINES                        0

#define INCLUDE_vTaskDelay                           1
#define INCLUDE_vTaskDelayUntil                      1
#define INCLUDE_vTaskSuspend                         1
#define INCLUDE_xTaskGetSchedulerState               1
#define INCLUDE_vTaskDelete                          0
#define INCLUDE_vTaskPrioritySet                     0
#define INCLUDE_uxTaskPriorityGet                    0
#define INCLUDE_xTaskGetCurrentTaskHandle            0

#ifdef __NVIC_PRIO_BITS
#define configPRIO_BITS                              __NVIC_PRIO_BITS
#else
#define configPRIO_BITS                              4
#endif

/*
 * UART4 uses preemption priority 5 and calls a FreeRTOS FromISR API.
 * Numerically smaller interrupt priorities than 5 must not call FreeRTOS APIs.
 */
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY      15
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY 5
#define configKERNEL_INTERRUPT_PRIORITY              \
    (configLIBRARY_LOWEST_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))
#define configMAX_SYSCALL_INTERRUPT_PRIORITY         \
    (configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))

void bms_freertos_assert_failed(const char *file, uint32_t line);

#define configASSERT(condition)                                            \
    do                                                                    \
    {                                                                     \
        if ((condition) == 0)                                             \
        {                                                                 \
            bms_freertos_assert_failed(__FILE__, (uint32_t)__LINE__);      \
        }                                                                 \
    } while (0)

/* FreeRTOS owns these exception vectors directly. SysTick keeps a local wrapper. */
#define vPortSVCHandler    SVC_Handler
#define xPortPendSVHandler PendSV_Handler

#endif /* FREERTOS_CONFIG_H */
