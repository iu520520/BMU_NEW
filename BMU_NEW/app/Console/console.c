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

static void console_write(const char *text)
{
    (void)pc_uart_write_string(text, CONSOLE_TX_TIMEOUT_MS);
}

static char console_to_lower(char character)
{
    if ((character >= 'A') && (character <= 'Z'))
    {
        return (char)(character + ('a' - 'A'));
    }
    return character;
}

static void console_show_menu(void)
{
    console_write("\r\nBMU console ready\r\n");
    console_write("C1..C16 : start balancing selected cell\r\n");
    console_write("r       : stop all balancing channels\r\n");
    console_write("startafe: print all cell voltages every second\r\n");
    console_write("stopafe : stop voltage printing\r\n");
}

static bool console_parse_cell_number(const char *command, uint8_t *cell_number)
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

    if ((command[index] != '\0') || (value == 0u))
    {
        return false;
    }

    *cell_number = (uint8_t)value;
    return true;
}

static const char *console_balance_error_text(cell_balance_status_t status)
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

static void console_start_cell(uint8_t cell_number)
{
    char response[64];
    cell_balance_status_t status = bms_app_start_balance(cell_number);

    if (status == CELL_BALANCE_STATUS_OK)
    {
        (void)snprintf(response, sizeof(response),
                       "OK: balancing cell C%u\r\n", (unsigned int)cell_number);
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

static void console_execute_command(void)
{
    uint8_t cell_number;

    for (uint8_t index = 0u; index < s_command_length; index++)
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
        s_last_afe_print_ms = mwTick - CONSOLE_AFE_PRINT_PERIOD_MS;
        console_write("OK: AFE voltage printing started\r\n");
    }
    else if (strcmp(s_command, "stopafe") == 0)
    {
        s_afe_print_enabled = false;
        console_write("OK: AFE voltage printing stopped\r\n");
    }
    else
    {
        console_write("ERROR: invalid command\r\n");
    }
}

static void console_receive_commands(void)
{
    uint8_t data;

    while (pc_uart_read_byte(&data) == PC_UART_STATUS_OK)
    {
        if ((data == '\r') || (data == '\n'))
        {
            if (s_command_length != 0u)
            {
                console_write("\r\n");
                console_execute_command();
                s_command_length = 0u;
            }
            continue;
        }

        if ((data == 0x08u) || (data == 0x7Fu))
        {
            if (s_command_length != 0u)
            {
                s_command_length--;
                console_write("\b \b");
            }
            continue;
        }

        if ((data < 0x20u) || (data > 0x7Eu))
        {
            continue;
        }

        if (s_command_length < (CONSOLE_COMMAND_BUFFER_SIZE - 1u))
        {
            s_command[s_command_length++] = (char)data;
            (void)pc_uart_write_byte(data, CONSOLE_TX_TIMEOUT_MS);
        }
        else
        {
            s_command_length = 0u;
            console_write("\r\nERROR: command too long\r\n");
        }
    }
}

static void console_print_afe_voltages(void)
{
    char line[48];
    const mp2797_cell_voltages_t *voltages;
    uint32_t now_ms = mwTick;

    if (!s_afe_print_enabled
        || ((uint32_t)(now_ms - s_last_afe_print_ms) < CONSOLE_AFE_PRINT_PERIOD_MS))
    {
        return;
    }
    s_last_afe_print_ms = now_ms;

    voltages = bms_app_get_voltages();
    if ((bms_app_get_afe_status() != MP2797_STATUS_OK) || !voltages->valid)
    {
        (void)snprintf(line, sizeof(line), "AFE ERROR: status=%u\r\n",
                       (unsigned int)bms_app_get_afe_status());
        console_write(line);
        return;
    }

    console_write("AFE voltages:\r\n");
    for (uint8_t index = 0u; index < voltages->cell_count; index++)
    {
        (void)snprintf(line, sizeof(line), "C%u=%u mV\r\n",
                       (unsigned int)(index + 1u),
                       (unsigned int)voltages->cell_mv[index]);
        console_write(line);
    }
    (void)snprintf(line, sizeof(line), "PACK=%lu mV, SUM=%lu mV\r\n",
                   (unsigned long)voltages->pack_mv,
                   (unsigned long)voltages->cell_sum_mv);
    console_write(line);
}

void console_init(void)
{
    s_command_length = 0u;
    s_afe_print_enabled = false;
    s_last_afe_print_ms = mwTick;
    memset(s_command, 0, sizeof(s_command));
    console_show_menu();
}

void console_task(void)
{
    console_receive_commands();
    console_print_afe_voltages();
}
