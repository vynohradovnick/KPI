#include "stage_bus.h"
#include "stage_bus_pins.h"

enum { BUS_OK, BUS_NACK, BUS_CLOCK_LOW, BUS_DATA_LOW };
static uint32_t error;

static bool raise_clock(void)
{
    release(SCL);
    for (unsigned wait = 0; wait < 20U; ++wait) {
        half_period();
        if (high(SCL)) {
            return true;
        }
    }
    error = BUS_CLOCK_LOW;
    return false;
}

static bool start(void)
{
    release(SDA);
    half_period();
    if (!raise_clock()) {
        return false;
    }
    if (!high(SDA)) {
        error = BUS_DATA_LOW;
        return false;
    }
    drive_low(SDA);
    half_period();
    drive_low(SCL);
    return true;
}

static bool stop(void)
{
    drive_low(SDA);
    half_period();
    const bool clock = raise_clock();
    release(SDA);
    half_period();
    return clock;
}

static bool send_byte(uint8_t value)
{
    for (unsigned bit = 0; bit < 8U; ++bit) {
        if (value & 0x80U) {
            release(SDA);
        } else {
            drive_low(SDA);
        }
        half_period();
        if (!raise_clock()) {
            return false;
        }
        drive_low(SCL);
        value <<= 1;
    }
    release(SDA);
    half_period();
    if (!raise_clock()) {
        return false;
    }
    const bool ack = !high(SDA);
    drive_low(SCL);
    if (!ack) {
        error = BUS_NACK;
    }
    return ack;
}

static bool read_byte(uint8_t *value, bool ack)
{
    uint8_t byte = 0;
    release(SDA);
    for (unsigned bit = 0; bit < 8U; ++bit) {
        half_period();
        if (!raise_clock()) {
            return false;
        }
        byte = (uint8_t)((byte << 1) | high(SDA));
        drive_low(SCL);
    }
    if (ack) {
        drive_low(SDA);
    }
    half_period();
    if (!raise_clock()) {
        return false;
    }
    drive_low(SCL);
    release(SDA);
    *value = byte;
    return true;
}

static bool begin_transfer(void)
{
    error = BUS_OK;
    release(SCL | SDA);
    half_period();
    if (high(SCL) && !high(SDA)) {
        for (unsigned i = 0; i < 9U && !high(SDA); ++i) {
            drive_low(SCL);
            half_period();
            if (!raise_clock()) {
                return false;
            }
        }
        drive_low(SCL);
        if (!stop()) {
            return false;
        }
    }
    return start();
}

void stage_bus_init(void)
{
    init_pins();
}

bool stage_bus_read(uint8_t address, uint8_t reg, uint8_t *data, size_t size)
{
    bool ok = begin_transfer() && send_byte(address << 1) &&
              send_byte(reg) && start() && send_byte((address << 1) | 1U);
    for (size_t i = 0; ok && i < size; ++i) {
        ok = read_byte(&data[i], i + 1U < size);
    }
    return stop() && ok;
}

bool stage_bus_write(uint8_t address, uint8_t reg, uint8_t value)
{
    const bool ok = begin_transfer() && send_byte(address << 1) &&
                    send_byte(reg) && send_byte(value);
    return stop() && ok;
}

uint32_t stage_bus_error(void)
{
    return error;
}
