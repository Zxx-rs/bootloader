#include <stdio.h>
#include "stm32f4xx.h"
#include "key.h"
#include "key_desc.h"
#include "led.h"
#include "led_desc.h"

void board_lowlevel_init(void)
{
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA, ENABLE);
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOB, ENABLE);
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOC, ENABLE);
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOD, ENABLE);
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOE, ENABLE);
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART3, ENABLE);
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_DMA1, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM6, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_I2C1, ENABLE);  /* BL24C512 EEPROM */
}


static struct led_desc _led0 = { GPIOE, GPIO_Pin_5, Bit_RESET, Bit_SET};
static struct led_desc _led1 = { GPIOE, GPIO_Pin_6, Bit_RESET, Bit_SET};
static struct led_desc _led2 = { GPIOC, GPIO_Pin_13, Bit_RESET, Bit_SET};
led_desc_t led0 = &_led0;
led_desc_t led1 = &_led1;
led_desc_t led2 = &_led2;


static struct key_desc _key3 = { GPIOE, GPIO_Pin_1, GPIO_PuPd_UP, Bit_RESET};
static struct key_desc _key4 = { GPIOE, GPIO_Pin_0, GPIO_PuPd_UP, Bit_RESET};

key_desc_t key3 = &_key3;
key_desc_t key4 = &_key4;

int fputc(int ch, FILE *f)
{
    USART_SendData(USART1, (uint8_t)ch);
    while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET);
    return ch;
}


