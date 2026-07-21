#include "cell_balance.h"

#include <stddef.h>
#include <string.h>

#include "board_pins.h"
#include "n32g45x_cfg.h"

#define CELL_BALANCE_SWITCH_DEADTIME_MS 20u
#define CELL_BALANCE_PATH_SETTLE_MS      2u

typedef enum
{
    BALANCE_POWER_LOW_CELLS = 0,//单体1-8节电池，对应U101
    BALANCE_POWER_HIGH_CELLS,  //单体9-16节电池，对应U100
} balance_power_group_t;

static cell_balance_config_t s_config;
static cell_balance_state_t s_state;

static void cell_balance_write_pin(GPIO_Module *port, uint16_t pin, bool high)
{
    if (high)
    {
        GPIO_SetBits(port, pin);
    }
    else
    {
        GPIO_ResetBits(port, pin);
    }
}

static void cell_balance_power_off(void)
{
    GPIO_ResetBits(BOARD_BALANCE_LOW_PWR_PORT, BOARD_BALANCE_LOW_PWR_PIN);//对应原理图的U104和U105
    GPIO_ResetBits(BOARD_BALANCE_HIGH_PWR_PORT, BOARD_BALANCE_HIGH_PWR_PIN);
}


/*U100和U101的使能端*/
static void cell_balance_mux_disable(void)
{
    GPIO_SetBits(BOARD_BALANCE_MUX_EN_PORT, BOARD_BALANCE_MUX_EN_PIN);
}

static void cell_balance_mux_enable(void)
{
    GPIO_ResetBits(BOARD_BALANCE_MUX_EN_PORT, BOARD_BALANCE_MUX_EN_PIN);
}




static void cell_balance_set_low_address(uint8_t address)//U100的三个地址
{
    cell_balance_write_pin(BOARD_BALANCE_LOW_A_PORT,BOARD_BALANCE_LOW_A_PIN,(address & 0x01u) != 0u);
    cell_balance_write_pin(BOARD_BALANCE_LOW_B_PORT,BOARD_BALANCE_LOW_B_PIN,(address & 0x02u) != 0u);
    cell_balance_write_pin(BOARD_BALANCE_LOW_C_PORT,BOARD_BALANCE_LOW_C_PIN,(address & 0x04u) != 0u);
}

static void cell_balance_set_high_address(uint8_t address)//U101的三个地址
{
    cell_balance_write_pin(BOARD_BALANCE_HIGH_A_PORT,BOARD_BALANCE_HIGH_A_PIN,(address & 0x01u) != 0u);
    cell_balance_write_pin(BOARD_BALANCE_HIGH_B_PORT,BOARD_BALANCE_HIGH_B_PIN,(address & 0x02u) != 0u);
    cell_balance_write_pin(BOARD_BALANCE_HIGH_C_PORT,BOARD_BALANCE_HIGH_C_PIN,(address & 0x04u) != 0u);
}

static void cell_balance_hw_stop(bool wait_for_energy_release)
{
    /* 先停反激电源，给变压器和输出通道留出释放能量的时间，再断开通道。 */
    cell_balance_power_off();
    if (wait_for_energy_release)//等待变压器残余能量释放，如果之前在均衡的话，就等待20ms
    {
        SysTick_Delayms(CELL_BALANCE_SWITCH_DEADTIME_MS);
    }

    cell_balance_mux_disable();//把模拟开关的电源和地址清零
    cell_balance_set_low_address(0u);
    cell_balance_set_high_address(0u);
}

static void cell_balance_hw_start(uint8_t cell_number)//cell_number为开启的电池编号，1-16
{
    balance_power_group_t group;
    uint8_t address;

    /* 即使上层状态异常，也不允许在旧功率级仍开启时直接切换地址。 */
    cell_balance_power_off();
    cell_balance_mux_disable();

    if (cell_number <= 8u)//判断是在高8节还是低8节，并配置地址
    {
        group = BALANCE_POWER_LOW_CELLS;
        address = (uint8_t)(cell_number - 1u);
    }
    else
    {
        group = BALANCE_POWER_HIGH_CELLS;
        address = (uint8_t)(cell_number - 9u);
    }

    /*
     * U101 的 X0～X7 对应单体 1～8；U100 的 X0～X7 对应单体 9～16。
     * PC7 会同时选通两片模拟开关，因此必须只启动目标组的 ME8320 电源级。
     */
    cell_balance_set_low_address((group == BALANCE_POWER_LOW_CELLS) ? address : 0u);
    cell_balance_set_high_address((group == BALANCE_POWER_HIGH_CELLS) ? address : 0u);
    cell_balance_mux_enable();
    SysTick_Delayms(CELL_BALANCE_PATH_SETTLE_MS);

    if (group == BALANCE_POWER_LOW_CELLS)
    {
        GPIO_SetBits(BOARD_BALANCE_LOW_PWR_PORT, BOARD_BALANCE_LOW_PWR_PIN);
    }
    else
    {
        GPIO_SetBits(BOARD_BALANCE_HIGH_PWR_PORT, BOARD_BALANCE_HIGH_PWR_PIN);
    }
}

static bool cell_balance_config_is_valid(const cell_balance_config_t *config)//检查均衡参数配置是否合理
{
    if ((config == NULL)
        || (config->cell_count == 0u)
        || (config->cell_count > CELL_BALANCE_MAX_CELL_COUNT)
        || (config->valid_min_mv >= config->valid_max_mv)
        || (config->balance_ceiling_mv <= config->valid_min_mv)
        || (config->balance_ceiling_mv > config->valid_max_mv)
        || (config->max_continuous_ms == 0u))
    {
        return false;
    }

    return true;
}


/*检查一组单体电压是否有效，并计算当前最小电压、最大电压、压差。*/
static cell_balance_status_t cell_balance_update_voltages(const uint16_t *cell_mv,
                                                          uint8_t cell_count,
                                                          uint32_t now_ms)
{
    uint16_t min_mv;
    uint16_t max_mv;//记录电池组里的最低电压和最高电压

    if ((cell_mv == NULL) || (cell_count != s_config.cell_count))//没有电压数据or电池数和配置数不一致
    {
        cell_balance_stop(CELL_BALANCE_STOP_VOLTAGE_INVALID, now_ms);//停止当前均衡并返回错误
        return CELL_BALANCE_STATUS_INVALID_ARG;
    }

    min_mv = cell_mv[0];
    max_mv = cell_mv[0];
    for (uint8_t index = 0u; index < cell_count; index++)
    {
        uint16_t voltage = cell_mv[index];
        if ((voltage < s_config.valid_min_mv) || (voltage > s_config.valid_max_mv))//如果电压异常，立即停止均衡并返回电压无效
        {
            cell_balance_stop(CELL_BALANCE_STOP_VOLTAGE_INVALID, now_ms);
            return CELL_BALANCE_STATUS_VOLTAGE_INVALID;
        }

        if (voltage < min_mv)//更新最高和最低电压
        {
            min_mv = voltage;
        }
        if (voltage > max_mv)
        {
            max_mv = voltage;
        }
    }

    s_state.min_cell_mv = min_mv;
    s_state.max_cell_mv = max_mv;
    s_state.delta_mv = (uint16_t)(max_mv - min_mv);
    return CELL_BALANCE_STATUS_OK;
}


void cell_balance_get_default_config(cell_balance_config_t *config)
{
    if (config == NULL)
    {
        return;
    }

    config->cell_count = CELL_BALANCE_MAX_CELL_COUNT;
    config->valid_min_mv = 1500u;
    config->valid_max_mv = 2600u;
    config->balance_ceiling_mv = 2350u;
    config->max_continuous_ms = 10u * 60u * 1000u;//10min
}


/*初始化均衡模块*/
cell_balance_status_t cell_balance_init(const cell_balance_config_t *config)
{
    if (!cell_balance_config_is_valid(config))//检查配置是否合法
    {
        cell_balance_power_off();
        cell_balance_mux_disable();
        return CELL_BALANCE_STATUS_INVALID_ARG;
    }

    s_config = *config;
    memset(&s_state, 0, sizeof(s_state));//清空运行状态
    cell_balance_hw_stop(false);//均衡停止

    s_state.initialized = true;//表示均衡模块已经成功初始化
    s_state.last_stop_reason = CELL_BALANCE_STOP_NONE;//当前没有停止故障或停止原因
    return CELL_BALANCE_STATUS_OK;
}

/*强制停止均衡并记录原因*/
void cell_balance_stop(cell_balance_stop_reason_t reason, uint32_t now_ms)
{
    bool was_active = s_state.active;//先记录当前状态

    cell_balance_hw_stop(was_active);//关闭硬件
    s_state.active = false;
    s_state.active_cell = 0u;
    s_state.active_since_ms = 0u;
    s_state.last_stop_reason = reason;
    (void)now_ms;
}


/*手动指定某节电池开始均衡*/
cell_balance_status_t cell_balance_manual_start(uint8_t cell_number,//（要启动均衡的单体编号，所有单体电压数组，单体数量，系统时间）
                                                const uint16_t *cell_mv,
                                                uint8_t cell_count,
                                                uint32_t now_ms)
{
    cell_balance_status_t status;

    if (!s_state.initialized)//检测模块是否初始化
    {
        return CELL_BALANCE_STATUS_NOT_INITIALIZED;
    }
    if ((cell_number == 0u) || (cell_number > s_config.cell_count))//单体编号是否在范围内
    {
        return CELL_BALANCE_STATUS_INVALID_ARG;
    }

    status = cell_balance_update_voltages(cell_mv, cell_count, now_ms);//检查更新所有单体电压
    if (status != CELL_BALANCE_STATUS_OK)
    {
        return status;
    }

    if (cell_mv[cell_number - 1u] >= s_config.balance_ceiling_mv)//检查单体电压是否达到均衡上限
    {
        cell_balance_stop(CELL_BALANCE_STOP_CELL_LIMIT, now_ms);
        return CELL_BALANCE_STATUS_CELL_LIMIT;
    }

    if (s_state.active && (s_state.active_cell == cell_number))//是否与上一阶段均衡的电池为同一节
    {
        return CELL_BALANCE_STATUS_OK;
    }

    if (s_state.active)//切换目标应关断后重启
    {
        cell_balance_hw_stop(true);
        s_state.active = false;
        s_state.active_cell = 0u;
        s_state.active_since_ms = 0u;
    }

    cell_balance_hw_start(cell_number);
    s_state.active = true;
    s_state.active_cell = cell_number;
    s_state.active_since_ms = now_ms;
    s_state.last_stop_reason = CELL_BALANCE_STOP_NONE;
    return CELL_BALANCE_STATUS_OK;
}
  
void cell_balance_manual_stop(uint32_t now_ms)
{
    if (!s_state.initialized)
    {
        return;
    }

    cell_balance_stop(CELL_BALANCE_STOP_COMMAND, now_ms);
}

cell_balance_status_t cell_balance_process(const uint16_t *cell_mv,
                                           uint8_t cell_count,
                                           uint32_t now_ms)//均衡模式下的周期检查函数
{
    cell_balance_status_t status;

    if (!s_state.initialized)
    {
        return CELL_BALANCE_STATUS_NOT_INITIALIZED;
    }

    status = cell_balance_update_voltages(cell_mv, cell_count, now_ms);//更新并检查单体电压
    if (status != CELL_BALANCE_STATUS_OK)
    {
        return status;
    }

    /* 手动测试模式下，本函数绝不会根据压差自动开启任何通道。 */
    if (!s_state.active)//没有在均衡就直接返回
    {
        return CELL_BALANCE_STATUS_OK;
    }

    if ((s_state.active_cell == 0u) || (s_state.active_cell > cell_count))//单体编号是否合法
    {
        cell_balance_stop(CELL_BALANCE_STOP_VOLTAGE_INVALID, now_ms);
        return CELL_BALANCE_STATUS_INVALID_ARG;
    }

    if (cell_mv[s_state.active_cell - 1u] >= s_config.balance_ceiling_mv)//如果目标单体电池超过了电压上限，停止均衡
    {
        cell_balance_stop(CELL_BALANCE_STOP_CELL_LIMIT, now_ms);
        return CELL_BALANCE_STATUS_CELL_LIMIT;
    }

    if ((uint32_t)(now_ms - s_state.active_since_ms) >= s_config.max_continuous_ms)//检查最长运行时间
    {
        cell_balance_stop(CELL_BALANCE_STOP_TIME_LIMIT, now_ms);
    }

    return CELL_BALANCE_STATUS_OK;
}

const cell_balance_state_t *cell_balance_get_state(void)
{
    return &s_state;
}
