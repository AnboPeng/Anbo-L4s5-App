/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Anbo Peng
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
/**
 * @file  app_sensor.c
 * @brief Temperature sensor — ADC1 ch17 (internal) → Pool async event
 *
 * Demonstrates the async Pool path:
 *   Timer 1 s → ADC read → Pool_Alloc(TempEvent) → EvtQ_Post
 *   → main-loop Pool_Dispatch → EBus broadcast APP_SIG_TEMP_UPDATE
 *
 * The sensor module does NOT know who consumes the temperature — full
 * decoupling via the event bus.
 */

#include "anbo_config.h"

#if ANBO_CONF_POOL

#include "app_sensor.h"
#include "app_signals.h"
#include "anbo_timer.h"
#include "anbo_pool.h"
#include "anbo_log.h"

#if ANBO_CONF_WDT
#include "anbo_wdt.h"
#endif

#include "app_fault_mgr.h"
#include "anbo_arch.h"

#include "stm32l4xx_hal.h"

/* ================================================================== */
/*  Derived Pool Event: temperature reading                            */
/* ================================================================== */

typedef struct {
    Anbo_PoolEvent super;       /* base class — must be first */
    int32_t        temp_x10;    /* temperature in 0.1 °C units  */
} TempEvent;

/* ================================================================== */
/*  ADC handle (static, local to this module)                          */
/* ================================================================== */

static ADC_HandleTypeDef s_hadc1;

static void adc_init(void)
{
    ADC_ChannelConfTypeDef ch = {0};

    __HAL_RCC_ADC_CLK_ENABLE();

    s_hadc1.Instance                   = ADC1;
    s_hadc1.Init.ClockPrescaler        = ADC_CLOCK_SYNC_PCLK_DIV4; /* 120/4=30 MHz, no async source needed */
    s_hadc1.Init.Resolution            = ADC_RESOLUTION_12B;
    s_hadc1.Init.DataAlign             = ADC_DATAALIGN_RIGHT;
    s_hadc1.Init.ScanConvMode          = ADC_SCAN_DISABLE;
    s_hadc1.Init.EOCSelection          = ADC_EOC_SINGLE_CONV;
    s_hadc1.Init.LowPowerAutoWait      = DISABLE;
    s_hadc1.Init.ContinuousConvMode    = DISABLE;
    s_hadc1.Init.NbrOfConversion       = 1;
    s_hadc1.Init.DiscontinuousConvMode = DISABLE;
    s_hadc1.Init.ExternalTrigConv      = ADC_SOFTWARE_START;
    s_hadc1.Init.ExternalTrigConvEdge  = ADC_EXTERNALTRIGCONVEDGE_NONE;
    s_hadc1.Init.DMAContinuousRequests = DISABLE;
    s_hadc1.Init.Overrun               = ADC_OVR_DATA_OVERWRITTEN;
    s_hadc1.Init.OversamplingMode      = DISABLE;
    HAL_ADC_Init(&s_hadc1);

    /*
     * Calibration MUST be done BEFORE channel configuration (RM0432 §18.4.8).
     * HAL_ADCEx_Calibration_Start() disables the ADC internally;
     * channel/TSEN config done afterward ensures registers are not clobbered.
     */
    HAL_ADCEx_Calibration_Start(&s_hadc1, ADC_SINGLE_ENDED);

    /* Internal temperature sensor = ADC1 channel 17 */
    ch.Channel      = ADC_CHANNEL_TEMPSENSOR;
    ch.Rank         = ADC_REGULAR_RANK_1;
    ch.SamplingTime = ADC_SAMPLETIME_247CYCLES_5;  /* long sample for accuracy */
    ch.SingleDiff   = ADC_SINGLE_ENDED;
    ch.OffsetNumber = ADC_OFFSET_NONE;
    ch.Offset       = 0;
    HAL_ADC_ConfigChannel(&s_hadc1, &ch);

    ANBO_LOGI("ADC: CAL1=%u CAL2=%u (at %d/%d C)",
              (unsigned)*TEMPSENSOR_CAL1_ADDR,
              (unsigned)*TEMPSENSOR_CAL2_ADDR,
              (int)TEMPSENSOR_CAL1_TEMP,
              (int)TEMPSENSOR_CAL2_TEMP);
}

/**
 * @brief Read internal temperature sensor via ADC (blocking, ~50 µs).
 * @return Temperature in 0.1 °C   (e.g. 312 = 31.2 °C).
 */
static int32_t adc_read_temp(void)
{
    uint32_t raw;
    int32_t  temp;

    HAL_ADC_Start(&s_hadc1);
    HAL_ADC_PollForConversion(&s_hadc1, 10u);
    raw = HAL_ADC_GetValue(&s_hadc1);
    HAL_ADC_Stop(&s_hadc1);

    /*
     * STM32L4 internal temp formula (RM0432 §18.4.32):
     *   T(°C) = (TS_CAL2_TEMP - TS_CAL1_TEMP)
     *         / (TS_CAL2 - TS_CAL1)
     *         * (raw - TS_CAL1)
     *         + TS_CAL1_TEMP
     *
     * Factory calibration values at 3.0 V, stored in System Flash.
     * Address: 0x1FFF_xxxx (NOT 0x0FFF — that region is unmapped).
     * Use official CMSIS/HAL defines from stm32l4xx_ll_adc.h.
     */
    #define TS_CAL1       (*TEMPSENSOR_CAL1_ADDR)                /* @30 °C  */
    #define TS_CAL2       (*TEMPSENSOR_CAL2_ADDR)                /* @130 °C */
    #define TS_CAL1_TEMP  TEMPSENSOR_CAL1_TEMP
    #define TS_CAL2_TEMP  TEMPSENSOR_CAL2_TEMP
    #define TS_CAL_VREF   TEMPSENSOR_CAL_VREFANALOG              /* 3000 mV */

    /*
     * VDDA compensation (RM0432 §18.4.32):
     *   Calibration was done at VDDA = 3.0 V.  If actual VDDA differs,
     *   the raw ADC value must be scaled to the calibration reference:
     *     raw_compensated = raw * VDDA_mV / TS_CAL_VREF
     *
     * B-L4S5I-IOT01A: VDDA = 3.3 V (from VDD via ferrite bead).
     * For higher accuracy, VDDA could be measured via VREFINT channel.
     */
    #define BSP_VDDA_MV  3300u
    uint32_t raw_comp = raw * BSP_VDDA_MV / TS_CAL_VREF;

    /* result in 0.1 °C to avoid float */
    temp = (int32_t)(TS_CAL2_TEMP - TS_CAL1_TEMP) * 10
         * (int32_t)(raw_comp - TS_CAL1)
         / (int32_t)(TS_CAL2 - TS_CAL1)
         + TS_CAL1_TEMP * 10;

    return temp;
}

/* ================================================================== */
/*  Physical range limits for sanity check                             */
/* ================================================================== */

#define SENSOR_TEMP_MIN_X10   (-400)   /* -40.0 °C */
#define SENSOR_TEMP_MAX_X10   (1500)   /* 150.0 °C */

/** Last good reading (used by recovery callback) */
static int32_t s_last_temp_x10;

int32_t App_Sensor_QuickRead(void)
{
    return adc_read_temp();
}

int32_t App_Sensor_GetLastTemp(void)
{
    return s_last_temp_x10;
}

/**
 * Recovery probe: called by FaultManager's periodic scan.
 * Returns true if the most recent ADC reading is back in range.
 */
static bool sensor_range_ok(FaultId id, void *ctx)
{
    (void)id; (void)ctx;
    int32_t t = adc_read_temp();
    return (t >= SENSOR_TEMP_MIN_X10 && t <= SENSOR_TEMP_MAX_X10);
}

/* ================================================================== */
/*  1-second periodic timer → Pool async publish                       */
/* ================================================================== */

static Anbo_Timer s_sensor_timer;

#if ANBO_CONF_WDT
static Anbo_WDT_Slot s_sensor_wdt = ANBO_WDT_SLOT_INVALID;
#endif

static void sensor_timer_cb(Anbo_Timer *tmr)
{
    (void)tmr;

    int32_t t = adc_read_temp();

#if ANBO_CONF_WDT
    if (s_sensor_wdt >= 0) {
        Anbo_WDT_Checkin(s_sensor_wdt);
    }
#endif

    /* ---- Range check: report fault if reading is physically impossible ---- */
    if (t < SENSOR_TEMP_MIN_X10 || t > SENSOR_TEMP_MAX_X10) {
        ANBO_LOGW("Sensor: out of range %d.%d C",
                  (int)(t / 10), (int)(t % 10));
        Fault_Report(FAULT_SENSOR_RANGE, Anbo_Arch_GetTick());
        return;     /* do NOT publish bad data */
    }

    s_last_temp_x10 = t;

    /* Allocate a TempEvent from the static pool */
    TempEvent *evt = (TempEvent *)Anbo_Pool_Alloc();
    if (evt == NULL) {
        ANBO_LOGD("Sensor: pool exhausted");
        return;
    }

    /* Fill the event */
    evt->super.sig = APP_SIG_TEMP_UPDATE;
    evt->temp_x10  = t;

    /* Post to the async queue — main loop will dispatch */
    if (Anbo_EvtQ_Post(&evt->super) != 0) {
        Anbo_Pool_Free(evt);    /* queue full, reclaim block */
        ANBO_LOGD("Sensor: evtq full");
    }
}

/* ================================================================== */
/*  Public init                                                        */
/* ================================================================== */

void App_Sensor_Init(void)
{
    adc_init();

    /* Register sensor-range fault: WARNING severity, 3 retries, recovery probe */
    Fault_Register(FAULT_SENSOR_RANGE, "SensorRange",
                   FAULT_SEV_WARNING, 3, 1000u,
                   sensor_range_ok, NULL);

#if ANBO_CONF_WDT
    s_sensor_wdt = Anbo_WDT_Register("sensor", 2000u);
#endif

    Anbo_Timer_Create(&s_sensor_timer, ANBO_TIMER_PERIODIC, 1000u,
                      sensor_timer_cb, NULL);
    Anbo_Timer_Start(&s_sensor_timer);

    ANBO_LOGI("Sensor: ADC1-ch17 (int temp), 1 s period, Pool async");
}

void App_Sensor_Stop(void)
{
    Anbo_Timer_Stop(&s_sensor_timer);
#if ANBO_CONF_WDT
    if (s_sensor_wdt >= 0) {
        Anbo_WDT_Suspend(s_sensor_wdt);
    }
#endif
}

void App_Sensor_Resume(void)
{
    Anbo_Timer_Start(&s_sensor_timer);
#if ANBO_CONF_WDT
    if (s_sensor_wdt >= 0) {
        Anbo_WDT_Resume(s_sensor_wdt);
    }
#endif
}

#endif /* ANBO_CONF_POOL */
