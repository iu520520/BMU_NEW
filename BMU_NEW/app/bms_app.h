#ifndef BMS_APP_H
#define BMS_APP_H

#include <stdbool.h>
#include <stdint.h>

#include "cell_balance.h"
#include "mp2797.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    mp2797_status_t status;
    bool sampling;
    mp2797_cell_voltages_t voltages;
} bms_app_afe_snapshot_t;

/* 初始化硬件相关应用模块，但不在调度器启动前访问AFE。 */
void bms_app_init(void);

/* 创建应用层互斥锁；必须在启动调度器之前完成。 */
bool bms_app_rtos_init(void);

/* 分别由高优先级Console任务和低优先级AFE任务调用。 */
void bms_app_console_task(void);
void bms_app_afe_task(void);

/*
 * 测试阶段采用纯手动均衡。只有调用此接口才会开启指定通道，
 * cell_number的有效范围为1～16，并且必须已经取得有效AFE电压。
 */
cell_balance_status_t bms_app_start_balance(uint8_t cell_number);

/* 手动停止当前均衡通道。 */
void bms_app_stop_balance(void);

/* 原子复制AFE状态和完整电压帧，避免Console读到一半新、一半旧的数据。 */
void bms_app_get_afe_snapshot(bms_app_afe_snapshot_t *snapshot);

#ifdef __cplusplus
}
#endif

#endif /* BMS_APP_H */
