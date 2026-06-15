#include "stm32f4xx.h"
#include "bl_uart.h"

// UART3_TX  PD8
// UART3_RX  PD9
// Baudrate 115200
// Data bits 8-N-1
// DMA: TX RX
// IT: UART_TX/DMA_TC/DMA_RX_TC

static uart_rx_callback_t rx_callback = 0;

static void uart_io_init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct;
    GPIO_StructInit(&GPIO_InitStruct);
    GPIO_InitStruct.GPIO_Pin = GPIO_Pin_8 | GPIO_Pin_9;
    GPIO_InitStruct.GPIO_Mode = GPIO_Mode_AF;
    GPIO_InitStruct.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStruct.GPIO_OType = GPIO_OType_PP;
    GPIO_InitStruct.GPIO_PuPd = GPIO_PuPd_UP;
    GPIO_Init(GPIOD, &GPIO_InitStruct);

    GPIO_PinAFConfig(GPIOD, GPIO_PinSource8, GPIO_AF_USART3);
    GPIO_PinAFConfig(GPIOD, GPIO_PinSource9, GPIO_AF_USART3);
}
/* static void uart_dma_init(void)
{
    DMA_InitTypeDef DMA_InitStruct;
    DMA_StructInit(&DMA_InitStruct);

    //RX
    DMA_InitStruct.DMA_Channel = DMA_Channel_4;
    DMA_InitStruct.DMA_PeripheralBaseAddr = (uint32_t)&USART3->DR;
    DMA_InitStruct.DMA_Memory0BaseAddr = 0; //set later
    DMA_InitStruct.DMA_DIR = DMA_DIR_PeripheralToMemory;
    DMA_InitStruct.DMA_BufferSize = 0; //set later
    DMA_InitStruct.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
    DMA_InitStruct.DMA_MemoryInc = DMA_MemoryInc_Enable;
    DMA_InitStruct.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;
    DMA_InitStruct.DMA_MemoryDataSize = DMA_MemoryDataSize_Byte;
    DMA_InitStruct.DMA_Mode = DMA_Mode_Normal;
    DMA_InitStruct.DMA_Priority = DMA_Priority_Medium;
    DMA_InitStruct.DMA_FIFOThreshold = DMA_FIFOThreshold_Full;
    DMA_InitStruct.DMA_MemoryBurst = DMA_MemoryBurst_INC16;
    DMA_InitStruct.DMA_PeripheralBurst = DMA_PeripheralBurst_Single;
    DMA_Init(DMA1_Stream1, &DMA_InitStruct);
    DMA_IT_TCConfig(DMA1_Stream1, DMA_IT_TC, ENABLE);//开启中断
    DMA_Cmd(DMA1_Stream1, ENABLE);

    //TX
    DMA_InitStruct.DMA_Channel = DMA_Channel_4;
    DMA_InitStruct.DMA_PeripheralBaseAddr = (uint32_t)&USART3->DR;
    DMA_InitStruct.DMA_Memory0BaseAddr = 0; //set later
    DMA_InitStruct.DMA_DIR = DMA_DIR_MemoryToPeripheral;
    DMA_InitStruct.DMA_BufferSize = 0;//set later
    DMA_InitStruct.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
    DMA_InitStruct.DMA_MemoryInc = DMA_MemoryInc_Enable;
    DMA_InitStruct.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;
    DMA_InitStruct.DMA_MemoryDataSize = DMA_MemoryDataSize_Byte;
    DMA_InitStruct.DMA_Mode = DMA_Mode_Normal;
    DMA_InitStruct.DMA_Priority = DMA_Priority_Medium;
    DMA_InitStruct.DMA_FIFOThreshold = DMA_FIFOThreshold_Full;
    DMA_InitStruct.DMA_MemoryBurst = DMA_MemoryBurst_INC16;
    DMA_InitStruct.DMA_PeripheralBurst = DMA_PeripheralBurst_Single;
    DMA_Init(DMA1_Stream3, &DMA_InitStruct);
    DMA_IT_TCConfig(DMA1_Stream3, DMA_IT_TC, ENABLE);//开启中断
    DMA_Cmd(DMA1_Stream3, ENABLE);
}  */

static void uart_iqr_init(void)
{
    // RX
    NVIC_InitTypeDef NVIC_InitStruct;
    NVIC_InitStruct.NVIC_IRQChannel = USART3_IRQn;
    NVIC_InitStruct.NVIC_IRQChannelPreemptionPriority = 5;
    NVIC_InitStruct.NVIC_IRQChannelSubPriority = 0;
    NVIC_InitStruct.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStruct);
    NVIC_SetPriority(USART3_IRQn, 5);

    /*     //DMA_TX
        NVIC_InitStruct.NVIC_IRQChannel = DMA1_Stream3_IRQn;
        NVIC_InitStruct.NVIC_IRQChannelPreemptionPriority = 5;
        NVIC_InitStruct.NVIC_IRQChannelSubPriority = 0;
        NVIC_InitStruct.NVIC_IRQChannelCmd = ENABLE;
        NVIC_Init(&NVIC_InitStruct);
        NVIC_SetPriority(DMA1_Stream3_IRQn,5);

        //DMA_RX
        NVIC_InitStruct.NVIC_IRQChannel = DMA1_Stream1_IRQn;
        NVIC_InitStruct.NVIC_IRQChannelPreemptionPriority = 5;
        NVIC_InitStruct.NVIC_IRQChannelSubPriority = 0;
        NVIC_InitStruct.NVIC_IRQChannelCmd = ENABLE;
        NVIC_Init(&NVIC_InitStruct);
        NVIC_SetPriority(DMA1_Stream1_IRQn,5); */
}

static void uart_lowlevel_init(void)
{
    USART_InitTypeDef USART_InitStruct;
    USART_StructInit(&USART_InitStruct);
    USART_InitStruct.USART_BaudRate = 115200;
    USART_InitStruct.USART_WordLength = USART_WordLength_8b;
    USART_InitStruct.USART_StopBits = USART_StopBits_1;
    USART_InitStruct.USART_Parity = USART_Parity_No;
    USART_InitStruct.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStruct.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
    USART_Init(USART3, &USART_InitStruct);
    USART_ITConfig(USART3, USART_IT_RXNE, ENABLE);
    //    USART_DMACmd(USART3, USART_DMAReq_Tx, ENABLE);//开启串口的 DMA 请求
    //    USART_DMACmd(USART3, USART_DMAReq_Rx, ENABLE);
    USART_Cmd(USART3, ENABLE);
}

void bl_uart_init(void)
{
    // uart_dma_init();
    uart_iqr_init();
    uart_lowlevel_init();
    uart_io_init();
}

void bl_uart_write(uint8_t *data, uint32_t size)
{
    /* //DMA transfer 65535 bytes at most, so we need to split the data into chunks
    while(size > 0)    {
        uint32_t chunk_size = size > 65535 ? 65535 : size;
        //set memory address and buffer size
        DMA1_Stream3->M0AR = (uint32_t)data;
        DMA1_Stream3->NDTR = chunk_size;
        //enable DMA stream
        DMA_Cmd(DMA1_Stream3, ENABLE);
        //wait for transfer complete
        while(DMA_GetCmdStatus(DMA1_Stream3) != DISABLE);
        //update data pointer and size
        data += chunk_size;
        size -= chunk_size;
    } */
    while (size--)
    {
        USART_SendData(USART3, *data++);
        while (USART_GetFlagStatus(USART3, USART_FLAG_TC) == RESET);
    }
}

void bl_uart_register_rx_callback(uart_rx_callback_t callback)
{
    rx_callback = callback;
}
void USART3_IRQHandler(void)
{
    // handle UART interrupt if needed
    if (USART_GetITStatus(USART3, USART_IT_RXNE) != RESET)
    {
        if (rx_callback)
        {
            uint8_t data = USART_ReceiveData(USART3);
            rx_callback(&data, 1);
        }
        USART_ClearITPendingBit(USART3, USART_IT_RXNE);
    }
}

/* void DMA1_Stream3_IRQHandler(void)
{
    //handle DMA TX complete interrupt if needed
    if(DMA_GetFlagStatus(DMA1_Stream3, DMA_IT_TC) != RESET)
    {
        //clear interrupt flag
        DMA_ClearFlag(DMA1_Stream3, DMA_IT_TC);
    }
}

void DMA1_Stream1_IRQHandler(void)
{
    //handle DMA RX complete interrupt if needed
    if(DMA_GetFlagStatus(DMA1_Stream1, DMA_IT_TC) != RESET)
    {
        //clear interrupt flag
        DMA_ClearFlag(DMA1_Stream1, DMA_IT_TC);
    }
} */
