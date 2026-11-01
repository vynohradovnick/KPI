#include "stage_probe.h"
#include "stage_inputs.h"
#include "stage_bus.h"
#include "stage_journal.h"
#include <stdio.h>
#include <string.h>

#define MPU_ADDRESS 0x68U
#define PIR_PIN GPIO_PIN_8
#define ADC_MARK GPIO_PIN_1

typedef struct {
    bool ready;
    uint8_t who;
    int32_t accel[3];
    int32_t gyro[3];
    uint32_t errors;
} StageImu;

static ADC_HandleTypeDef adc;

static bool adc_setup(void)
{
    __HAL_RCC_ADC_CLK_ENABLE();
    GPIO_InitTypeDef input = {
        .Pin = GPIO_PIN_0, .Mode = GPIO_MODE_ANALOG, .Pull = GPIO_NOPULL
    };
    HAL_GPIO_Init(GPIOA, &input);
    adc.Instance = ADC1;
    adc.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV4;
    adc.Init.Resolution = ADC_RESOLUTION_12B;
    adc.Init.DataAlign = ADC_DATAALIGN_RIGHT;
    adc.Init.ScanConvMode = ADC_SCAN_DISABLE;
    adc.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
    adc.Init.NbrOfConversion = 1;
    adc.Init.ExternalTrigConv = ADC_SOFTWARE_START;
    adc.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
    adc.Init.Overrun = ADC_OVR_DATA_PRESERVED;
    adc.Init.SamplingTimeCommon1 = ADC_SAMPLETIME_79CYCLES_5;
    adc.Init.TriggerFrequencyMode = ADC_TRIGGER_FREQ_HIGH;
    ADC_ChannelConfTypeDef channel = {
        .Channel = ADC_CHANNEL_0, .Rank = ADC_REGULAR_RANK_1,
        .SamplingTime = ADC_SAMPLINGTIME_COMMON_1
    };
    return HAL_ADC_Init(&adc) == HAL_OK &&
           HAL_ADCEx_Calibration_Start(&adc) == HAL_OK &&
           HAL_ADC_ConfigChannel(&adc, &channel) == HAL_OK;
}

static bool adc_read(uint16_t *raw)
{
    HAL_GPIO_WritePin(GPIOA, ADC_MARK, GPIO_PIN_SET);
    bool ok = HAL_ADC_Start(&adc) == HAL_OK;
    if (ok) {
        ok = HAL_ADC_PollForConversion(&adc, 2U) == HAL_OK;
        if (ok) {
            *raw = (uint16_t)HAL_ADC_GetValue(&adc);
        }
        (void)HAL_ADC_Stop(&adc);
    }
    HAL_GPIO_WritePin(GPIOA, ADC_MARK, GPIO_PIN_RESET);
    return ok;
}

static bool imu_read(StageImu *imu, uint8_t address)
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

typedef struct {
    uint32_t target_ms;
    uint32_t period_ms;
    uint32_t count;
    uint32_t missed;
    uint32_t max_late_ms;
    uint16_t max_us;
} StageTask;

typedef enum { WAITING, SHOWING, MOVING, SETTLING } StageMode;

typedef struct {
    StageMode mode;
    StageLight light;
    StageMotion motion;
    StageImu imu;
    uint32_t end_ms;
    uint32_t storage_errors;
    uint32_t journal_cursor;
    uint32_t journal_stop;
    uint16_t light_raw;
    uint16_t light_median;
    uint8_t mount_streak;
    bool mount;
    bool fault;
} StageDesk;

static const char *const mode_name[] = {"wait", "show", "move", "settling"};
static volatile uint8_t serial_rx[128];
static volatile uint8_t serial_write;
static volatile uint8_t serial_read;
static volatile uint32_t serial_drops;
static volatile bool serial_gap;

void USART2_IRQHandler(void)
{
    if ((USART2->ISR & USART_ISR_ORE) != 0U) {
        USART2->ICR = USART_ICR_ORECF;
        ++serial_drops;
        serial_gap = true;
    }
    if ((USART2->ISR & USART_ISR_RXNE_RXFNE) != 0U) {
        uint8_t data = (uint8_t)USART2->RDR;
        const uint8_t next = (serial_write + 1U) % sizeof serial_rx;
        if (next == serial_read) {
            ++serial_drops;
            serial_gap = true;
        } else {
            serial_rx[serial_write] = data;
            serial_write = next;
        }
    }
}

static void timebase_init(void)
{
    __HAL_RCC_TIM3_CLK_ENABLE();
    TIM3->PSC = SystemCoreClock / 1000000U - 1U;
    TIM3->ARR = 0xffffU;
    TIM3->EGR = TIM_EGR_UG;
    TIM3->CR1 = TIM_CR1_CEN;
}

static uint16_t micros(void)
{
    return (uint16_t)TIM3->CNT;
}

static bool due(uint32_t now, uint32_t target)
{
    return (int32_t)(now - target) >= 0;
}

static bool release_task(StageTask *task, uint32_t now)
{
    if (!due(now, task->target_ms)) {
        return false;
    }
    const uint32_t late = now - task->target_ms;
    const uint32_t skipped = late / task->period_ms;
    if (late > task->max_late_ms) {
        task->max_late_ms = late;
    }
    task->missed += skipped;
    task->target_ms += (skipped + 1U) * task->period_ms;
    ++task->count;
    return true;
}

static void record_duration(StageTask *task, uint16_t start)
{
    const uint16_t elapsed = micros() - start;
    if (elapsed > task->max_us) {
        task->max_us = elapsed;
    }
}

static void record_event(StageDesk *desk, uint32_t now, enum StageEvent event)
{
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_6, GPIO_PIN_SET);
    const bool stored = stage_journal_append(now, event);
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_6, GPIO_PIN_RESET);
    if (!stored) {
        ++desk->storage_errors;
    }
    printf("event=%u t_ms=%lu stored=%u\r\n", event, (unsigned long)now, stored);
}

static void status_line(const StageDesk *desk, uint32_t now)
{
    printf("t_ms=%lu mode=%s pir=%u adc=%u med=%u dim=%u imu=%s mount=%u "
           "a_mg=%ld,%ld,%ld log_err=%lu rx_drop=%lu\r\n",
           (unsigned long)now, mode_name[desk->mode], desk->motion.level,
           desk->light_raw, desk->light_median, desk->light.dim,
           desk->imu.ready ? "ok" : "stale", desk->mount,
           (long)desk->imu.accel[0], (long)desk->imu.accel[1], (long)desk->imu.accel[2],
           (unsigned long)desk->storage_errors, (unsigned long)serial_drops);
}

static void task_line(const char *name, const StageTask *task)
{
    printf("task=%s n=%lu miss=%lu late_max_ms=%lu work_max_us=%u\r\n",
           name, (unsigned long)task->count, (unsigned long)task->missed,
           (unsigned long)task->max_late_ms, task->max_us);
}

static void handle_command(StageDesk *desk, const char *command, uint32_t now,
                           const StageTask *pir, const StageTask *ldr, const StageTask *imu)
{
    if (strcmp(command, "status") == 0) {
        status_line(desk, now);
    } else if (strcmp(command, "move on") == 0) {
        desk->mode = MOVING;
        desk->mount = false;
        desk->mount_streak = 0;
        record_event(desk, now, STAGE_MOVE);
    } else if (strcmp(command, "move off") == 0 && desk->mode == MOVING) {
        desk->mode = SETTLING;
        desk->mount_streak = 0;
        puts("OK settling\r");
    } else if (strcmp(command, "log") == 0) {
        desk->journal_stop = stage_journal_latest();
        desk->journal_cursor = desk->journal_stop > 255U ? desk->journal_stop - 255U : 1U;
        printf("log begin last=%lu\r\n", (unsigned long)desk->journal_stop);
    } else if (strcmp(command, "metrics") == 0) {
        task_line("pir", pir);
        task_line("ldr", ldr);
        task_line("imu", imu);
    } else if (strcmp(command, "i2c fault") == 0) {
        desk->fault = true;
        puts("OK fixture 0x69\r");
    } else if (strcmp(command, "i2c restore") == 0) {
        desk->fault = false;
        puts("OK fixture 0x68\r");
    } else if (strcmp(command, "reset") == 0) {
        NVIC_SystemReset();
    } else {
        puts("ERR command\r");
    }
}

static void service_rx(StageDesk *desk, uint32_t now, const StageTask *pir,
                       const StageTask *ldr, const StageTask *imu)
{
    static char command[32];
    static unsigned used;
    static bool discard;
    if (serial_gap) {
        __disable_irq();
        serial_gap = false;
        serial_read = serial_write;
        __enable_irq();
        used = 0;
        discard = true;
    }
    for (unsigned budget = 0; budget < 16U && serial_read != serial_write; ++budget) {
        const char c = (char)serial_rx[serial_read];
        serial_read = (serial_read + 1U) % sizeof serial_rx;
        if (c == '\r' || c == '\n') {
            if (discard) {
                puts("ERR line\r");
            } else if (used != 0U) {
                command[used] = '\0';
                handle_command(desk, command, now, pir, ldr, imu);
            }
            used = 0;
            discard = false;
            return;
        }
        if (c < ' ' || c > '~' || used == sizeof command - 1U) {
            discard = true;
        } else if (!discard) {
            command[used++] = c;
        }
    }
}

static void service_log(StageDesk *desk)
{
    if (desk->journal_cursor == 0U) {
        return;
    }
    if (desk->journal_cursor > desk->journal_stop) {
        desk->journal_cursor = 0;
        puts("log end\r");
        return;
    }
    StageRecord record;
    if (stage_journal_read(desk->journal_cursor++, &record)) {
        printf("log n=%lu t_ms=%lu event=%lu\r\n", (unsigned long)record.number,
               (unsigned long)record.uptime_ms, (unsigned long)record.event);
    }
}

static uint32_t magnitude(int32_t value)
{
    return (uint32_t)(value < 0 ? -value : value);
}

static bool unstable(const StageImu *imu)
{
    return magnitude(imu->accel[0]) > 258U || magnitude(imu->accel[1]) > 258U ||
           imu->accel[2] < 850 || imu->accel[2] > 1150 ||
           magnitude(imu->gyro[0]) > 40000U || magnitude(imu->gyro[1]) > 40000U ||
           magnitude(imu->gyro[2]) > 40000U;
}

static void update_mount(StageDesk *desk, uint32_t now)
{
    if (desk->mode == MOVING) {
        return;
    }
    const bool bad = unstable(&desk->imu);
    const bool candidate = desk->mode == SETTLING ? !bad : bad;
    if (!candidate) {
        desk->mount_streak = 0;
        desk->mount = false;
        return;
    }
    if (desk->mount_streak < 3U) {
        ++desk->mount_streak;
    }
    if (desk->mount_streak == 3U && desk->mode == SETTLING) {
        desk->mode = WAITING;
        desk->mount_streak = 0;
        record_event(desk, now, STAGE_WAIT);
    } else if (desk->mount_streak == 3U && !desk->mount) {
        desk->mount = true;
        record_event(desk, now, STAGE_MOUNT);
    }
}

void stage_probe_run(I2C_HandleTypeDef *bus, UART_HandleTypeDef *uart)
{
    GPIO_InitTypeDef gpio = { .Pin = PIR_PIN, .Mode = GPIO_MODE_INPUT, .Pull = GPIO_PULLDOWN };
    HAL_GPIO_Init(GPIOA, &gpio);
    gpio.Pin = ADC_MARK | GPIO_PIN_6 | GPIO_PIN_7;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &gpio);

    (void)HAL_I2C_DeInit(bus);
    stage_bus_init();
    const bool adc_ready = adc_setup();
    timebase_init();
    HAL_NVIC_SetPriority(USART2_IRQn, 1U, 0U);
    HAL_NVIC_EnableIRQ(USART2_IRQn);
    __HAL_UART_ENABLE_IT(uart, UART_IT_RXNE);

    StageDesk desk = {0};
    const uint32_t start = HAL_GetTick();
    StageTask pir = {.target_ms = start, .period_ms = 10U};
    StageTask ldr = {.target_ms = start, .period_ms = 50U};
    StageTask imu = {.target_ms = start, .period_ms = 20U};
    uint32_t report_at = start;
    uint32_t retry_at = start;
    bool imu_reported = false;
    printf("StageCue lab3 | ADC=%s | log_last=%lu\r\n", adc_ready ? "ok" : "init-error",
           (unsigned long)stage_journal_latest());
    for (;;) {
        uint32_t now = HAL_GetTick();
        if (release_task(&pir, now)) {
            const uint16_t t = micros();
            HAL_GPIO_WritePin(GPIOA, GPIO_PIN_7, GPIO_PIN_SET);
            const bool edge = stage_motion_sample(&desk.motion,
                HAL_GPIO_ReadPin(GPIOA, PIR_PIN) == GPIO_PIN_SET);
            const bool begin = edge && desk.mode == WAITING;
            const bool end = desk.mode == SHOWING && due(now, desk.end_ms) && !edge;
            if (edge && (desk.mode == WAITING || desk.mode == SHOWING)) {
                desk.end_ms = now + 15000U;
                desk.mode = SHOWING;
            } else if (end) {
                desk.mode = WAITING;
            }
            const bool led = desk.mode == SHOWING &&
                (!desk.light.dim || ((now / 250U) & 1U) == 0U);
            HAL_GPIO_WritePin(Led_GPIO_Port, Led_Pin, led ? GPIO_PIN_SET : GPIO_PIN_RESET);
            HAL_GPIO_WritePin(GPIOA, GPIO_PIN_7, GPIO_PIN_RESET);
            record_duration(&pir, t);
            if (begin || end) {
                record_event(&desk, now, begin ? STAGE_SHOW : STAGE_WAIT);
            }
        }
        now = HAL_GetTick();
        if (release_task(&ldr, now)) {
            const uint16_t t = micros();
            if (adc_ready && adc_read(&desk.light_raw)) {
                desk.light_median = stage_light_sample(&desk.light, desk.light_raw);
            }
            record_duration(&ldr, t);
        }
        now = HAL_GetTick();
        if (release_task(&imu, now) && (desk.imu.ready || due(now, retry_at))) {
            const uint16_t t = micros();
            const bool ok = imu_read(&desk.imu, desk.fault ? MPU_ADDRESS + 1U : MPU_ADDRESS);
            record_duration(&imu, t);
            if (ok) {
                if (imu_reported) {
                    imu_reported = false;
                    record_event(&desk, now, STAGE_IMU_BACK);
                }
                update_mount(&desk, now);
            } else {
                desk.mount = false;
                desk.mount_streak = 0;
                retry_at = now + 500U;
                if (!imu_reported) {
                    imu_reported = true;
                    record_event(&desk, now, STAGE_IMU_LOST);
                }
            }
        }
        now = HAL_GetTick();
        service_rx(&desk, now, &pir, &ldr, &imu);
        service_log(&desk);
        if (due(now, report_at)) {
            report_at += 5000U;
            status_line(&desk, now);
        }
    }
}
