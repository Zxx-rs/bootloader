#include "stdint.h"
#include "stdio.h"
#include "stdbool.h"
#include "ringbuffer.h"
struct ringbuffer
{
    uint16_t size;    // 环形缓冲区的大小
    uint16_t head;    // 写入数据的位置
    uint16_t tail;    // 读取数据的位置
    uint8_t buffer[]; //环形缓冲区的数据存储区域，C语言中的柔性数组。它不占用结构体的大小（sizeof(struct ringbuffer) 只计算 size, head, tail 的和）
}; 

rb_t rb_new(uint8_t *buffer, uint16_t length)
{
    if (length < sizeof(struct ringbuffer) + 1)
        return NULL; // 缓冲区太小，无法容纳ringbuffer结构

    rb_t rb = (rb_t)buffer;
    rb->head = 0;
    rb->tail = 0;
    rb->size = length - sizeof(struct ringbuffer);
    return rb;
}
static inline uint16_t next_head(rb_t rb)
{
    return rb->head + 1 < rb->size ? rb->head + 1 : 0;
}
    
static inline uint16_t next_tail(rb_t rb)
{
    return rb->tail + 1 < rb->size ? rb->tail + 1 : 0;
}

bool rb_empty(rb_t rb)
{
    return rb->head == rb->tail;
}

bool rb_full(rb_t rb)
{
    return next_head(rb) == rb->tail;
}

bool rb_put(rb_t rb, uint8_t data)
{
    if (rb_full(rb))
        return false; // 缓冲区已满，无法写入数据

    rb->buffer[rb->head] = data;
    rb->head = next_head(rb);
    return true;
}

bool rb_get(rb_t rb, uint8_t *data)
{
    if (rb_empty(rb))
        return false; // 缓冲区为空，无法读取数据

    *data = rb->buffer[rb->tail];
    rb->tail = next_tail(rb);
    return true;
}

bool rb_puts(rb_t rb, const uint8_t *data, uint16_t length)
{
    while(length--)
    {
        if (!rb_put(rb, *data++))
            return false; // 缓冲区已满，无法写入更多数据
    }
    return true;
}

uint16_t rb_gets(rb_t rb, uint8_t *data, uint16_t length)
{
    uint16_t count = 0;
    while(length--)
    {
        if(!rb_get(rb, data++))
            break; // 缓冲区为空，无法读取更多数据
            count++;
    }
    return count; // 返回实际读取的数据长度
}
