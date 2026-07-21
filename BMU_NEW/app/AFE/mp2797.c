#include <mp2797.h>

#include <stddef.h>
#include <string.h>

#include <board_pins.h>
#include <n32g45x_gpio.h>
#include <soft_i2c.h>
#include <system_n32g45x.h>

/* 第一级电压采样驱动使用的 MP2797 寄存器地址。 */
#define MP2797_REG_CELLS_CTRL   0x00u//电芯数量控制寄存器，只修改最低四位，接入8节电池就写入0x7,接入12节电池就写入0xB
#define MP2797_REG_PWR_STATUS   0x01u//电源状态寄存器（只读），读取数据来判断MP2797是否已经被唤醒；MP2797能否响应I2C；基础寄存器读取正常。
                                     /*  bit[4:0]表示 MP2797 当前处于什么电源状态
                                         0x01，安全模式;0x02,待机模式;0x04，活动模式;0x08，故障模式;0x10，恢复模式

                                         bits[9:7]表示 MP2797 判断出的电池包电流方向
                                         0x1电池包正在放电    0x2电流处于待机范围     0x4电池包正在充电
                                     */
#define MP2797_REG_PACKFT_CTRL  0x34u//电池包故障控制寄存器，设置为只采集电压模式
#define MP2797_REG_CELLFT_CTRL  0x35u//单体电芯故障控制寄存器
#define MP2797_REG_RD_VTOP      0x6Au//电压采样结果寄存器，读取VTOP电压结果的寄存器，VTOP为电池组总电压or顶部电压
#define MP2797_REG_RD_VCELL1    0x6Cu//第 1 节电芯电压结果寄存器的起始地址，第一节0x6C，第二节0x6E,第三节0x70
#define MP2797_REG_ADC_CTRL     0x99u//ADC 扫描控制和状态寄存器，bit0：SCAN_GO，启动扫描或表示扫描命令有效；bit1：SCAN_DONE，扫描完成；bit2：SCAN_ERROR，扫描发生错误
#define MP2797_REG_CC_CFG       0x9Au//库仑计配置
#define MP2797_REG_HR_SCAN0     0x9Cu//高分辨率扫描配置
#define MP2797_REG_HR_SCAN1     0x9Du//指定哪些电芯通道参与高分辨率扫描，8节电池就是0x00FF,16节电池就是0xFFFF
#define MP2797_REG_COMM_CFG     0xA3u//通信配置寄存器,bit2为CRC使能位，I2C的7位设备地址位于寄存器的bit14～bit8
#define MP2797_REG_BAL_LIST     0xA5u//电芯均衡列表寄存器，用来选择哪些电芯需要进行均衡
#define MP2797_REG_BAL_CTRL     0xA6u//均衡控制寄存器
#define MP2797_REG_BAL_CFG      0xA7u//均衡模式配置寄存器


/*寄存器配置掩码*/
#define MP2797_CELLS_CTRL_COUNT_MASK       0x000Fu

#define MP2797_PACKFT_VOLTAGE_ONLY_MASK    0x1B33u
#define MP2797_CELLFT_VOLTAGE_ONLY_MASK    0x0036u
/*ADC 扫描控制位*/
#define MP2797_ADC_SCAN_GO                  (1u << 0)
#define MP2797_ADC_SCAN_DONE                (1u << 1)
#define MP2797_ADC_SCAN_ERROR               (1u << 2)

#define MP2797_CC_DISABLE_MASK              ((1u << 0) | (1u << 14))
/*通信配置*/
#define MP2797_HR_SCAN0_CONTROL_MASK        0x037Fu
#define MP2797_HR_SCAN0_VCELL_VTOP_ONLY     ((1u << 0) | (1u << 1))

#define MP2797_COMM_USE_CRC                 (1u << 2)
#define MP2797_COMM_ADDRESS_MASK            0x7F00u
#define MP2797_COMM_ADDRESS_SHIFT           8u
/*均衡控制*/
#define MP2797_BALANCE_GO                   (1u << 0)
#define MP2797_BAL_CFG_MODE_MASK             0x0007u
/*ADC 数据处理*/
#define MP2797_ADC_RESULT_MASK              0x7FFFu
#define MP2797_CELL_REGISTER_STRIDE         2u
/*I2C 地址方向位*/
#define MP2797_I2C_READ_BIT                 0x01u
#define MP2797_I2C_WRITE_BIT                0x00u
/*CRC 多项式*/
#define MP2797_CRC8_POLYNOMIAL              0x07u
/*时序延时*/
#define MP2797_SHUTDOWN_DELAY_US            10000u
#define MP2797_WAKE_DELAY_US                5000u
#define MP2797_FUNCTION_COMMAND_DELAY_US    200u

static mp2797_config_t s_mp2797_config;
static bool s_mp2797_config_valid;
static bool s_mp2797_ready;


/*把i2c的错误状态转换成MP2797的错误状态*/
static mp2797_status_t mp2797_status_from_i2c(soft_i2c_status_t status)
{
    switch (status)
    {
        case SOFT_I2C_STATUS_OK:
            return MP2797_STATUS_OK;

        case SOFT_I2C_STATUS_INVALID_ARG:
            return MP2797_STATUS_INVALID_ARG;

        case SOFT_I2C_STATUS_NACK:
            return MP2797_STATUS_NACK;

        case SOFT_I2C_STATUS_BUS_BUSY:
            return MP2797_STATUS_BUS_BUSY;

        case SOFT_I2C_STATUS_TIMEOUT:
            return MP2797_STATUS_TIMEOUT;

        case SOFT_I2C_STATUS_ERROR:
        default:
            return MP2797_STATUS_ERROR;
    }
}

static void mp2797_default_delay_us(uint32_t delay_us)
{
    uint32_t cycles_per_us = SystemCoreClock / 1000000u;

    if (cycles_per_us == 0u)
    {
        cycles_per_us = 1u;
    }

    while (delay_us-- > 0u)
    {
        volatile uint32_t cycles = cycles_per_us;

        while (cycles-- > 0u)
        {
            __NOP();
        }
    }
}

static void mp2797_delay_us(uint32_t delay_us)
{
    if (s_mp2797_config.delay_us != NULL)
    {
        s_mp2797_config.delay_us(delay_us);
    }
    else
    {
        mp2797_default_delay_us(delay_us);
    }
}

/*更新计算CRC校验值*/
static uint8_t mp2797_crc8_update(uint8_t crc, uint8_t data)
{
    crc ^= data;

    for (uint8_t bit = 0u; bit < 8u; bit++)
    {
        if ((crc & 0x80u) != 0u)
        {
            crc = (uint8_t)((crc << 1u) ^ MP2797_CRC8_POLYNOMIAL);
        }
        else
        {
            crc <<= 1u;
        }
    }

    return crc;
}
/*从 MP2797 的某个 8 位寄存器地址中读取一个 16 位寄存器值*/
static mp2797_status_t mp2797_read_word_internal(uint8_t reg_addr, uint16_t *value)//(要读取的 MP2797 内部寄存器地址,用于保存数据的16位寄存器)
{
    if (value == NULL)//检查指针是否为空
    {
        return MP2797_STATUS_INVALID_ARG;
    }

    if (!s_mp2797_config_valid)//检查配置是否有被初始化保存并确认
    {
        return MP2797_STATUS_NOT_READY;
    }

    uint8_t data[3] = {0u, 0u, 0u};//（寄存器低字节，寄存器高字节，CRC字节）
    size_t length = s_mp2797_config.use_crc ? 3u : 2u;//决定读取几个字节，如果使用crc就是三字节，不使用就是两个字节

    soft_i2c_status_t i2c_status = soft_i2c_read_reg8(s_mp2797_config.i2c_address,
                                                       reg_addr,
                                                       data,
                                                       length);
    mp2797_status_t status = mp2797_status_from_i2c(i2c_status);

    if (status != MP2797_STATUS_OK)
    {
        return status;
    }

    if (s_mp2797_config.use_crc)
    {
        uint8_t crc = 0u;
        uint8_t address_write = (uint8_t)((s_mp2797_config.i2c_address << 1u)
                                          | MP2797_I2C_WRITE_BIT);
        uint8_t address_read = (uint8_t)((s_mp2797_config.i2c_address << 1u)
                                         | MP2797_I2C_READ_BIT);

        /*
         第二次寄存器地址reg_addr虽然没有在 RESTART 后再次真正出现在总线上，
         但计算读取 CRC 时仍然必须加入。这是MP2797的协议特性
         */
        crc = mp2797_crc8_update(crc, address_write);//依次把相关字节加入CRC
        crc = mp2797_crc8_update(crc, reg_addr);
        crc = mp2797_crc8_update(crc, address_read);
        crc = mp2797_crc8_update(crc, reg_addr);
        crc = mp2797_crc8_update(crc, data[0]);
        crc = mp2797_crc8_update(crc, data[1]);

        if (crc != data[2])//判断MCU生成的CRC校验码是否和MP2797发送的校验码一致
        {
            return MP2797_STATUS_CRC_ERROR;
        }
    }

    
    *value = (uint16_t)data[0] | ((uint16_t)data[1] << 8u);
    return MP2797_STATUS_OK;
}

static mp2797_status_t mp2797_write_word_internal(uint8_t reg_addr, uint16_t value)//向MP2797的某八位寄存器地址写16位的数据
{
    if (!s_mp2797_config_valid)
    {
        return MP2797_STATUS_NOT_READY;
    }

    uint8_t data[3];
    data[0] = (uint8_t)(value >> 8u);
    data[1] = (uint8_t)value;
    size_t length = 2u;

    if (s_mp2797_config.use_crc)
    {
        uint8_t crc = 0u;
        uint8_t address_write = (uint8_t)((s_mp2797_config.i2c_address << 1u)
                                          | MP2797_I2C_WRITE_BIT);

        crc = mp2797_crc8_update(crc, address_write);
        crc = mp2797_crc8_update(crc, reg_addr);
        crc = mp2797_crc8_update(crc, data[0]);
        crc = mp2797_crc8_update(crc, data[1]);
        data[2] = crc;
        length = 3u;
    }

    return mp2797_status_from_i2c(soft_i2c_write_reg8(s_mp2797_config.i2c_address,
                                                       reg_addr,
                                                       data,
                                                       length));
}


/*对 MP2797指定寄存器的指定 bit 进行修改，其他 bit 保持不变；写完后重新读取，确认配置确实生效。*/
static mp2797_status_t mp2797_update_and_verify(uint8_t reg_addr,//reg_addr：要修改的MP2797 寄存器
                                                uint16_t mask,//mask：该寄存器中哪些 bit 允许修改，1为修改，0为不变
                                                uint16_t value)//希望这些bit被修改成的值
{
    uint16_t current = 0u;
    mp2797_status_t status = mp2797_read_word_internal(reg_addr, &current);//读取当前寄存器的值

    if (status != MP2797_STATUS_OK)
    {
        return status;
    }

    uint16_t updated = (uint16_t)((current & (uint16_t)(~mask)) | (value & mask));//current & ~mask负责把需要修改的位清零，并保留不需要修改的位；(value & mask)把value写入

    if (updated != current)//如果数据变化的话，写入寄存器
    {
        status = mp2797_write_word_internal(reg_addr, updated);
        if (status != MP2797_STATUS_OK)
        {
            return status;
        }
    }

    uint16_t readback = 0u;
    status = mp2797_read_word_internal(reg_addr, &readback);//写入后再次读取该寄存器
    if (status != MP2797_STATUS_OK)
    {
        return status;
    }

    if ((readback & mask) != (value & mask))//验证指定bit是否正确
    {
        return MP2797_STATUS_CONFIG_MISMATCH;
    }

    return MP2797_STATUS_OK;
}


/*读取MP2797的通信配置寄存器COMM_CFG，检查芯片当前使用的CRC设置和I²C地址，是否与驱动配置一致。*/
static mp2797_status_t mp2797_verify_communication_config(void)
{
    uint16_t comm_cfg = 0u;
    mp2797_status_t status = mp2797_read_word_internal(MP2797_REG_COMM_CFG, &comm_cfg);

    if (status != MP2797_STATUS_OK)
    {
        return status;
    }

    bool device_uses_crc = (comm_cfg & MP2797_COMM_USE_CRC) != 0u;
    if (device_uses_crc != s_mp2797_config.use_crc)//检测芯片是否是用来CRC
    {
        return MP2797_STATUS_CONFIG_MISMATCH;
    }

    uint8_t device_address = (uint8_t)((comm_cfg & MP2797_COMM_ADDRESS_MASK) >> MP2797_COMM_ADDRESS_SHIFT);//提取芯片当前的I2C地址

    if (device_address != s_mp2797_config.i2c_address)//和驱动配置中的地址比较
    {
        return MP2797_STATUS_CONFIG_MISMATCH;
    }

    return MP2797_STATUS_OK;
}


static mp2797_status_t mp2797_apply_voltage_only_config(void)//只进行电压采样的测试模式
{
    mp2797_status_t status;

    /*CELLS_CTRL将八节电池设置为0x7，十六节设置为0xF,低于七节设置为0x7*/
    uint16_t cell_count_code = (uint16_t)(s_mp2797_config.cell_count - 1u);//设置串联的电芯数量
    status = mp2797_update_and_verify(MP2797_REG_CELLS_CTRL,
                                     MP2797_CELLS_CTRL_COUNT_MASK,
                                     cell_count_code);//修改CELLS_CTRL寄存器的最低4位，告诉MP2797实际连接了多少节电池
    if (status != MP2797_STATUS_OK)
    {
        return status;
    }

    /* 停止当前所有均衡动作 */
    status = mp2797_update_and_verify(MP2797_REG_BAL_CTRL,MP2797_BALANCE_GO,0u);//将均衡启动位清零，先停止电芯均衡
    if (status != MP2797_STATUS_OK)
    {
        return status;
    }

    status = mp2797_update_and_verify(MP2797_REG_BAL_LIST, 0xFFFFu, 0u);//清空均衡电芯列表，BAL_LIST寄存器用于指定哪些电芯需要均衡
    if (status != MP2797_STATUS_OK)
    {
        return status;
    }

    status = mp2797_update_and_verify(MP2797_REG_BAL_CFG,MP2797_BAL_CFG_MODE_MASK,0u);//关闭均衡和清除当前均衡模式
    if (status != MP2797_STATUS_OK)
    {
        return status;
    }

    /* 禁用库仑计和其重启模式，库仑计用于计算流入流出的电量 */
    status = mp2797_update_and_verify(MP2797_REG_CC_CFG,MP2797_CC_DISABLE_MASK,0u);
    if (status != MP2797_STATUS_OK)
    {
        return status;
    }

    /*关闭单体电芯电压相关的自动故障动作*/
    status = mp2797_update_and_verify(MP2797_REG_PACKFT_CTRL,MP2797_PACKFT_VOLTAGE_ONLY_MASK,0u);
    if (status != MP2797_STATUS_OK)
    {
        return status;
    }

    status = mp2797_update_and_verify(MP2797_REG_CELLFT_CTRL,
                                     MP2797_CELLFT_VOLTAGE_ONLY_MASK,
                                     0u);
    if (status != MP2797_STATUS_OK)
    {
        return status;
    }

    /*只开启单体电压和VTOP扫描*/
    status = mp2797_update_and_verify(MP2797_REG_HR_SCAN0,MP2797_HR_SCAN0_CONTROL_MASK,MP2797_HR_SCAN0_VCELL_VTOP_ONLY);
    if (status != MP2797_STATUS_OK)
    {
        return status;
    }

    /*设置哪些电芯参与扫描，有几节电芯从最低位数就有几个1*/
    uint16_t enabled_cells = (s_mp2797_config.cell_count == 16u)
                                 ? 0xFFFFu
                                 : (uint16_t)((1u << s_mp2797_config.cell_count) - 1u);//16节电池单独写是因为如果向左移动16位就会导致数据丢失

    return mp2797_update_and_verify(MP2797_REG_HR_SCAN1,0xFFFFu,enabled_cells);
}

void mp2797_get_default_config(mp2797_config_t *config)
{
    if (config == NULL)
    {
        return;
    }

    config->cell_count = MP2797_MAX_CELL_COUNT;
    config->i2c_address = MP2797_DEFAULT_I2C_ADDRESS;
    config->scan_poll_limit = MP2797_DEFAULT_SCAN_POLL_LIMIT;//设置扫描状态轮询次数，默认最多查询100次
    config->use_crc = false;
    config->voltage_only_mode = true;
    config->delay_us = NULL;
}

mp2797_status_t mp2797_init(const mp2797_config_t *config)
{
    if (config == NULL)
    {
        return MP2797_STATUS_INVALID_ARG;
    }

    if ((config->cell_count < MP2797_MIN_CELL_COUNT)
        || (config->cell_count > MP2797_MAX_CELL_COUNT))//检查电芯数量是否在7-16
    {
        return MP2797_STATUS_INVALID_ARG;
    }

    if (config->i2c_address > 0x7Fu)//I2C有效地址为七位，如果超过7位，报错
    {
        return MP2797_STATUS_INVALID_ARG;
    }

    s_mp2797_config = *config;
    if (s_mp2797_config.i2c_address == 0u)
    {
        s_mp2797_config.i2c_address = MP2797_DEFAULT_I2C_ADDRESS;
    }
    if (s_mp2797_config.scan_poll_limit == 0u)
    {
        s_mp2797_config.scan_poll_limit = MP2797_DEFAULT_SCAN_POLL_LIMIT;
    }

    s_mp2797_config_valid = true;//驱动已经拥有有效配置，知道 I2C 地址和 CRC 设置
    s_mp2797_ready = false;//初始化还没有全部成功，暂时不能开始正式采样

    /*首先强制芯片进入确定的关断状态。数据手册规定进入关断状态最长需要 8 ms，此处等待 10 ms 留出余量，然后再等待 5 ms 完成唤醒 */

    GPIO_ResetBits(BOARD_AFE_NSHDN_PORT, BOARD_AFE_NSHDN_PIN);//先关断再唤醒，把MP2797从一个不确定状态复位
    mp2797_delay_us(MP2797_SHUTDOWN_DELAY_US);
    GPIO_SetBits(BOARD_AFE_NSHDN_PORT, BOARD_AFE_NSHDN_PIN);
    mp2797_delay_us(MP2797_WAKE_DELAY_US);

    soft_i2c_init();
    if (!soft_i2c_is_bus_idle())
    {
        soft_i2c_recover_bus();
        if (!soft_i2c_is_bus_idle())
        {
            return MP2797_STATUS_BUS_BUSY;
        }
    }

    uint16_t power_status = 0u;
    mp2797_status_t status = mp2797_read_word_internal(MP2797_REG_PWR_STATUS,&power_status);
    (void)power_status;
    if (status != MP2797_STATUS_OK)
    {
        return status;
    }

    status = mp2797_verify_communication_config();//验证通信配置
    if (status != MP2797_STATUS_OK)
    {
        return status;
    }

    if (s_mp2797_config.voltage_only_mode)
    {
        status = mp2797_apply_voltage_only_config();
        if (status != MP2797_STATUS_OK)
        {
            return status;
        }
    }

    s_mp2797_ready = true;
    return MP2797_STATUS_OK;
}

mp2797_status_t mp2797_probe(void)
{
    if (!s_mp2797_config_valid)
    {
        return MP2797_STATUS_NOT_READY;
    }

    uint16_t power_status = 0u;
    return mp2797_read_word_internal(MP2797_REG_PWR_STATUS, &power_status);
}
 
mp2797_status_t mp2797_configure_voltage_only(void)//把芯片配置成只采电压模式
{
    if (!s_mp2797_ready)
    {
        return MP2797_STATUS_NOT_READY;
    }

    mp2797_status_t status = mp2797_apply_voltage_only_config();
    if (status == MP2797_STATUS_OK)
    {
        s_mp2797_config.voltage_only_mode = true;
    }

    return status;
}

mp2797_status_t mp2797_read_register(uint8_t reg_addr, uint16_t *value)
{
    return mp2797_read_word_internal(reg_addr, value);
}

mp2797_status_t mp2797_start_voltage_scan(void)//向 MP2797 发出一次“开始高分辨率电压扫描”的命令，先检测上次的扫描是否还在进行，完成或出错后清除旧命令，再启动下一次扫描
{
    if (!s_mp2797_ready)
    {
        return MP2797_STATUS_NOT_READY;
    }

    uint16_t adc_ctrl = 0u;
    mp2797_status_t status = mp2797_read_word_internal(MP2797_REG_ADC_CTRL,&adc_ctrl);//读取ADC寄存器状态
    if (status != MP2797_STATUS_OK)
    {
        return status;
    }

    bool scan_is_active = ((adc_ctrl & MP2797_ADC_SCAN_GO) != 0u)
                          && ((adc_ctrl & (MP2797_ADC_SCAN_DONE| MP2797_ADC_SCAN_ERROR)) == 0u);//判断是否还在扫描（GO=1,DONE和ERROR=0）
    if (scan_is_active)
    {
        return MP2797_STATUS_BUSY;
    }

    if ((adc_ctrl & MP2797_ADC_SCAN_GO) != 0u)//清除上一次已经结束的扫描命令
    {
        status = mp2797_write_word_internal(MP2797_REG_ADC_CTRL, 0u);
        if (status != MP2797_STATUS_OK)
        {
            return status;
        }
        mp2797_delay_us(MP2797_FUNCTION_COMMAND_DELAY_US);
    }

    status = mp2797_write_word_internal(MP2797_REG_ADC_CTRL,MP2797_ADC_SCAN_GO);//启动一次新的扫描
    if (status != MP2797_STATUS_OK)
    {
        return status;
    }

    /*数据手册要求：在安全模式下使能功能命令后，串行总线必须保持至少 200 us 的空闲时间。*/
    mp2797_delay_us(MP2797_FUNCTION_COMMAND_DELAY_US);
    return MP2797_STATUS_OK;
}

mp2797_status_t mp2797_get_voltage_scan_state(mp2797_scan_state_t *state)//读取 MP2797 的 ADC_CTRL 寄存器，判断电压扫描目前处于哪个状态
{
    if (state == NULL)
    {
        return MP2797_STATUS_INVALID_ARG;
    }

    if (!s_mp2797_ready)
    {
        return MP2797_STATUS_NOT_READY;
    }

    uint16_t adc_ctrl = 0u;
    mp2797_status_t status = mp2797_read_word_internal(MP2797_REG_ADC_CTRL,&adc_ctrl);
    if (status != MP2797_STATUS_OK)
    {
        return status;
    }

    if ((adc_ctrl & MP2797_ADC_SCAN_ERROR) != 0u)
    {
        *state = MP2797_SCAN_STATE_ERROR;
    }
    else if ((adc_ctrl & MP2797_ADC_SCAN_DONE) != 0u)
    {
        *state = MP2797_SCAN_STATE_DONE;
    }
    else if ((adc_ctrl & MP2797_ADC_SCAN_GO) != 0u)
    {
        *state = MP2797_SCAN_STATE_BUSY;
    }
    else
    {
        *state = MP2797_SCAN_STATE_IDLE;
    }

    return MP2797_STATUS_OK;
}

mp2797_status_t mp2797_wait_voltage_scan(void)//等待 MP2797 的电压扫描结束，直到返回扫描完成，扫描错误，通信失败or超时
{
    if (!s_mp2797_ready)
    {
        return MP2797_STATUS_NOT_READY;
    }

    for (uint16_t poll = 0u; poll < s_mp2797_config.scan_poll_limit; poll++)
    {
        mp2797_scan_state_t state = MP2797_SCAN_STATE_IDLE;
        mp2797_status_t status = mp2797_get_voltage_scan_state(&state);
        if (status != MP2797_STATUS_OK)
        {
            return status;
        }

        if ((state == MP2797_SCAN_STATE_DONE) || (state == MP2797_SCAN_STATE_ERROR))//判断本轮扫描是否结束，只要扫描完成or扫描错误都算结束
        {
            status = mp2797_write_word_internal(MP2797_REG_ADC_CTRL, 0u);
            if (status != MP2797_STATUS_OK)
            {
                return status;
            }

            mp2797_delay_us(MP2797_FUNCTION_COMMAND_DELAY_US);
            return (state == MP2797_SCAN_STATE_DONE)? MP2797_STATUS_OK: MP2797_STATUS_SCAN_ERROR;
        }

        mp2797_delay_us(MP2797_FUNCTION_COMMAND_DELAY_US);
    }

    return MP2797_STATUS_TIMEOUT;
}

uint16_t mp2797_cell_raw_to_mv(uint16_t raw)//把原始单体电芯数据转化为mV
{
    uint32_t code = (uint32_t)(raw & MP2797_ADC_RESULT_MASK);
    return (uint16_t)((code * 5000u + 16384u) / 32768u);//满量程5V，15位ADC
}

uint32_t mp2797_pack_raw_to_mv(uint16_t raw)//把总电池电压转化为mV
{
    uint32_t code = (uint32_t)(raw & MP2797_ADC_RESULT_MASK);
    return (code * 80000u + 16384u) / 32768u;//满量程80V，15位ADC
}

mp2797_status_t mp2797_read_cell_voltage(uint8_t cell_number,
                                         uint16_t *raw,
                                        uint16_t *millivolts)//读取指定电芯的ADC电压值（电芯编号，15位ADC原始数据的指针，换算后的电压值的指针）
{
    if ((raw == NULL) && (millivolts == NULL))
    {
        return MP2797_STATUS_INVALID_ARG;
    }

    if (!s_mp2797_ready)
    {
        return MP2797_STATUS_NOT_READY;
    }

    if ((cell_number == 0u) || (cell_number > s_mp2797_config.cell_count))
    {
        return MP2797_STATUS_INVALID_ARG;
    }

    uint8_t reg_addr = (uint8_t)(MP2797_REG_RD_VCELL1+ ((cell_number - 1u)* MP2797_CELL_REGISTER_STRIDE));//计算该电芯ADC原始数据对应的寄存器地址
    uint16_t result = 0u;
    mp2797_status_t status = mp2797_read_word_internal(reg_addr, &result);
    if (status != MP2797_STATUS_OK)
    {
        return status;
    }

    result &= MP2797_ADC_RESULT_MASK;//因为ADC是15位数据，但是该寄存器有16位，需要把bit15清除
    if (raw != NULL)
    {
        *raw = result;
    }
    if (millivolts != NULL)
    {
        *millivolts = mp2797_cell_raw_to_mv(result);
    }

    return MP2797_STATUS_OK;
}

mp2797_status_t mp2797_read_pack_voltage(uint16_t *raw, uint32_t *millivolts)//读取VTOP的电压
{
    if ((raw == NULL) && (millivolts == NULL))
    {
        return MP2797_STATUS_INVALID_ARG;
    }

    if (!s_mp2797_ready)
    {
        return MP2797_STATUS_NOT_READY;
    }

    uint16_t result = 0u;
    mp2797_status_t status = mp2797_read_word_internal(MP2797_REG_RD_VTOP,&result);
    if (status != MP2797_STATUS_OK)
    {
        return status;
    }

    result &= MP2797_ADC_RESULT_MASK;
    if (raw != NULL)
    {
        *raw = result;
    }
    if (millivolts != NULL)
    {
        *millivolts = mp2797_pack_raw_to_mv(result);
    }

    return MP2797_STATUS_OK;
}

mp2797_status_t mp2797_read_cell_voltages(mp2797_cell_voltages_t *voltages)//读取 VTOP 总压以及所有已配置电芯的电压
{
    if (voltages == NULL)
    {
        return MP2797_STATUS_INVALID_ARG;
    }

    memset(voltages, 0, sizeof(*voltages));//把整个结果结构体清零

    if (!s_mp2797_ready)
    {
        return MP2797_STATUS_NOT_READY;
    }

    voltages->cell_count = s_mp2797_config.cell_count;

    mp2797_status_t status = mp2797_start_voltage_scan();
    if (status != MP2797_STATUS_OK)
    {
        return status;
    }

    status = mp2797_wait_voltage_scan();
    if (status != MP2797_STATUS_OK)
    {
        return status;
    }

    status = mp2797_read_pack_voltage(&voltages->pack_raw,&voltages->pack_mv);
    if (status != MP2797_STATUS_OK)
    {
        return status;
    }

    for (uint8_t cell = 0u; cell < voltages->cell_count; cell++)
    {
        status = mp2797_read_cell_voltage((uint8_t)(cell + 1u), &voltages->cell_raw[cell],&voltages->cell_mv[cell]);
        if (status != MP2797_STATUS_OK)
        {
            return status;
        }

        voltages->cell_sum_mv += voltages->cell_mv[cell];
    }

    voltages->valid = true;
    return MP2797_STATUS_OK;
}

void mp2797_shutdown(void)
{
    GPIO_ResetBits(BOARD_AFE_NSHDN_PORT, BOARD_AFE_NSHDN_PIN);
    s_mp2797_ready = false;
    s_mp2797_config_valid = false;
}

bool mp2797_is_ready(void)
{
    return s_mp2797_ready;
}
