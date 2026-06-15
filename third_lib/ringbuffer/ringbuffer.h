#ifndef _RINGBUFFER_H
#define _RINGBUFFER_H

#include "stdint.h"
#include "stdbool.h"

struct ringbuffer;
typedef struct ringbuffer *rb_t;

rb_t rb_new(uint8_t *buffer, uint16_t length);
bool rb_empty(rb_t rb);
bool rb_full(rb_t rb);
bool rb_put(rb_t rb, uint8_t data);
bool rb_get(rb_t rb, uint8_t *data);
bool rb_puts(rb_t rb, const uint8_t *data, uint16_t length);
uint16_t rb_gets(rb_t rb, uint8_t *data, uint16_t length);

#endif // _RINGBUFFER_H
