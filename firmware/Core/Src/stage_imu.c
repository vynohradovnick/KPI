#include "stage_imu.h"
#include "stage_bus.h"
#include "stage_inputs.h"

bool stage_imu_read(StageImu *imu, uint8_t address)
{
    if (!imu->ready) {
        uint8_t who = 0;
        if (!stage_bus_read(address, 0x75U, &who, 1U) ||
            who != 0x68U ||
            !stage_bus_write(address, 0x6BU, 1U)) {
            return false;
        }
        imu->who = who;
        imu->ready = true;
    }
    uint8_t frame[14];
    if (!stage_bus_read(address, 0x3BU, frame, sizeof frame)) {
        imu->ready = false;
        return false;
    }
    for (unsigned i = 0; i < 3U; ++i) {
        imu->accel[i] = stage_accel_mg(&frame[2U * i]);
        imu->gyro[i] = stage_gyro_mdps(&frame[8U + 2U * i]);
    }
    return true;
}
