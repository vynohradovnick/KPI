#include "stage_probe.h"
#include "stage_inputs.h"
#include "stage_bus.h"
#include "stage_imu.h"
#include <stdio.h>
#include <string.h>

#define MPU_ADDRESS 0x68U
#define PIR_PIN GPIO_PIN_8
#define ADC_MARK GPIO_PIN_1

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

static bool due(uint32_t now, uint32_t target)
{
    return (int32_t)(now - target) >= 0;
}

static void probe_command(UART_HandleTypeDef *uart, bool *fault)
{
    static char command[20];
    static uint8_t used;
    static bool overlong;
    if (__HAL_UART_GET_FLAG(uart, UART_FLAG_ORE)) {
        __HAL_UART_CLEAR_OREFLAG(uart);
        overlong = true;
    }
    if (!__HAL_UART_GET_FLAG(uart, UART_FLAG_RXNE)) {
        return;
    }
    const char c = (char)uart->Instance->RDR;
    if (c != '\r' && c != '\n') {
        if (used < sizeof command - 1U) {
            command[used++] = c;
        } else {
            overlong = true;
        }
        return;
    }
    if (used || overlong) {
        command[used] = '\0';
        if (!overlong && strcmp(command, "i2c fault") == 0) {
            *fault = true;
            puts("I2C fixture: address 0x69\r");
        } else if (!overlong && strcmp(command, "i2c restore") == 0) {
            *fault = false;
            puts("I2C fixture: address 0x68\r");
        } else {
            puts("ERR command\r");
        }
    }
    used = 0;
    overlong = false;
}

void stage_probe_run(I2C_HandleTypeDef *bus, UART_HandleTypeDef *uart)
{
    GPIO_InitTypeDef gpio = { .Pin = PIR_PIN, .Mode = GPIO_MODE_INPUT, .Pull = GPIO_PULLDOWN };
    HAL_GPIO_Init(GPIOA, &gpio);
    gpio.Pin = ADC_MARK;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &gpio);

    (void)HAL_I2C_DeInit(bus);
    stage_bus_init();
    const bool adc_ready = adc_setup();
    StageLight light = {0};
    StageMotion motion = {0};
    StageImu imu = {0};
    uint16_t raw = 0, median = 0;
    uint32_t adc_errors = 0, i2c_at = 0, adc_at = 0, pir_at = 0, print_at = 0;
    bool fault = false;
    printf("StageCue lab2 | ADC=%s | PIR 10 ms | ADC 50 ms | IMU 20 ms\r\n",
           adc_ready ? "ready" : "init-error");
    for (;;) {
        const uint32_t now = HAL_GetTick();
        probe_command(uart, &fault);
        if (due(now, pir_at)) {
            pir_at = now + 10U;
            if (stage_motion_sample(&motion, HAL_GPIO_ReadPin(GPIOA, PIR_PIN) == GPIO_PIN_SET)) {
                printf("PIR rising t_ms=%lu\r\n", (unsigned long)now);
            }
            HAL_GPIO_WritePin(Led_GPIO_Port, Led_Pin, motion.level ? GPIO_PIN_SET : GPIO_PIN_RESET);
        }
        if (adc_ready && due(now, adc_at)) {
            adc_at = now + 50U;
            if (adc_read(&raw)) {
                median = stage_light_sample(&light, raw);
            } else {
                ++adc_errors;
            }
        }
        if (due(now, i2c_at)) {
            bool previous = imu.ready;
            if (stage_imu_read(&imu, fault ? MPU_ADDRESS + 1U : MPU_ADDRESS)) {
                i2c_at = now + 20U;
                if (!previous) {
                    printf("IMU ready t_ms=%lu who=0x%02x\r\n", (unsigned long)now, imu.who);
                }
            } else {
                ++imu.errors;
                i2c_at = now + 500U;
                if (previous || imu.errors == 1U) {
                    printf("IMU unavailable t_ms=%lu bus=%lu\r\n",
                           (unsigned long)now, (unsigned long)stage_bus_error());
                }
            }
        }
        if (due(now, print_at)) {
            print_at = now + 1000U;
            printf("t_ms=%lu pir=%u adc=%u median=%u dim=%u adc_err=%lu imu=%s "
                   "a_mg=%ld,%ld,%ld g_mdps=%ld,%ld,%ld i2c_err=%lu\r\n",
                   (unsigned long)now, motion.level, raw, median, light.dim,
                   (unsigned long)adc_errors, imu.ready ? "ok" : "stale",
                   (long)imu.accel[0], (long)imu.accel[1], (long)imu.accel[2],
                   (long)imu.gyro[0], (long)imu.gyro[1], (long)imu.gyro[2],
                   (unsigned long)imu.errors);
        }
    }
}
