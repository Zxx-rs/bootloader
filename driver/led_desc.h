#ifndef _LED_DESC_H_
#define _LED_DESC_H_

#include "stm32f4xx.h"
#include "stm32f4xx_conf.h"

struct led_desc
{
    GPIO_TypeDef* Port;
    uint32_t Pin;
    BitAction OnBit;
    BitAction OffBit;
};

#endif
