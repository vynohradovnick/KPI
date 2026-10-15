#ifndef STAGE_BUS_H
#define STAGE_BUS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void stage_bus_init(void);
bool stage_bus_read(uint8_t address, uint8_t reg, uint8_t *data, size_t size);
bool stage_bus_write(uint8_t address, uint8_t reg, uint8_t value);
uint32_t stage_bus_error(void);

#endif
