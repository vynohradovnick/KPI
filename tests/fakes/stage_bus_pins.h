#ifndef STAGE_BUS_TEST_PINS_H
#define STAGE_BUS_TEST_PINS_H

#include <stdbool.h>
#include <stdint.h>

#define SCL (1U << 8)
#define SDA (1U << 9)

void half_period(void);
void drive_low(uint32_t pins);
void release(uint32_t pins);
bool high(uint32_t pin);
void init_pins(void);

#endif
