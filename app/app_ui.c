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
 * @file  app_ui.c
 * @brief UI output — LED2 blink rate + temperature display
 *
 * Two EBus subscriptions:
 *   APP_SIG_TEMP_UPDATE  → log temperature to UART (2nd subscriber,
 *                          same Pool event also consumed by Controller FSM)
 *   APP_SIG_ALARM_STATE  → change LED2 blink rate
 *
 * This demonstrates the multi-subscriber Pool dispatch path:
 *   Pool_Dispatch → EBus_Publish → callback A (Controller) + callback B (UI)
 *   → all return → Pool_Free
 */

#include "anbo_config.h"

#if ANBO_CONF_POOL

#include "app_ui.h"
#include "app_signals.h"
#include "anbo_pool.h"
#include "anbo_ebus.h"
#include "anbo_timer.h"
#include "anbo_log.h"
#include "b_l4s5i_hw.h"     /* BSP_LED2_Toggle */

/* ================================================================== */
/*  Shared derived event type (must match app_sensor.c)                 */
/* ================================================================== */

typedef struct {
    Anbo_PoolEvent super;
    int32_t        temp_x10;
} TempEvent;

/* ================================================================== */
/*  LED blink timer                                                    */
/* ================================================================== */

#define UI_BLINK_SLOW_MS    1000u
#define UI_BLINK_FAST_MS     100u

static Anbo_Timer s_led_timer;

static void led_blink_cb(Anbo_Timer *tmr)
{
    (void)tmr;
    BSP_LED2_Toggle();
}

/* ================================================================== */
/*  Temperature display subscriber (2nd consumer of TEMP_UPDATE)       */
/* ================================================================== */

static Anbo_Subscriber s_temp_sub;

static void temp_display_handler(const Anbo_Event *evt, void *ctx)
{
    (void)ctx;
    const TempEvent *te = (const TempEvent *)evt->param;
    if (te == NULL) {
        return;
    }
    ANBO_LOGI("UI: temp = %d.%d C",
              (int)(te->temp_x10 / 10), (int)(te->temp_x10 % 10));
}

/* ================================================================== */
/*  Alarm state subscriber                                             */
/* ================================================================== */

static Anbo_Subscriber s_alarm_sub;

static void alarm_handler(const Anbo_Event *evt, void *ctx)
{
    (void)ctx;
    uint32_t active = (uint32_t)(uintptr_t)evt->param;

    uint32_t period = active ? UI_BLINK_FAST_MS : UI_BLINK_SLOW_MS;

    Anbo_Timer_Stop(&s_led_timer);
    Anbo_Timer_Create(&s_led_timer, ANBO_TIMER_PERIODIC, period,
                      led_blink_cb, NULL);
    Anbo_Timer_Start(&s_led_timer);

    ANBO_LOGI("UI: alarm=%u -> blink %u ms", active, period);
}

/* ================================================================== */
/*  Public init                                                        */
/* ================================================================== */

void App_UI_Init(void)
{
    /* Start with slow blink (normal state) */
    Anbo_Timer_Create(&s_led_timer, ANBO_TIMER_PERIODIC, UI_BLINK_SLOW_MS,
                      led_blink_cb, NULL);
    Anbo_Timer_Start(&s_led_timer);

    /* Subscribe to alarm state changes */
    Anbo_EBus_Subscribe(&s_alarm_sub, APP_SIG_ALARM_STATE,
                        alarm_handler, NULL);

    /* Subscribe to temperature updates (2nd subscriber alongside Controller) */
    Anbo_EBus_Subscribe(&s_temp_sub, APP_SIG_TEMP_UPDATE,
                        temp_display_handler, NULL);

    ANBO_LOGI("UI: LED2 blink ready, temp display on UART");
}

void App_UI_Stop(void)
{
    Anbo_Timer_Stop(&s_led_timer);
}

void App_UI_Resume(void)
{
    Anbo_Timer_Create(&s_led_timer, ANBO_TIMER_PERIODIC, UI_BLINK_SLOW_MS,
                      led_blink_cb, NULL);
    Anbo_Timer_Start(&s_led_timer);
}

#endif /* ANBO_CONF_POOL */
