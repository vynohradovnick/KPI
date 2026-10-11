#include "stage_inputs.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
    StageMotion motion = {0};
    assert(!stage_motion_sample(&motion, true));
    assert(!stage_motion_sample(&motion, false));
    assert(!stage_motion_sample(&motion, true));
    assert(stage_motion_sample(&motion, true));
    assert(motion.level && !stage_motion_sample(&motion, true));
    assert(!stage_motion_sample(&motion, false));
    assert(!stage_motion_sample(&motion, false) && !motion.level);

    StageLight light = {0};
    const uint16_t samples[] = {200, 220, 4095, 240, 260};
    for (unsigned i = 0; i < 5; ++i) {
        (void)stage_light_sample(&light, samples[i]);
    }
    assert(!light.dim && stage_light_sample(&light, 250) == 250);
    for (unsigned i = 0; i < 5; ++i) {
        (void)stage_light_sample(&light, 1900);
    }
    assert(light.dim);
    for (unsigned i = 0; i < 5; ++i) {
        (void)stage_light_sample(&light, 1600);
    }
    assert(light.dim);
    for (unsigned i = 0; i < 5; ++i) {
        (void)stage_light_sample(&light, 1400);
    }
    assert(!light.dim);

    assert(stage_accel_mg((const uint8_t[]){0x40, 0x00}) == 1000);
    assert(stage_accel_mg((const uint8_t[]){0xc0, 0x00}) == -1000);
    assert(stage_accel_mg((const uint8_t[]){0x80, 0x00}) == -2000);
    assert(stage_gyro_mdps((const uint8_t[]){0xff, 0x7d}) == -1000);
    puts("StageCue input tests passed: PIR edge, median, hysteresis, signed MPU conversion");
}
