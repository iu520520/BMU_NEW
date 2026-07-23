/**
 * @file n32g45x_it.c
 * @author N32cube
 */

/* NTFx CODE START */
#include "n32g45x_it.h"
#include "n32g45x.h"
/* NTFx CODE END */

#include "pc_uart.h"

/* NTFx CODE START */
extern __IO uint32_t mwTick;
/**
 * @brief  This function handles NMI exception.
 */
void NMI_Handler(void)
{
/* NTFx CODE END */

}
/* NTFx CODE START */
/**
 * @brief  This function handles Hard Fault exception.
 */
void HardFault_Handler(void)
{
    /* Go to infinite loop when Hard Fault exception occurs */
    while (1)
    {
    /* NTFx CODE END */

    }
}
/* NTFx CODE START */
/**
 * @brief  This function handles Memory Manage exception.
 */
void MemManage_Handler(void)
{
    /* Go to infinite loop when Memory Manage exception occurs */
    while (1)
    {
/* NTFx CODE END */

    }
}
/* NTFx CODE START */
/**
 * @brief  This function handles Bus Fault exception.
 */
void BusFault_Handler(void)
{
    /* Go to infinite loop when Bus Fault exception occurs */
    while (1)
    {
/* NTFx CODE END */

    }
}
/* NTFx CODE START */
/**
 * @brief  This function handles Usage Fault exception.
 */
void UsageFault_Handler(void)
{
    /* Go to infinite loop when Usage Fault exception occurs */
    while (1)
    {
/* NTFx CODE END */

    }
}
/* NTFx CODE START */
/**
 * @brief  This function handles SVCall exception.
 */
void SVC_Handler(void)
{
/* NTFx CODE END */

}
/* NTFx CODE START */
/**
 * @brief  This function handles Debug Monitor exception.
 */
void DebugMon_Handler(void)
{
/* NTFx CODE END */

}
/* NTFx CODE START */
/**
 * @brief  This function handles SysTick Handler.
 */
void SysTick_Handler(void)
{
    mwTick++;
/* NTFx CODE END */

}
/* NTFx CODE START(UART4_IRQHandler)*/
/**
* @brief  This function handles UART4IRQHandler.
*/
void UART4_IRQHandler(void)
{
/* NTFx CODE END */

/* NTFx CODE START */
    if (USART_GetIntStatus(UART4, USART_INT_RXDNE))
    {
/* NTFx CODE END */

        pc_uart_irq_handler();

    }
/* NTFx CODE START */
    if (USART_GetIntStatus(UART4, USART_INT_OREF)||
        USART_GetIntStatus(UART4, USART_INT_NEF)||
        USART_GetIntStatus(UART4, USART_INT_FEF)||
        USART_GetIntStatus(UART4, USART_INT_PEF))
    {
        /*clear IT flag*/
        /*Read the sts register first,and the read the DAT register to clear the all error flag*/
        (void)UART4->STS;
        (void)UART4->DAT;
/* NTFx CODE END */

    }
    
/* NTFx CODE START */
}
/* NTFx CODE END(UART4_IRQHandler)*/

