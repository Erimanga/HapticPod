#pragma once
#include <stdint.h>
typedef struct { uint8_t addr[6]; } bd_addr_t;
uint8_t ble_get_public_address(bd_addr_t *addr);
