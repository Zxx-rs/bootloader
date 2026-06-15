#ifndef __KEY_H
#define __KEY_H

#include <stdint.h>
#include <stdbool.h>

struct key_desc;
typedef struct key_desc *key_desc_t;

void key_init(key_desc_t key);
bool key_read(key_desc_t key);
	
#endif
