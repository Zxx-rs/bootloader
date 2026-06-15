#ifndef _KEY_DESC_H
#define _KEY_DESC_H

#include "stm32f4xx.h"
#include "stm32f4xx_conf.h"

struct key_desc
{
    GPIO_TypeDef *port;
    uint16_t pin;
    GPIOPuPd_TypeDef pupd;
    BitAction press_level;
};

#endif
