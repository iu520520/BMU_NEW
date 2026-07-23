#include "console.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bms_app.h"
#include "n32g45x.h"
#include "pc_uart.h"

#define CONSOLE_COMMAND_BUFFER_SIZE 32u
#define CONSOLE_TX_TIMEOUT_MS       100u
#define CONSOLE_AFE_PRINT_PERIOD_MS 1000u

extern __IO uint32_t mwTick;

static char s_command[CONSOLE_COMMAND_BUFFER_SIZE];
static uint8_t s_command_length;
static bool s_afe_print_enabled;
static uint32_t s_last_afe_print_ms;

static void console_write(const char *text)//调用串口打印函数入口
{
    (void)pc_uart_write_string(text, CONSOLE_TX_TIMEOUT_MS);
}

static char console_to_lower(char character)//不区分大小写
{
    if ((character >= 'A') && (character <= 'Z'))
    {
        return (char)(character + ('a' - 'A'));
    }
    return character;
}

static void console_show_menu(void)//menu
{
    console_write("\r\nBMU console ready\r\n");
    console_write("C1..C16 : start balancing selected cell\r\n");
    console_write("r       : stop all balancing channels\r\n");
    console_write("startafe: start printing AFE voltages every second\r\n");
    console_write("stopafe : stop printing (AFE keeps sampling)\r\n");
    console_write("menu    : show this menu\r\n");
    console_write("c       : clear terminal screen\r\n");
}

static void console_clear_screen(void)
{
    /* ANSI：清除整个终端窗口，并将光标移动到左上角。 */
    console_write("\x1B[2J\x1B[H");
}

static bool console_parse_cell_number(const char *command, uint8_t *cell_number)//把字符串命令转化为C?的形式
{
    uint16_t value = 0u;
    size_t index = 1u;

    if ((command == NULL) || (cell_number == NULL) || (command[0] != 'c'))
    {
        return false;
    }
    if ((command[index] < '0') || (command[index] > '9'))
    {
        return false;
    }

    while ((command[index] >= '0') && (command[index] <= '9'))
    {
        value = (uint16_t)(value * 10u + (uint16_t)(command[index] - '0'));
        if (value > CELL_BALANCE_MAX_CELL_COUNT)
        {
            return false;
        }
        index++;
    }

    if ((command[index] != '\0') || (value == 0u))//检查后面是否有字符or电芯编号是否为0
    {
        return false;
    }

    *cell_number = (uint8_t)value;
    return true;
}

static const char *console_balance_error_text(cell_balance_status_t status)//均衡状态-详细错误解释
{
    switch (status)
    {
        case CELL_BALANCE_STATUS_INVALID_ARG:
            return "invalid cell number";
        case CELL_BALANCE_STATUS_NOT_INITIALIZED:
            return "balance module not initialized";
        case CELL_BALANCE_STATUS_VOLTAGE_INVALID:
            return "AFE voltage is not ready or invalid";
        case CELL_BALANCE_STATUS_CELL_LIMIT:
            return "selected cell reached voltage limit";
        case CELL_BALANCE_STATUS_OK:
        default:
            return "unknown error";
    }
}

static const char *console_afe_status_detail(mp2797_status_t status)//AFE状态-详细错误解释
{
    switch (status)
    {
        case MP2797_STATUS_ERROR:
            return "general driver or I2C error";
        case MP2797_STATUS_TIMEOUT:
            return "I2C or ADC scan timed out";
        case MP2797_STATUS_INVALID_ARG:
            return "invalid driver argument or configuration";
        case MP2797_STATUS_NOT_READY:
            return "AFE initialization is not complete";
        case MP2797_STATUS_NACK:
            return "MP2797 did not acknowledge I2C";
        case MP2797_STATUS_BUS_BUSY:
            return "I2C bus is held low or busy";
        case MP2797_STATUS_CRC_ERROR:
            return "received communication CRC is invalid";
        case MP2797_STATUS_SCAN_ERROR:
            return "MP2797 reported an ADC scan error";
        case MP2797_STATUS_BUSY:
            return "MP2797 functional command is still busy";
        case MP2797_STATUS_CONFIG_MISMATCH:
            return "MP2797 register readback does not match";
        case MP2797_STATUS_OK:
            return "no error";
        default:
            return "unknown AFE status";
    }
}

static void console_start_cell(uint8_t cell_number)//尝试启动某一节电芯的均衡，然后把执行结果通过串口打印出来
{
    char response[128];
    bms_app_afe_snapshot_t afe_snapshot;
    cell_balance_status_t status = bms_app_start_balance(cell_number);

    if (status == CELL_BALANCE_STATUS_OK)
    {
        (void)snprintf(response, sizeof(response), "OK: balancing cell C%u\r\n", (unsigned int)cell_number);
    }
    else if (status == CELL_BALANCE_STATUS_VOLTAGE_INVALID)
    {
        bms_app_get_afe_snapshot(&afe_snapshot);
        if (afe_snapshot.status != MP2797_STATUS_OK)
        {
            (void)snprintf(response, sizeof(response),
                           "ERROR: C%u not started, AFE code=%u - %s\r\n",
                           (unsigned int)cell_number,
                           (unsigned int)afe_snapshot.status,
                           console_afe_status_detail(afe_snapshot.status));
        }
        else
        {
            (void)snprintf(response, sizeof(response),
                           "ERROR: C%u, %s\r\n",
                           (unsigned int)cell_number,
                           console_balance_error_text(status));
        }
    }
    else
    {
        (void)snprintf(response, sizeof(response),
                       "ERROR: C%u, %s\r\n",
                       (unsigned int)cell_number,
                       console_balance_error_text(status));
    }
    console_write(response);
}

static void console_execute_command(void)//处理串口命令
{
    uint8_t cell_number;

    for (uint8_t index = 0u; index < s_command_length; index++)//把全部命令转为小写
    {
        s_command[index] = console_to_lower(s_command[index]);
    }
    s_command[s_command_length] = '\0';

    if (s_command_length == 0u)
    {
        return;
    }
    if (console_parse_cell_number(s_command, &cell_number))
    {
        console_start_cell(cell_number);
    }
    else if (strcmp(s_command, "r") == 0)
    {
        bms_app_stop_balance();
        console_write("OK: all balancing channels stopped\r\n");
    }
    else if (strcmp(s_command, "startafe") == 0)
    {
        s_afe_print_enabled = true;
        s_last_afe_print_ms = mwTick;
        console_write("OK: AFE voltage printing started; sampling is always active\r\n");
    }
    else if (strcmp(s_command, "stopafe") == 0)
    {
        s_afe_print_enabled = false;
        console_write("OK: AFE voltage printing stopped; sampling remains active\r\n");
    }
    else if (strcmp(s_command, "menu") == 0)
    {
        console_show_menu();
    }
    else if (strcmp(s_command, "c") == 0)
    {
        console_clear_screen();
    }
    else
    {
        console_write("ERROR: invalid command\r\n");
    }
}


/*不断从 PC 调试串口读取字符，把字符拼成一条完整命令，并处理回车、退格、非法字符和命令过长等情况。*/
static void console_receive_commands(void)
{
    uint8_t data;

    while (pc_uart_read_byte(&data) == PC_UART_STATUS_OK)//不断读取串口中的字符
    {
        if ((data == '\r') || (data == '\n'))//回车和换行才开始处理
        {
            if (s_command_length != 0u)
            {
                console_write("\r\n");
                console_execute_command();
                s_command_length = 0u;
            }
            continue;
        }

        if ((data == 0x08u) || (data == 0x7Fu))//处理退格
        {
            if (s_command_length != 0u)
            {
                s_command_length--;
                console_write("\b \b");
            }
            continue;
        }

        if ((data < 0x20u) || (data > 0x7Eu))//忽略无效字符
        {
            continue;
        }

        if (s_command_length < (CONSOLE_COMMAND_BUFFER_SIZE - 1u))//这里检查命令缓冲区是否还有空间，最后一个字符需要放字符串结束符
        {
            s_command[s_command_length++] = (char)data;
            (void)pc_uart_write_byte(data, CONSOLE_TX_TIMEOUT_MS);//输入回显
        }
        else
        {
            s_command_length = 0u;
            console_write("\r\nERROR: command too long\r\n");
        }
    }
}

static void console_print_afe_voltages(void)//在串口中打印电压数据
{
    char line[128];
    bms_app_afe_snapshot_t snapshot;
    const mp2797_cell_voltages_t *voltages = &snapshot.voltages;
    uint32_t now_ms = mwTick;

    if (!s_afe_print_enabled|| 
        ((uint32_t)(now_ms - s_last_afe_print_ms) < CONSOLE_AFE_PRINT_PERIOD_MS))//判断是否需要打印
    {
        return;
    }
    bms_app_get_afe_snapshot(&snapshot);

    /*
     * 分步采样期间等待整帧完成再打印。这里不更新时间戳，
     * 因而状态机结束后会立即输出新数据或本轮错误。
     */
    if (snapshot.sampling)
    {
        return;
    }

    s_last_afe_print_ms = now_ms;//记录本次打印时间

    if ((snapshot.status != MP2797_STATUS_OK) || !voltages->valid)//检查 AFE 状态和电压数据是否有效
    {
        (void)snprintf(line, sizeof(line),
                       "AFE ERROR: code=%u - %s\r\n",
                       (unsigned int)snapshot.status,
                       console_afe_status_detail(snapshot.status));
        console_write(line);
        /* 错误只报告一次，AFE仍在后台采样；再次输入 startafe 可重新开启打印。 */
        s_afe_print_enabled = false;
        return;
    }

    console_write("voltages:\r\n");
    for (uint8_t index = 0u; index < voltages->cell_count; index++)//逐节打印电压
    {
        (void)snprintf(line, sizeof(line), "C%u=%u mV\r\n",
                       (unsigned int)(index + 1u),
                       (unsigned int)voltages->cell_mv[index]);
        console_write(line);
    }
    (void)snprintf(line, sizeof(line), "PACK=%lu mV, SUM=%lu mV\r\n",
                   (unsigned long)voltages->pack_mv,
                   (unsigned long)voltages->cell_sum_mv);//打印总电压，voltages->pack_mv是AFE测得的，voltages->cell_sum_mv是相加的
    console_write(line);
}

void console_init(void)
{
    s_command_length = 0u;//初始化当前命令
    s_afe_print_enabled = false;//不打印电压
    s_last_afe_print_ms = mwTick;//记录初始化的时间
    memset(s_command, 0, sizeof(s_command));//清空命令缓存区
    console_show_menu();
}

void console_task(void)
{
    console_receive_commands();//接收并处理串口命令
    console_print_afe_voltages();//检查是否需要打印 AFE 电压
}
