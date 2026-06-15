#include "key.h"
#include "key_desc.h"
#include "stdlib.h"
#include <stdbool.h>
#include "stdio.h"


void key_init(key_desc_t key)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    GPIO_StructInit(&GPIO_InitStructure);
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN;
    GPIO_InitStructure.GPIO_PuPd = key->pupd;
    GPIO_InitStructure.GPIO_Speed = GPIO_Medium_Speed;
    GPIO_InitStructure.GPIO_Pin = key->pin;
    GPIO_Init(key->port, &GPIO_InitStructure);
}

bool key_read(key_desc_t key)
{
    return GPIO_ReadInputDataBit(key->port, key->pin) == key->press_level;//按下为true，松开为false
}

