#include "soft_i2c.h"

#include "FreeRTOS.h"
#include "task.h"

#include "board_pins.h"
#include "n32g45x_gpio.h"

#define SOFT_I2C_READ_BIT            0x01u
#define SOFT_I2C_WRITE_BIT           0x00u
#define SOFT_I2C_CLOCK_STRETCH_LIMIT 2000u
#define SOFT_I2C_BUS_RECOVER_CLOCKS  9u

/*
 * PD1=SCL, PD2=SDA are configured by NTFx as open-drain outputs.
 *
 * For open-drain I2C:
 * - writing 0 drives the line low;
 * - writing 1 releases the line, and the pull-up resistor makes it high.
 */

static void soft_i2c_delay(void)//延时函数，用于控制 SCL、SDA 电平变化之间的时间间隔
{
    volatile uint32_t delay = 24u;

    while (delay-- > 0u)
    {
        __NOP();
    }
}


//释放和拉低SCL和SDA
static void soft_i2c_scl_release(void)
{
    GPIO_SetBits(BOARD_AFE_SCL_PORT, BOARD_AFE_SCL_PIN);
}

static void soft_i2c_scl_low(void)
{
    GPIO_ResetBits(BOARD_AFE_SCL_PORT, BOARD_AFE_SCL_PIN);
}

static void soft_i2c_sda_release(void)
{
    GPIO_SetBits(BOARD_AFE_SDA_PORT, BOARD_AFE_SDA_PIN);
}

static void soft_i2c_sda_low(void)
{
    GPIO_ResetBits(BOARD_AFE_SDA_PORT, BOARD_AFE_SDA_PIN);
}


//读取SCL和SDA的电平状态
static bool soft_i2c_read_scl(void)
{
    return GPIO_ReadInputDataBit(BOARD_AFE_SCL_PORT, BOARD_AFE_SCL_PIN) != 0u;
}

static bool soft_i2c_read_sda(void)
{
    return GPIO_ReadInputDataBit(BOARD_AFE_SDA_PORT, BOARD_AFE_SDA_PIN) != 0u;
}



static soft_i2c_status_t soft_i2c_wait_scl_high(void)//释放 SCL，然后等待 SCL 真正变成高电平；如果等太久还没变高，就返回超时错误
{
    uint32_t timeout = SOFT_I2C_CLOCK_STRETCH_LIMIT;

    soft_i2c_scl_release();
    while (!soft_i2c_read_scl())
    {
        if (timeout-- == 0u)
        {
            return SOFT_I2C_STATUS_TIMEOUT;
        }
        soft_i2c_delay();
    }

    return SOFT_I2C_STATUS_OK;
}

static soft_i2c_status_t soft_i2c_clock_high_delay(void)//等待SCL变高之后，再额外保持一段时间，高电平脉冲太窄，从机可能检测不到
{
    soft_i2c_status_t status = soft_i2c_wait_scl_high();
    if (status != SOFT_I2C_STATUS_OK)
    {
        return status;
    }

    soft_i2c_delay();
    return SOFT_I2C_STATUS_OK;
}

static soft_i2c_status_t soft_i2c_start(void)
{
    soft_i2c_sda_release();
    soft_i2c_scl_release();//释放两个引脚
    soft_i2c_delay();

    if (!soft_i2c_read_sda())//检查总线是否空闲
    {
        return SOFT_I2C_STATUS_BUS_BUSY;
    }

    if (soft_i2c_wait_scl_high() != SOFT_I2C_STATUS_OK)
    {
        return SOFT_I2C_STATUS_TIMEOUT;
    }

    soft_i2c_sda_low();//SCL保持高时，SDA从高到低，然后再把SCL拉低，准备通信
    soft_i2c_delay();
    soft_i2c_scl_low();
    soft_i2c_delay();

    return SOFT_I2C_STATUS_OK;
}

static soft_i2c_status_t soft_i2c_restart(void)//重新发送
{
    soft_i2c_sda_release();
    soft_i2c_delay();             

    if (soft_i2c_wait_scl_high() != SOFT_I2C_STATUS_OK)
    {
        return SOFT_I2C_STATUS_TIMEOUT;
    }

    soft_i2c_sda_low();
    soft_i2c_delay();
    soft_i2c_scl_low();
    soft_i2c_delay();

    return SOFT_I2C_STATUS_OK;
}

static void soft_i2c_stop(void)
{
    soft_i2c_sda_low();
    soft_i2c_delay();
    soft_i2c_scl_release();
    (void)soft_i2c_wait_scl_high();
    soft_i2c_delay();
    soft_i2c_sda_release();
    soft_i2c_delay();
}

/*
 * 软件I2C一个事务由连续的GPIO边沿组成，不允许在半个字节中切换任务。
 * 这里只暂停任务调度，不关闭中断；UART4仍可及时收字节并通知Console，
 * xTaskResumeAll后会立即执行已经挂起的高优先级任务。
 */
static bool soft_i2c_suspend_scheduler(void)
{
    if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING)
    {
        vTaskSuspendAll();
        return true;
    }

    return false;
}

static void soft_i2c_resume_scheduler(bool scheduler_suspended)
{
    if (scheduler_suspended)
    {
        (void)xTaskResumeAll();
    }
}




static soft_i2c_status_t soft_i2c_write_bit(bool bit)//MCU向SDA写bit数据
{
    if (bit)
    {
        soft_i2c_sda_release();
    }
    else
    {
        soft_i2c_sda_low();
    }

    soft_i2c_delay();//在SCL上升前，数据必须保持稳定

    soft_i2c_status_t status = soft_i2c_clock_high_delay();
    if (status != SOFT_I2C_STATUS_OK)//如果SCL一直无法为高，说明可能发生时钟拉伸超时，返回错误状态
    {
        soft_i2c_scl_low();
        return status;
    }

    soft_i2c_scl_low();
    soft_i2c_delay();

    return SOFT_I2C_STATUS_OK;
}

static soft_i2c_status_t soft_i2c_read_bit(bool *bit)//MCU从SDA读取数据
{
    if (bit == NULL)//如果传入空指针，报错
    {
        return SOFT_I2C_STATUS_INVALID_ARG;
    }

    soft_i2c_sda_release();//MCU需要释放SDA，否则从机无法控制SDA
    soft_i2c_delay();

    soft_i2c_status_t status = soft_i2c_clock_high_delay();
    if (status != SOFT_I2C_STATUS_OK)
    {
        soft_i2c_scl_low();
        return status;
    }

    *bit = soft_i2c_read_sda();

    soft_i2c_scl_low();
    soft_i2c_delay();

    return SOFT_I2C_STATUS_OK;
}



static soft_i2c_status_t soft_i2c_write_byte(uint8_t byte)
{
    for (uint8_t mask = 0x80u; mask != 0u; mask >>= 1u)//mask初始值为1000，0000；然后一直在右移
    {
        soft_i2c_status_t status = soft_i2c_write_bit((byte & mask) != 0u);
        if (status != SOFT_I2C_STATUS_OK)//如果某一位发送失败，就返回错误值
        {
            return status;
        }
    }

    bool ack_is_high = true;
    soft_i2c_status_t status = soft_i2c_read_bit(&ack_is_high);//发送完8bit之后，读取从机输出端电平，
    if (status != SOFT_I2C_STATUS_OK)
    {
        return status;
    }

    return ack_is_high ? SOFT_I2C_STATUS_NACK : SOFT_I2C_STATUS_OK;//SDA低电平：ACK,表示从机收到；SDA高电平：NACK，表示从机没有收到
}

static soft_i2c_status_t soft_i2c_read_byte(uint8_t *byte, bool ack)//（用于保存读取到的1字节数据，读取完成后，主机是否向从机发送ack）
{
    if (byte == NULL)//先检查byte是否为空指针
    {
        return SOFT_I2C_STATUS_INVALID_ARG;
    }

    uint8_t value = 0u;

    for (uint8_t i = 0u; i < 8u; i++)//循环读取8位
    {
        bool bit = false;
        soft_i2c_status_t status = soft_i2c_read_bit(&bit);//读取数据放在bit中
        if (status != SOFT_I2C_STATUS_OK)
        {
            return status;
        }

        value <<= 1u;
        if (bit)
        {
            value |= 0x01u;
        }
    }

    *byte = value;

    return soft_i2c_write_bit(!ack);
}

void soft_i2c_init(void)//i2c初始化函数
{
    soft_i2c_sda_release();
    soft_i2c_scl_release();
    soft_i2c_delay();
}

bool soft_i2c_is_bus_idle(void)//检查i2c是否处于idle状态
{
    soft_i2c_sda_release();
    soft_i2c_scl_release();
    soft_i2c_delay();

    return soft_i2c_read_sda() && soft_i2c_read_scl();
}

static void soft_i2c_recover_bus_unlocked(void)//恢复卡死的总线
{
    soft_i2c_sda_release();

    for (uint8_t i = 0u; i < SOFT_I2C_BUS_RECOVER_CLOCKS; i++)//额外时钟是为了让卡住的从机继续完成数据传输
    {
        soft_i2c_scl_low();
        soft_i2c_delay();
        soft_i2c_scl_release();
        (void)soft_i2c_wait_scl_high();
        soft_i2c_delay();
    }

    soft_i2c_stop();
}

void soft_i2c_recover_bus(void)
{
    bool scheduler_suspended = soft_i2c_suspend_scheduler();
    soft_i2c_recover_bus_unlocked();
    soft_i2c_resume_scheduler(scheduler_suspended);
}

static soft_i2c_status_t soft_i2c_write_unlocked(uint8_t dev_addr_7bit,
                                                  const uint8_t *data,
                                                  size_t len)
{
    if ((data == NULL) && (len > 0u))//如果发送长度为0，或者指针为空指针，报错
    {
        return SOFT_I2C_STATUS_INVALID_ARG;
    }

    soft_i2c_status_t status = soft_i2c_start();//产生i2c起始信号
    if (status != SOFT_I2C_STATUS_OK)
    {
        return status;
    }

    status = soft_i2c_write_byte((uint8_t)((dev_addr_7bit << 1u) | SOFT_I2C_WRITE_BIT));//发送的数据为8位，7位地址+1位读写方向（0为写，1为读）
    if (status != SOFT_I2C_STATUS_OK)
    {
        soft_i2c_stop();
        return status;
    }

    for (size_t i = 0u; i < len; i++)//把data数据逐字节发送
    {
        status = soft_i2c_write_byte(data[i]);
        if (status != SOFT_I2C_STATUS_OK)
        {
            soft_i2c_stop();
            return status;
        }
    }

    soft_i2c_stop();
    return SOFT_I2C_STATUS_OK;
}

soft_i2c_status_t soft_i2c_write(uint8_t dev_addr_7bit,
                                 const uint8_t *data,
                                 size_t len)
{
    bool scheduler_suspended = soft_i2c_suspend_scheduler();
    soft_i2c_status_t status =
        soft_i2c_write_unlocked(dev_addr_7bit, data, len);
    soft_i2c_resume_scheduler(scheduler_suspended);
    return status;
}

static soft_i2c_status_t soft_i2c_read_unlocked(uint8_t dev_addr_7bit,
                                                 uint8_t *data,
                                                 size_t len)
{
    if ((data == NULL) && (len > 0u))
    {
        return SOFT_I2C_STATUS_INVALID_ARG;
    }

    soft_i2c_status_t status = soft_i2c_start();
    if (status != SOFT_I2C_STATUS_OK)
    {
        return status;
    }

    status = soft_i2c_write_byte((uint8_t)((dev_addr_7bit << 1u) | SOFT_I2C_READ_BIT));//发送的数据为8位，7位地址+1位读写方向（0为写，1为读）
    if (status != SOFT_I2C_STATUS_OK)
    {
        soft_i2c_stop();
        return status;
    }

    for (size_t i = 0u; i < len; i++)//把数据逐一读取到data
    {
        bool ack = (i + 1u) < len;//这里ack是指是否还想继续读取（1为还需要读取，0为当前读取的字节为最后一个字节，不需要再读取）
        status = soft_i2c_read_byte(&data[i], ack);
        if (status != SOFT_I2C_STATUS_OK)
        {
            soft_i2c_stop();
            return status;
        }
    }

    soft_i2c_stop();
    return SOFT_I2C_STATUS_OK;
}

soft_i2c_status_t soft_i2c_read(uint8_t dev_addr_7bit,
                                uint8_t *data,
                                size_t len)
{
    bool scheduler_suspended = soft_i2c_suspend_scheduler();
    soft_i2c_status_t status =
        soft_i2c_read_unlocked(dev_addr_7bit, data, len);
    soft_i2c_resume_scheduler(scheduler_suspended);
    return status;
}

static soft_i2c_status_t soft_i2c_write_read_unlocked(
    uint8_t dev_addr_7bit,
    const uint8_t *tx_data,
    size_t tx_len,
    uint8_t *rx_data,
    size_t rx_len)
{
    if (((tx_data == NULL) && (tx_len > 0u)) || ((rx_data == NULL) && (rx_len > 0u)))
    {
        return SOFT_I2C_STATUS_INVALID_ARG;
    }

    soft_i2c_status_t status = soft_i2c_start();
    if (status != SOFT_I2C_STATUS_OK)
    {
        return status;
    }

    status = soft_i2c_write_byte((uint8_t)((dev_addr_7bit << 1u) | SOFT_I2C_WRITE_BIT));
    if (status != SOFT_I2C_STATUS_OK)
    {
        soft_i2c_stop();
        return status;
    }

    for (size_t i = 0u; i < tx_len; i++)
    {
        status = soft_i2c_write_byte(tx_data[i]);
        if (status != SOFT_I2C_STATUS_OK)
        {
            soft_i2c_stop();
            return status;
        }
    }

    if (rx_len == 0u)//如果不需要读取数据的话就直接stop，退化成一次普通的写操作
    {
        soft_i2c_stop();
        return SOFT_I2C_STATUS_OK;
    }

    status = soft_i2c_restart();//因为读写其实是同一任务，所以不能stop，而是restart
    if (status != SOFT_I2C_STATUS_OK)
    {
        soft_i2c_stop();
        return status;
    }

    status = soft_i2c_write_byte((uint8_t)((dev_addr_7bit << 1u) | SOFT_I2C_READ_BIT));//告诉从机，现在改为从机发送数据
    if (status != SOFT_I2C_STATUS_OK)
    {
        soft_i2c_stop();
        return status;
    }

    for (size_t i = 0u; i < rx_len; i++)//循环读取数据
    {
        bool ack = (i + 1u) < rx_len;
        status = soft_i2c_read_byte(&rx_data[i], ack);
        if (status != SOFT_I2C_STATUS_OK)
        {
            soft_i2c_stop();
            return status;
        }
    }

    soft_i2c_stop();
    return SOFT_I2C_STATUS_OK;
}

soft_i2c_status_t soft_i2c_write_read(uint8_t dev_addr_7bit,
                                      const uint8_t *tx_data,
                                      size_t tx_len,
                                      uint8_t *rx_data,
                                      size_t rx_len)
{
    bool scheduler_suspended = soft_i2c_suspend_scheduler();
    soft_i2c_status_t status =
        soft_i2c_write_read_unlocked(dev_addr_7bit,
                                     tx_data,
                                     tx_len,
                                     rx_data,
                                     rx_len);
    soft_i2c_resume_scheduler(scheduler_suspended);
    return status;
}

static soft_i2c_status_t soft_i2c_write_reg8_unlocked(
    uint8_t dev_addr_7bit,
    uint8_t reg_addr,
    const uint8_t *data,
    size_t len)
{
    if ((data == NULL) && (len > 0u))
    {
        return SOFT_I2C_STATUS_INVALID_ARG;
    }

    soft_i2c_status_t status = soft_i2c_start();
    if (status != SOFT_I2C_STATUS_OK)
    {
        return status;
    }

    status = soft_i2c_write_byte((uint8_t)((dev_addr_7bit << 1u) | SOFT_I2C_WRITE_BIT));//发送设备地址+写位
    if (status != SOFT_I2C_STATUS_OK)
    {
        soft_i2c_stop();
        return status;
    }

    status = soft_i2c_write_byte(reg_addr);//寄存器地址
    if (status != SOFT_I2C_STATUS_OK)
    {
        soft_i2c_stop();
        return status;
    }

    for (size_t i = 0u; i < len; i++)//
    {
        status = soft_i2c_write_byte(data[i]);
        if (status != SOFT_I2C_STATUS_OK)
        {
            soft_i2c_stop();
            return status;
        }
    }

    soft_i2c_stop();
    return SOFT_I2C_STATUS_OK;
}

soft_i2c_status_t soft_i2c_write_reg8(uint8_t dev_addr_7bit,
                                      uint8_t reg_addr,
                                      const uint8_t *data,
                                      size_t len)
{
    bool scheduler_suspended = soft_i2c_suspend_scheduler();
    soft_i2c_status_t status =
        soft_i2c_write_reg8_unlocked(dev_addr_7bit,
                                     reg_addr,
                                     data,
                                     len);
    soft_i2c_resume_scheduler(scheduler_suspended);
    return status;
}

soft_i2c_status_t soft_i2c_read_reg8(uint8_t dev_addr_7bit,
                                     uint8_t reg_addr,
                                     uint8_t *data,
                                     size_t len)
{
    return soft_i2c_write_read(dev_addr_7bit, &reg_addr, 1u, data, len);
}
