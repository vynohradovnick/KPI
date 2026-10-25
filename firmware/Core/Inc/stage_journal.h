#ifndef STAGE_JOURNAL_H
#define STAGE_JOURNAL_H

#include <stdbool.h>
#include <stdint.h>

enum StageEvent {
    STAGE_SHOW = 1, STAGE_WAIT, STAGE_MOVE, STAGE_MOUNT,
    STAGE_IMU_LOST, STAGE_IMU_BACK
};

typedef struct {
    uint32_t number;
    uint32_t uptime_ms;
    uint32_t event;
    uint32_t seal;
} StageRecord;

uint32_t stage_journal_latest(void);
bool stage_journal_append(uint32_t uptime_ms, enum StageEvent event);
bool stage_journal_read(uint32_t number, StageRecord *record);

#endif
