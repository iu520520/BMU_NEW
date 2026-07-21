#ifndef MP2797_H
#define MP2797_H

#include <stdbool.h>
#include <stdint.h>

#define MP2797_MIN_CELL_COUNT          7u
#define MP2797_MAX_CELL_COUNT          16u
#define MP2797_DEFAULT_I2C_ADDRESS     0x01u
#define MP2797_DEFAULT_SCAN_POLL_LIMIT 100u

typedef enum
{
    MP2797_STATUS_OK = 0,
    MP2797_STATUS_ERROR,
    MP2797_STATUS_TIMEOUT,//I2C通信超时，ADC采样超时
    MP2797_STATUS_INVALID_ARG,//空指针，电芯数量超出范围，I2C地址超过7位
    MP2797_STATUS_NOT_READY,//通信配置无效，初始化未完成
    MP2797_STATUS_NACK,//I2C通信无应答
    MP2797_STATUS_BUS_BUSY,//I2C总线繁忙
    MP2797_STATUS_CRC_ERROR,//CRC错误
    MP2797_STATUS_SCAN_ERROR,//ADC 扫描状态寄存器报错
    MP2797_STATUS_BUSY,//MP2797内部功能正忙
    MP2797_STATUS_CONFIG_MISMATCH,//通信配置不一致，写寄存器后读回不一致
} mp2797_status_t;

typedef enum
{
    MP2797_SCAN_STATE_IDLE = 0,
    MP2797_SCAN_STATE_BUSY,
    MP2797_SCAN_STATE_DONE,
    MP2797_SCAN_STATE_ERROR,
} mp2797_scan_state_t;

typedef void (*mp2797_delay_us_fn_t)(uint32_t delay_us);

typedef struct
{
    
    uint8_t cell_count;//MP2797 支持 7～16 节串联电芯。

    
    uint8_t i2c_address;//设置为 0 时使用 MP2797_DEFAULT_I2C_ADDRESS。 

   
    uint16_t scan_poll_limit; // 设置为 0 时使用 MP2797_DEFAULT_SCAN_POLL_LIMIT。 

    
    bool use_crc;//必须与实际芯片中的 COMM_CFG.USE_COMM_CRC 配置一致。

    /*
     * 设置为 true 时，初始化函数会应用并校验以下易失性台架测试配置：
     * - 使能高分辨率单体电压和 VTOP 电压扫描；
     * - 禁用同步电流采样、库仑计数和均衡功能；
     * - 禁用单体/电池组欠压、过压、坏电芯及电压不匹配保护动作。
     *
     * 此配置不能使整块电路板直接达到安全量产 BMS 的要求。
     */
    bool voltage_only_mode;

    /* 可选的延时回调函数；设置为 NULL 时使用内部阻塞式延时。 */
    mp2797_delay_us_fn_t delay_us;
} mp2797_config_t;

typedef struct
{
    uint16_t cell_raw[MP2797_MAX_CELL_COUNT];
    uint16_t cell_mv[MP2797_MAX_CELL_COUNT];
    uint16_t pack_raw;
    uint32_t pack_mv;
    uint32_t cell_sum_mv;
    uint8_t cell_count;
    bool valid;
} mp2797_cell_voltages_t;

/* 填充适用于本 16 节电芯项目的第一阶段安全配置。 */
void mp2797_get_default_config(mp2797_config_t *config);

/* 唤醒 AFE、验证通信，并应用选定的易失性配置。 */
mp2797_status_t mp2797_init(const mp2797_config_t *config);

/* 读取不会改变芯片状态的状态寄存器，用于确认 AFE 能够正常应答。 */
mp2797_status_t mp2797_probe(void);

/* 重新应用仅电压采样的易失性配置，并回读确认配置是否生效。 */
mp2797_status_t mp2797_configure_voltage_only(void);

/* 只读诊断接口；不受限制的寄存器写入函数仍保持为驱动内部私有接口。 */
mp2797_status_t mp2797_read_register(uint8_t reg_addr, uint16_t *value);

mp2797_status_t mp2797_start_voltage_scan(void);
mp2797_status_t mp2797_get_voltage_scan_state(mp2797_scan_state_t *state);
mp2797_status_t mp2797_wait_voltage_scan(void);

/* cell_number 从 1 开始计数，范围为 1～已配置的 cell_count。 */
mp2797_status_t mp2797_read_cell_voltage(uint8_t cell_number,
                                         uint16_t *raw,
                                         uint16_t *millivolts);
mp2797_status_t mp2797_read_pack_voltage(uint16_t *raw, uint32_t *millivolts);

/* 启动一次扫描，在限定超时时间内等待完成，然后输出一份完整电压快照。 */
mp2797_status_t mp2797_read_cell_voltages(mp2797_cell_voltages_t *voltages);

uint16_t mp2797_cell_raw_to_mv(uint16_t raw);
uint32_t mp2797_pack_raw_to_mv(uint16_t raw);

/* 将 nSHDN 拉低，并将当前驱动状态标记为无效。 */
void mp2797_shutdown(void);
bool mp2797_is_ready(void);

#endif /* MP2797_H */
