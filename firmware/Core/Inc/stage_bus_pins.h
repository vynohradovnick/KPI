#ifndef STAGE_BUS_PINS_H
#define STAGE_BUS_PINS_H

#include "main.h"
#include <stdbool.h>

#define SCL GPIO_PIN_8
#define SDA GPIO_PIN_9

static void half_period(void)
{
    const uint32_t start = SysTick->VAL;
    const uint32_t reload = SysTick->LOAD + 1U;
    const uint32_t cycles = SystemCoreClock / 200000U;
    uint32_t elapsed;
    do {
        const uint32_t now = SysTick->VAL;
        elapsed = start >= now ? start - now : start + reload - now;
    } while (elapsed < cycles);
}

static uint32_t output_mode(uint32_t pins)
{
    return ((pins & SCL) << 8) | ((pins & SDA) << 9);
}

static void drive_low(uint32_t pins)
{
    GPIOB->BSRR = pins << 16;
    GPIOB->MODER |= output_mode(pins);
}

static void release(uint32_t pins)
{
    GPIOB->MODER &= ~output_mode(pins);
}

static bool high(uint32_t pin)
{
    return (GPIOB->IDR & pin) != 0U;
}

static void init_pins(void)
{
    GPIO_InitTypeDef config = {
        .Pin = SCL | SDA, .Mode = GPIO_MODE_INPUT, .Pull = GPIO_PULLUP
    };
    GPIOB->BSRR = (SCL | SDA) << 16;
    HAL_GPIO_Init(GPIOB, &config);
}

#endif
