/**
 * @file main.c
 * @author N32cube
 */
 //!!!!!!!!!!!!!!!!NOTE!!!!!!!!!!!!!!!
 // Code cannot be added between /* NTFx CODE START xxxxx*/ and /* NTFx CODE END xxxxx*/
/* NTFx CODE START Include*/
#include "main.h"
#include <stdio.h>
#include <stdint.h>
/* NTFx CODE END Include*/

#include "bms_app.h"

/**
 * @brief  Main program.
 */
int main(void)
{
    /* NTFx CODE START Config*/
    RCC_Configuration();
    GPIO_Configuration();
    NVIC_Configuration();
    USART_Configuration();
    CAN_Configuration();
    DMA_Configuration();
    /* NTFx CODE END Config*/
    
/*RCC 配置完成后，根据真实寄存器更新 SystemCoreClock。这里只更新软件变量，不会再次改变系统时钟。*/
    SystemCoreClockUpdate();

    /* 按真实系统时钟重新配置 1 ms SysTick。 */
    if (SysTick_Config(SystemCoreClock / 1000u) != 0u)
    {
        while (1)
        {
            __NOP();
        }
    }

    bms_app_init();
    while(1)
    {
        bms_app_task();
    }

}
