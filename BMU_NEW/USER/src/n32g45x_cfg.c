/**
 * @file n32g45x_cfg.c
 * @author N32cube
 */

 #include "n32g45x_cfg.h"
/* NTFx CODE START */
#define  CAN_FILTER_MASK_STDID(STDID)        ((STDID&0x7FF)<<5)
#define  CAN_FILTER_MASK_EXTID_H(EXTID)      ((uint16_t)  (((EXTID<<3)|0x04)>>16) )
#define  CAN_FILTER_MASK_EXTID_L(EXTID)      ((uint16_t)  (( EXTID<<3)|0x04) )
__IO uint32_t mwTick;
void SysTick_Delayms(uint32_t Delayms)
{
    uint32_t tickstart = mwTick;
    uint32_t wait=Delayms;
    /* Add 1 to guarantee minimum wait */
    if (wait < 0xFFFFFFFFU)
    {
        wait +=1;
    }
    while ((mwTick - tickstart) < wait)
    {
    }
}

 /**
 *@name  DMA_SetPerMemAddr.
 *@brief Set peripher address and memory address of DMA
 *@param DMAChx (The input parameters must be the following values):
 *          - DMA1_CH1
 *          - DMA1_CH2
 *          - DMA1_CH3
 *          - DMA1_CH4
 *          - DMA1_CH5
 *          - DMA1_CH6
 *          - DMA1_CH7
 *          - DMA1_CH8
 *          - DMA2_CH1
 *          - DMA2_CH2
 *          - DMA2_CH3
 *          - DMA2_CH4
 *          - DMA2_CH5
 *          - DMA2_CH6
 *          - DMA2_CH7
 *          - DMA2_CH8
 *@param periphAddr   peripher address
 *@param memAddr   memory address
 *@param bufSize   buff size
 *@return status
 */
 void DMA_SetPerMemAddr(DMA_ChannelType* DMAChx, uint32_t periphAddr,uint32_t memAddr,uint32_t bufSize )
 {
     /* DMAy Channelx TXNUM Configuration */
    /* Write to DMAy Channelx TXNUM */
    DMAChx->TXNUM = bufSize;

    /* DMAy Channelx PADDR Configuration */
    /* Write to DMAy Channelx PADDR */
    DMAChx->PADDR = periphAddr;

    /* DMAy Channelx MADDR Configuration */
    /* Write to DMAy Channelx MADDR */
    DMAChx->MADDR = memAddr;
 }
/* NTFx CODE END */

/* NTFx CODE START */
/**
 *@brief Initializes the clock tree
 *@param null
 *@return status
 */
bool RCC_Configuration(void)
{
    RCC_DeInit();
    RCC_ConfigHclk(RCC_SYSCLK_DIV1);
    RCC_ConfigPclk2(RCC_HCLK_DIV1);
    RCC_ConfigPclk1(RCC_HCLK_DIV1);
    RCC_ConfigTrng1mClk(RCC_TRNG1MCLK_SRC_HSI,RCC_TRNG1MCLK_DIV8);
    RCC_ConfigRngcClk(RCC_RNGCCLK_SYSCLK_DIV1);
    RCC_ConfigHse(RCC_HSE_ENABLE);
    if(RCC_WaitHseStable() == ERROR)/* Wait till HSE is ready */
    {
        RCC_DeInit();
        return false;
    }
    
    FLASH_SetLatency(FLASH_LATENCY_0);
    FLASH_iCacheCmd(FLASH_iCache_EN);
    FLASH_PrefetchBufSet(FLASH_PrefetchBuf_DIS);
    RCC_ConfigSysclk(RCC_SYSCLK_SRC_HSE);
    while (RCC_GetSysclkSrc() != 0x04)
    	__NOP();
    /* init systick*/
    SysTick_Config(8000);
/* NTFx CODE END */
    return true;
}
/* NTFx CODE START */
/**
 *@brief Initializes the NVIC
 *@param null
 *@return status
 */
bool NVIC_Configuration(void)
{
    /*Configure the preemption priority and subpriority:
    - 4 bits for pre-emption priority: possible value are 0..15
    - 0 bits for subpriority: possible value are 0
    - Lower values gives higher priority
    */
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_4);
    
/* NTFx CODE END */
    return true;
}
/* NTFx CODE START */
/**
 *@brief Initializes the DMA
 *@param null
 *@return status
 */
bool DMA_Configuration(void)
{
/* NTFx CODE END */
    return true;
}
/* NTFx CODE START */
/**
 *@brief Initializes the GPIO
 *@param null
 *@return status
 */
bool GPIO_Configuration(void)
{
    GPIO_InitType GPIO_InitStructure;
     
    GPIO_InitStruct(&GPIO_InitStructure);
     
    /* Enable the GPIO clock*/
    RCC_EnableAPB2PeriphClk(RCC_APB2_PERIPH_GPIOB | RCC_APB2_PERIPH_GPIOA | RCC_APB2_PERIPH_GPIOD | RCC_APB2_PERIPH_GPIOC | RCC_APB2_PERIPH_GPIOE | RCC_APB2_PERIPH_AFIO, ENABLE);
    
     
    GPIO_SetBits(GPIOD,GPIO_PIN_1 | GPIO_PIN_2);
    GPIO_SetBits(GPIOC,GPIO_PIN_3);
    GPIO_SetBits(GPIOB,GPIO_PIN_4 | GPIO_PIN_0);
    GPIO_SetBits(GPIOE,GPIO_PIN_7);
     
    GPIO_ResetBits(GPIOB,GPIO_PIN_12 | GPIO_PIN_13);
     
    /*Initialize Out_OD GPIO */
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_Out_OD;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_2MHz;
    GPIO_InitStructure.Pin        =GPIO_PIN_1 | GPIO_PIN_2;
    GPIO_InitPeripheral(GPIOD,&GPIO_InitStructure);
     
    /*Initialize Out_PP GPIO */
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_2MHz;
    GPIO_InitStructure.Pin        =GPIO_PIN_3;
    GPIO_InitPeripheral(GPIOC,&GPIO_InitStructure);
     
    GPIO_InitStructure.Pin        =GPIO_PIN_12 | GPIO_PIN_13 | GPIO_PIN_4 | GPIO_PIN_0;
    GPIO_InitPeripheral(GPIOB,&GPIO_InitStructure);
     
    GPIO_InitStructure.Pin        =GPIO_PIN_7;
    GPIO_InitPeripheral(GPIOE,&GPIO_InitStructure);
     
    /*Initialize IPU  GPIO */
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_IPU;
    GPIO_InitStructure.GPIO_Speed = GPIO_INPUT;
    GPIO_InitStructure.Pin        =GPIO_PIN_10;
    GPIO_InitPeripheral(GPIOA,&GPIO_InitStructure);
     
    GPIO_InitStructure.Pin        =GPIO_PIN_8;
    GPIO_InitPeripheral(GPIOB,&GPIO_InitStructure);
     
    GPIO_InitStructure.Pin        =GPIO_PIN_6;
    GPIO_InitPeripheral(GPIOD,&GPIO_InitStructure);
     
    GPIO_InitStructure.Pin        =GPIO_PIN_11;
    GPIO_InitPeripheral(GPIOC,&GPIO_InitStructure);
     
    /*Initialize AF_PP GPIO */
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_2MHz;
    GPIO_InitStructure.Pin        =GPIO_PIN_9;
    GPIO_InitPeripheral(GPIOA,&GPIO_InitStructure);
     
    GPIO_InitStructure.Pin        =GPIO_PIN_9;
    GPIO_InitPeripheral(GPIOB,&GPIO_InitStructure);
     
    GPIO_InitStructure.Pin        =GPIO_PIN_5;
    GPIO_InitPeripheral(GPIOD,&GPIO_InitStructure);
     
    GPIO_InitStructure.Pin        =GPIO_PIN_10;
    GPIO_InitPeripheral(GPIOC,&GPIO_InitStructure);
     
    /*config SYSpin remap*/
    GPIO_ConfigPinRemap(GPIO_RMP_SW_JTAG_SW_ENABLE, ENABLE);
    /*config USART2pin remap*/
    GPIO_ConfigPinRemap(GPIO_RMP1_USART2, ENABLE);
    /*config CAN1pin remap*/
    GPIO_ConfigPinRemap(GPIO_RMP2_CAN1, ENABLE);
/* NTFx CODE END */
    return true;
}
/* NTFx CODE START */
/**
 *@brief Initializes the USART
 *@param null
 *@return status
 */
bool USART_Configuration(void)
{
    USART_InitType USART_InitStructure;
    USART_StructInit(&USART_InitStructure);
    /* EnableUSART2|UART4clock */
    RCC_EnableAPB1PeriphClk(RCC_APB1_PERIPH_USART2|RCC_APB1_PERIPH_UART4,ENABLE);
    /* EnableUSART1clock */
    RCC_EnableAPB2PeriphClk(RCC_APB2_PERIPH_USART1,ENABLE);
     
     
    /*********initialize the USART1************/
    USART_InitStructure.BaudRate            = 115200;
    USART_InitStructure.WordLength          = USART_WL_8B;
    USART_InitStructure.StopBits            = USART_STPB_1;
    USART_InitStructure.Parity              = USART_PE_NO; 
    USART_InitStructure.HardwareFlowControl = USART_HFCTRL_NONE; 
    USART_InitStructure.Mode                = USART_MODE_RX | USART_MODE_TX; 
    /* Configure USART1 */
    USART_Init(USART1, &USART_InitStructure);
     
    /* Enable the USART1 */
    USART_Enable(USART1, ENABLE);
     
    /*********initialize the USART2************/
    /* Configure USART2 */
    USART_Init(USART2, &USART_InitStructure);
     
    /* Enable the USART2 */
    USART_Enable(USART2, ENABLE);
     
    /*********initialize the UART4************/
    /* Configure UART4 */
    USART_Init(UART4, &USART_InitStructure);
     
    /* Enable the UART4 */
    USART_Enable(UART4, ENABLE);
/* NTFx CODE END */
    return true;
}
/* NTFx CODE START */
/**
 *@brief Initializes the CAN
 *@param null
 *@return status
 */
bool CAN_Configuration(void)
{
    CAN_InitType CAN_InitStructure;
    CAN_InitStruct(&CAN_InitStructure);
     
    /* Enable CAN1 clock */
    RCC_EnableAPB1PeriphClk(RCC_APB1_PERIPH_CAN1,ENABLE);
    /***************** CAN1 configuration ***************/
    /* CAN1 register deinit */
    CAN_DeInit(CAN1);
    CAN_InitStructure.TTCM              = DISABLE;
    CAN_InitStructure.ABOM              = ENABLE;
    CAN_InitStructure.AWKUM             = DISABLE;
    CAN_InitStructure.NART              = DISABLE;
    CAN_InitStructure.RFLM              = DISABLE;
    CAN_InitStructure.TXFP              = DISABLE;
    CAN_InitStructure.OperatingMode     = CAN_Normal_Mode;
    CAN_InitStructure.RSJW              = CAN_RSJW_1tq;
    CAN_InitStructure.TBS1              = CAN_TBS1_13tq;
    CAN_InitStructure.TBS2              = CAN_TBS2_2tq;
    CAN_InitStructure.BaudRatePrescaler = 2;
    /* Initializes theCAN1*/
    CAN_Init(CAN1, &CAN_InitStructure);
     
    //Use reference:
    //CAN_TransmitMessage(CANx, TxMessage); to transmit data,return TransmitMailbox
    //CAN_ReceiveMessage(CANx, CAN_FIFO0, &CAN_RxMessage[CAN_RxMessage_Write_Cursor]); to receive data 
/* NTFx CODE END */
    return true;
}
