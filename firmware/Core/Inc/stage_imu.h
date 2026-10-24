#ifndef STAGE_IMU_H
#define STAGE_IMU_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool ready;
    uint8_t who;
    int32_t accel[3];
    int32_t gyro[3];
    uint32_t errors;
} StageImu;

bool stage_imu_read(StageImu *imu, uint8_t address);

#endif
