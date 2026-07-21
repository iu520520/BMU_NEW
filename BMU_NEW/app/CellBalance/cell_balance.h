#ifndef CELL_BALANCE_H
#define CELL_BALANCE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CELL_BALANCE_MAX_CELL_COUNT 16u

typedef enum
{
    CELL_BALANCE_STATUS_OK = 0,
    CELL_BALANCE_STATUS_INVALID_ARG,
    CELL_BALANCE_STATUS_NOT_INITIALIZED,
    CELL_BALANCE_STATUS_VOLTAGE_INVALID,
    CELL_BALANCE_STATUS_CELL_LIMIT,
} cell_balance_status_t;

typedef enum
{
    CELL_BALANCE_STOP_NONE = 0,
    CELL_BALANCE_STOP_COMMAND,
    CELL_BALANCE_STOP_VOLTAGE_INVALID,
    CELL_BALANCE_STOP_CELL_LIMIT,
    CELL_BALANCE_STOP_TIME_LIMIT,
    CELL_BALANCE_STOP_AFE_ERROR,
} cell_balance_stop_reason_t;

typedef struct
{
    uint8_t cell_count;
    uint16_t valid_min_mv;
    uint16_t valid_max_mv;
    uint16_t balance_ceiling_mv;
    uint32_t max_continuous_ms;
} cell_balance_config_t;

typedef struct
{
    bool initialized;
    bool active;
    uint8_t active_cell;
    uint16_t min_cell_mv;
    uint16_t max_cell_mv;
    uint16_t delta_mv;
    uint32_t active_since_ms;
    cell_balance_stop_reason_t last_stop_reason;
} cell_balance_state_t;

/* 得到适用于 2 V 铅碳单体的保守起始参数。本模块为手动测试模式，不会根据压差自动启动均衡。*/
void cell_balance_get_default_config(cell_balance_config_t *config);

/* 初始化后首先强制关闭反激驱动芯片和两片模拟开关*/
cell_balance_status_t cell_balance_init(const cell_balance_config_t *config);

/*
 * 手动启动指定单体的均衡通道。
 * cell_number 的有效范围为 1～cell_count；cell_mv 必须是最近一次有效采样。
 */
cell_balance_status_t cell_balance_manual_start(uint8_t cell_number,
                                                const uint16_t *cell_mv,
                                                uint8_t cell_count,
                                                uint32_t now_ms);

/* 手动停止当前均衡通道。 */
void cell_balance_manual_stop(uint32_t now_ms);

/*更新电压状态并执行安全监测。 此函数只会在电压越界、目标单体达到上限或运行超时时停止均衡，绝不会自动启动。*/
cell_balance_status_t cell_balance_process(const uint16_t *cell_mv,
                                           uint8_t cell_count,
                                           uint32_t now_ms);

/* 发生通信或系统故障时供上层强制关断。 */
void cell_balance_stop(cell_balance_stop_reason_t reason, uint32_t now_ms);

const cell_balance_state_t *cell_balance_get_state(void);

#ifdef __cplusplus
}
#endif

#endif /* CELL_BALANCE_H */
