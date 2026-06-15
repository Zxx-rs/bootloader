#ifndef __BL_UART_H
#define __BL_UART_H

#include <stdint.h>

typedef void (*uart_rx_callback_t)(const uint8_t* data, uint32_t size);

void bl_uart_init(void);
void bl_uart_write(uint8_t* data, uint32_t size);
void bl_uart_register_rx_callback(uart_rx_callback_t callback);

#endif // __BL_UART_H
