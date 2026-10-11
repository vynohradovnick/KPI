#ifndef STAGE_INPUTS_H
#define STAGE_INPUTS_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint16_t window[5];
    uint8_t used;
    uint8_t next;
    bool dim;
} StageLight;

typedef struct {
    bool level;
    bool pending;
} StageMotion;

uint16_t stage_light_sample(StageLight *light, uint16_t raw);
bool stage_motion_sample(StageMotion *motion, bool raw);
int32_t stage_accel_mg(const uint8_t *raw);
int32_t stage_gyro_mdps(const uint8_t *raw);

#endif
