#ifndef FREERTOS_APP_H
#define FREERTOS_APP_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 创建 Console 和 AFE 两个任务；必须在启动调度器之前调用。 */
bool freertos_app_init(void);

/* UART4 接收中断在字节写入环形缓冲区后调用，用于立即唤醒 Console。 */
void freertos_app_notify_console_from_isr(void);

#ifdef __cplusplus
}
#endif

#endif /* FREERTOS_APP_H */
