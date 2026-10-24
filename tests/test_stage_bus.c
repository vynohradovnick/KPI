#include "stage_bus.h"
#include "stage_imu.h"
#include "stage_bus_pins.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint32_t pin;
    bool level;
    bool sda_driven_low;
} Sample;

static Sample samples[512];
static size_t count, pos;
static uint32_t driven_low;
static bool initialized;

void init_pins(void)
{
    initialized = true;
    driven_low = 0;
}

void half_period(void) {}

void drive_low(uint32_t pins)
{
    assert(initialized);
    driven_low |= pins;
}

void release(uint32_t pins)
{
    assert(initialized);
    driven_low &= ~pins;
}

bool high(uint32_t pin)
{
    assert(pos < count);
    const Sample s = samples[pos++];
    assert(pin == s.pin);
    assert(!(driven_low & pin));
    assert(!!(driven_low & SDA) == s.sda_driven_low);
    return s.level;
}

static void expect(uint32_t pin, bool level, bool data_low)
{
    assert(count < sizeof samples / sizeof samples[0]);
    samples[count++] = (Sample){pin, level, data_low};
}

static void reset(void)
{
    count = pos = 0;
    initialized = false;
    stage_bus_init();
    assert(initialized);
}

static void complete(void)
{
    assert(pos == count);
    assert(!driven_low);
}

static void scl(bool data_low) { expect(SCL, true, data_low); }
static void sda(bool level) { expect(SDA, level, false); }

static void begin(void)
{
    scl(false);
    sda(true);  /* bus idle */
    scl(false);
    sda(true);  /* START */
}

static void timeout(bool data_low)
{
    for (unsigned i = 0; i < 20; ++i)
        expect(SCL, false, data_low);
}

static void byte_out(uint8_t value, bool ack)
{
    for (unsigned i = 0; i < 8; ++i) {
        scl(!(value & (0x80U >> i)));
    }
    scl(false);  /* slave must own SDA for ACK */
    sda(!ack);
}

static void byte_in(uint8_t value, bool ack)
{
    for (unsigned i = 0; i < 8; ++i) {
        scl(false);
        sda(!!(value & (0x80U >> i)));
    }
    scl(ack);  /* master ACKs only when another byte follows */
}

static void read_address(uint8_t reg)
{
    begin();
    byte_out(0xD0, true);
    byte_out(reg, true);
    scl(false);
    sda(true);  /* repeated START */
    byte_out(0xD1, true);
}

int main(void)
{
    /* Exact bytes seen in the Wokwi logic-analyzer capture. */
    const uint8_t capture[14] = {
        0x00, 0x00, 0x00, 0x00, 0x40, 0x00, 0xEF, 0x5C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };
    uint8_t frame[sizeof capture] = {0};

    reset();
    read_address(0x3B);
    for (size_t i = 0; i < sizeof capture; ++i)
        byte_in(capture[i], i + 1 < sizeof capture);
    scl(true);  /* STOP */
    assert(stage_bus_read(0x68, 0x3B, frame, sizeof frame));
    assert(stage_bus_error() == 0 && memcmp(frame, capture, sizeof frame) == 0);
    complete();

    reset();
    begin();
    byte_out(0xD2, false);
    scl(true);
    assert(!stage_bus_read(0x69, 0x75, frame, 1) && stage_bus_error() == 1);
    complete();

    reset();
    expect(SCL, false, false);  /* busy line before START */
    timeout(false);
    timeout(true);  /* STOP also cannot release a clock held low */
    assert(!stage_bus_read(0x68, 0x75, frame, 1) && stage_bus_error() == 2);
    complete();

    reset();
    begin();
    timeout(false);  /* MSB of address 0xD0 is high */
    scl(true);
    assert(!stage_bus_write(0x68, 0x6B, 0x01) && stage_bus_error() == 2);
    complete();

    reset();
    begin();
    for (unsigned i = 0; i < 8; ++i)
        scl(!(0xD0U & (0x80U >> i)));
    timeout(false);  /* clock fails while waiting for ACK */
    scl(true);
    assert(!stage_bus_write(0x68, 0x6B, 0x01) && stage_bus_error() == 2);
    complete();

    reset();
    read_address(0x3B);
    timeout(false);  /* clock fails in the data phase */
    scl(true);
    assert(!stage_bus_read(0x68, 0x3B, frame, sizeof frame) && stage_bus_error() == 2);
    complete();

    reset();
    read_address(0x75);
    for (unsigned i = 0; i < 8; ++i) {
        scl(false);
        sda(!!(0x68U & (0x80U >> i)));
    }
    timeout(false);  /* final master NACK cycle fails */
    scl(true);
    assert(!stage_bus_read(0x68, 0x75, frame, 1) && stage_bus_error() == 2);
    complete();

    reset();
    scl(false);
    sda(false);
    for (unsigned i = 0; i < 9; ++i) {
        sda(false);
        scl(false);
    }
    scl(true);
    scl(false);
    sda(false);  /* SDA remained low after recovery clocks */
    scl(true);
    assert(!stage_bus_read(0x68, 0x75, frame, 1) && stage_bus_error() == 3);
    complete();

    reset();
    scl(false);
    sda(false);
    sda(false);
    timeout(false);  /* recovery fails on its first clock pulse */
    timeout(true);
    assert(!stage_bus_write(0x68, 0x6B, 0x01) && stage_bus_error() == 2);
    complete();

    reset();
    scl(false);
    sda(false);
    sda(true);
    timeout(true);  /* recovery STOP fails */
    scl(true);
    assert(!stage_bus_write(0x68, 0x6B, 0x01) && stage_bus_error() == 2);
    complete();

    reset();
    scl(false);
    sda(false);
    sda(false);
    scl(false);
    sda(true);  /* slave released SDA after a recovery pulse */
    scl(true);
    scl(false);
    sda(true);
    byte_out(0xD0, true);
    byte_out(0x6B, true);
    byte_out(0x01, true);
    scl(true);
    assert(stage_bus_write(0x68, 0x6B, 0x01) && stage_bus_error() == 0);
    complete();

    StageImu imu = {0};
    reset();
    read_address(0x75);
    byte_in(0x69, false);  /* device answered at 0x68, but its ID is wrong */
    scl(true);
    assert(!stage_imu_read(&imu, 0x68) && !imu.ready);
    complete();  /* rejects the ID before wake-up or a data read */

    reset();
    read_address(0x75);
    byte_in(0x68, false);
    scl(true);
    begin();
    byte_out(0xD0, true);
    byte_out(0x6B, true);
    byte_out(0x01, true);
    scl(true);
    read_address(0x3B);
    for (size_t i = 0; i < sizeof capture; ++i)
        byte_in(capture[i], i + 1 < sizeof capture);
    scl(true);
    assert(stage_imu_read(&imu, 0x68) && imu.ready && imu.who == 0x68);
    assert(imu.accel[0] == 0 && imu.accel[1] == 0 && imu.accel[2] == 1000);
    assert(imu.gyro[0] == 0 && imu.gyro[1] == 0 && imu.gyro[2] == 0);
    complete();

    reset();
    begin();
    byte_out(0xD2, false);
    scl(true);
    assert(!stage_imu_read(&imu, 0x69) && !imu.ready);
    complete();

    reset();
    begin();
    byte_out(0xD2, false);
    scl(true);
    assert(!stage_imu_read(&imu, 0x69) && !imu.ready);
    complete();

    reset();
    read_address(0x75);
    byte_in(0x68, false);
    scl(true);
    begin();
    byte_out(0xD0, true);
    byte_out(0x6B, true);
    byte_out(0x01, false);
    scl(true);
    assert(!stage_imu_read(&imu, 0x68) && !imu.ready);
    complete();

    puts("StageCue I2C tests passed: captured read, write, NACK, clock timeout, SDA recovery, IMU ID and frame");
}
