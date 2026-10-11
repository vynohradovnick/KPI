#include "stage_inputs.h"

uint16_t stage_light_sample(StageLight *light, uint16_t raw)
{
    light->window[light->next] = raw;
    light->next = (light->next + 1U) % 5U;
    if (light->used < 5U) {
        ++light->used;
    }
    uint16_t ordered[5];
    for (uint8_t i = 0; i < light->used; ++i) {
        uint8_t pos = i;
        while (pos > 0 && ordered[pos - 1U] > light->window[i]) {
            ordered[pos] = ordered[pos - 1U];
            --pos;
        }
        ordered[pos] = light->window[i];
    }
    const uint16_t median = ordered[light->used / 2U];
    if (light->used == 5U) {
        if (median >= 1800U) {
            light->dim = true;
        } else if (median <= 1400U) {
            light->dim = false;
        }
    }
    return median;
}

bool stage_motion_sample(StageMotion *motion, bool raw)
{
    if (raw == motion->level) {
        motion->pending = false;
        return false;
    }
    if (!motion->pending) {
        motion->pending = true;
        return false;
    }
    motion->level = raw;
    motion->pending = false;
    return raw;
}

static int32_t signed_word(const uint8_t *raw)
{
    const uint32_t word = ((uint32_t)raw[0] << 8) | raw[1];
    return word < 0x8000U ? (int32_t)word : (int32_t)word - 65536;
}

int32_t stage_accel_mg(const uint8_t *raw)
{
    return signed_word(raw) * 1000 / 16384;
}

int32_t stage_gyro_mdps(const uint8_t *raw)
{
    return signed_word(raw) * 1000 / 131;
}
