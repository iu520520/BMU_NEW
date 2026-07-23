#ifndef BMS_APP_H
#define BMS_APP_H

#include <stdbool.h>
#include <stdint.h>

#include "cell_balance.h"
#include "mp2797.h"

#ifdef __cplusplus
extern "C" {
#endif

void bms_app_init(void);
void bms_app_task(void);

/*
 * 测试阶段采用纯手动均衡。只有调用此接口才会开启指定通道，范围为1～16。
 * 启动前必须已经取得一组有效的AFE单体电压。
 */
cell_balance_status_t bms_app_start_balance(uint8_t cell_number);

/* 手动停止当前均衡通道。 */
void bms_app_stop_balance(void);

mp2797_status_t bms_app_get_afe_status(void);
const mp2797_cell_voltages_t *bms_app_get_voltages(void);
const cell_balance_state_t *bms_app_get_balance_state(void);

#ifdef __cplusplus
}
#endif

#endif /* BMS_APP_H */
