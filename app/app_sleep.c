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
 * @file  app_sleep.c
 * @brief System deep-sleep — long-press 3 s → Stop 2 loop with IWDG feeding
 *
 * Architecture:
 *   1. Button press starts a 3 s one-shot timer.
 *   2. When the timer fires, if button is STILL held → confirmed long-press.
 *   3. App_Sleep_Poll() calls enter_deep_sleep() which orchestrates:
 *        sleep_prepare()       – drain UART TX, stop modules, arm RTC
 *        sleep_enter_stop2()   – single Stop 2 leg (LPTIM → WFI → clocks)
 *        sleep_check_wake()    – priority-based wake reason (UART/RTC/Btn)
 *        sleep_maintenance()   – LPTIM timeout: log flush + temp check
 *        sleep_resume()        – disarm RTC, resume WDT & modules
 */

#include "anbo_config.h"
#include "app_config.h"
#include "app_sleep.h"
#include "app_signals.h"
#include "anbo_ebus.h"
#include "anbo_timer.h"
#include "anbo_arch.h"
#include "anbo_log.h"
#include "b_l4s5i_hw.h"
#include "b_l4s5i_uart_drv.h"
#include "app_sensor.h"
#include "app_imu.h"
#include "b_l4s5i_imu_drv.h"

#if ANBO_CONF_WDT
#include "anbo_wdt.h"
#endif

#include "stm32l4xx_hal.h"

/* ================================================================== */
/*  Forward declarations for app module stop/resume                    */
/* ================================================================== */

extern void App_Sensor_Stop(void);
extern void App_Sensor_Resume(void);
extern void App_UI_Stop(void);
extern void App_UI_Resume(void);
extern void App_Controller_Stop(void);
extern void App_Controller_Resume(void);
extern void App_IMU_Resume(void);

/* ================================================================== */
/*  Module state                                                       */
/* ================================================================== */

/** Long-press detection timer (3 s one-shot). */
static Anbo_Timer s_lp_timer;

/** Set to 1 when long-press is confirmed; consumed by Poll(). */
static volatile uint8_t s_sleep_request;

/** Auto-wake timeout in seconds (0 = button-only wake). */
static uint32_t s_wake_timeout_s;

/** Main-loop SW WDT slot handle. */
static int s_wdt_slot = -1;

/** Button subscriber for long-press detection. */
static Anbo_Subscriber s_btn_sleep_sub;

#if !APP_CONF_SLEEP_FREEZE_IWDG
/* IWDG feeding interval — must be shorter than IWDG timeout (2000 ms). */
#define IWDG_FEED_INTERVAL_MS   1500u
#endif

/* ---- Auto deep-sleep parameters ---- */

/** Consecutive stable seconds before auto deep-sleep triggers. */
#define AUTOSLEEP_STABLE_S      60u

/** Max allowed range in the stability window (0.1 °C). 10 = 1.0 °C. */
#define AUTOSLEEP_RANGE_X10     10

/** Temperature change during deep sleep that triggers a full wake (0.1 °C). */
#define SLEEP_TEMP_WAKE_X10     10

static int32_t  s_autosleep_min;
static int32_t  s_autosleep_max;
static uint32_t s_autosleep_start;
static uint8_t  s_autosleep_armed;

/* ================================================================== */
/*  Long-press detection                                               */
/* ================================================================== */

/**
 * 3-second timer callback.
 * If button is STILL held (PC13 LOW), set the sleep-request flag.
 */
static void longpress_timer_cb(Anbo_Timer *tmr)
{
    (void)tmr;

    if (BSP_BTN_IsPressed()) {
        s_sleep_request = 1u;
        ANBO_LOGI("Sleep: long-press confirmed");
    }
}

/**
 * Button-press EBus handler — start/restart the 3 s detection timer.
 */
static void btn_sleep_handler(const Anbo_Event *evt, void *ctx)
{
    (void)evt;
    (void)ctx;

    /* (Re)start 3 s one-shot timer */
    Anbo_Timer_Stop(&s_lp_timer);
    Anbo_Timer_Create(&s_lp_timer, ANBO_TIMER_ONESHOT, 3000u,
                      longpress_timer_cb, NULL);
    Anbo_Timer_Start(&s_lp_timer);
}

/* ================================================================== */
/*  Deep-sleep helpers                                                 */
/* ================================================================== */

/* Externs used by deep-sleep helpers */
extern void              SystemClock_Recovery(void);
extern volatile uint32_t s_tick_ms;
extern volatile uint8_t  g_rtc_fired;

/** Wake-up reason returned by sleep_check_wake(). */
typedef enum {
    WAKE_NONE = 0,
    WAKE_UART,
    WAKE_RTC,
    WAKE_BUTTON,
    WAKE_IMU,
    WAKE_TEMP,
} WakeReason;

/** UART RXNE flag — sampled in sleep_enter_stop2(), consumed by sleep_check_wake(). */
static uint32_t s_uart_woke;

/** IMU INT1 (PD11) EXTI pending — sampled after Stop 2 wakeup. */
static uint32_t s_imu_woke;

/* -------------------------------------------------------------- */

/**
 * @brief Drain UART TX, stop app modules, suspend WDT, LED off, arm RTC.
 */
static void sleep_prepare(int32_t entry_temp)
{
    ANBO_LOGI("Sleep: entering deep sleep (RTC wake=%u s, temp=%d.%d C)",
              s_wake_timeout_s,
              (int)(entry_temp / 10),
              (int)(entry_temp % 10));

    /* Arm IMU wake-up BEFORE draining UART so the config log is visible,
     * and the accelerometer has time to settle while we drain. */
    App_IMU_SleepArm();

    /* Drain all pending log data before shutting down.
     * Loop Flush() until log_rb is empty → all data in UART TX ring buffer.
     * Then wait for USART1 hardware TC — every byte has left the pin. */
    {
        uint32_t t0 = Anbo_Arch_GetTick();
        do {
            Anbo_Log_Flush();
        } while ((Anbo_Arch_GetTick() - t0) < 50u);

        t0 = Anbo_Arch_GetTick();
        while (!(USART1->ISR & USART_ISR_TC) &&
               ((Anbo_Arch_GetTick() - t0) < 100u)) {
            /* spin */
        }
    }

    App_Sensor_Stop();
    App_UI_Stop();
    App_Controller_Stop();

#if ANBO_CONF_WDT
    if (s_wdt_slot >= 0) {
        Anbo_WDT_Suspend((Anbo_WDT_Slot)s_wdt_slot);
    }
#endif

    BSP_LED2_Set(0);   /* LED off = visual "asleep" indicator */

    if (s_wake_timeout_s > 0u) {
        BSP_RTC_SetWakeup(s_wake_timeout_s);
    }

#if APP_CONF_SLEEP_FREEZE_IWDG && ANBO_CONF_WDT
    /* IWDG frozen in Stop via Option Byte — one last feed before the loop */
    Anbo_Arch_WDT_Feed();
#endif
}

/**
 * @brief Execute one Stop 2 leg: arm LPTIM → WFI → restore clocks.
 * @return Elapsed milliseconds spent in Stop 2.
 *
 * Side-effect: stores USART1 RXNE flag into s_uart_woke (sampled before
 * __enable_irq so that ISR / maintenance TX cannot obscure it).
 */
static uint32_t sleep_enter_stop2(void)
{
#if !APP_CONF_SLEEP_FREEZE_IWDG && ANBO_CONF_WDT
    Anbo_Arch_WDT_Feed();
#endif

#if APP_CONF_SLEEP_FREEZE_IWDG
    uint32_t sleep_ms = 65000u;           /* IWDG frozen — LPTIM1 max (~65 s) */
#else
    uint32_t sleep_ms = IWDG_FEED_INTERVAL_MS; /* wake every 1.5 s to feed */
#endif

    BSP_LPTIM_StartOnce(sleep_ms);

    SysTick->CTRL &= ~SysTick_CTRL_TICKINT_Msk;
    HAL_SuspendTick();

    __HAL_PWR_CLEAR_FLAG(PWR_FLAG_WU);
    __HAL_GPIO_EXTI_CLEAR_IT(BSP_BTN_PIN);
    __HAL_GPIO_EXTI_CLEAR_IT(GPIO_PIN_11);   /* IMU INT1 (PD11) */

    __disable_irq();

    /* ---- Stop 2 ---- */
    HAL_PWREx_EnterSTOP2Mode(PWR_STOPENTRY_WFI);

    /* ---- Wakeup ---- */
    uint32_t elapsed = BSP_LPTIM_StopAndRead();

    SystemClock_Recovery();

    s_tick_ms += elapsed;
    NVIC_ClearPendingIRQ(LPTIM1_IRQn);
    NVIC_ClearPendingIRQ(RTC_WKUP_IRQn);

    s_uart_woke = (USART1->ISR & USART_ISR_RXNE_RXFNE);
    s_imu_woke  = __HAL_GPIO_EXTI_GET_IT(GPIO_PIN_11)
               || HAL_GPIO_ReadPin(GPIOD, GPIO_PIN_11);
    if (s_imu_woke) {
        __HAL_GPIO_EXTI_CLEAR_IT(GPIO_PIN_11);
    }

    __enable_irq();   /* may jump to RTC_WKUP_IRQHandler if RTC fired */

    HAL_ResumeTick();
    SysTick->CTRL |= SysTick_CTRL_TICKINT_Msk;

    return elapsed;
}

/**
 * @brief Check wake source in priority order.
 * @return WAKE_UART / WAKE_RTC / WAKE_BUTTON, or WAKE_NONE (LPTIM timeout).
 */
static WakeReason sleep_check_wake(uint32_t total_ms)
{
    /* Priority 1: UART data (sampled before __enable_irq) */
    if (s_uart_woke) {
        ANBO_LOGI("Sleep: woken by UART RX (%u ms asleep)", total_ms);
        return WAKE_UART;
    }

    /* Priority 2: RTC wakeup → unconditional full wake */
    if (s_wake_timeout_s > 0u && g_rtc_fired) {
        g_rtc_fired = 0;
        int32_t t = App_Sensor_QuickRead();
        ANBO_LOGI("Sleep: RTC wake, temp %d.%d C, full wake (%u ms asleep)",
                  (int)(t / 10), (int)(t % 10), total_ms);
        return WAKE_RTC;
    }

    /* Priority 3: User button → unconditional full wake */
    if (BSP_BTN_IsPressed()) {
        g_rtc_fired = 0;
        ANBO_LOGI("Sleep: woken by button (%u ms asleep)", total_ms);
        return WAKE_BUTTON;
    }

    /* Priority 4: IMU vibration → unconditional full wake */
    if (s_imu_woke) {
        uint8_t src;
        BSP_IMU_ReadWakeUpSrc(&src);  /* clear latch so INT1 goes LOW */
        ANBO_LOGI("Sleep: woken by IMU vibration (%u ms asleep)", total_ms);
        return WAKE_IMU;
    }

    return WAKE_NONE;
}

/**
 * @brief LPTIM maintenance leg: optional log flush + temperature check.
 * @return 1 if temperature changed significantly (caller should wake),
 *         0 if stable (caller should re-sleep).
 */
static int sleep_maintenance(int32_t entry_temp, uint32_t total_ms)
{
    (void)total_ms;

#if APP_CONF_SLEEP_MAINTENANCE_LOG
    ANBO_LOGI("Sleep: LPTIM maintenance wake (feed WDT)");

    Anbo_Log_Flush();
    {
        uint32_t t0 = HAL_GetTick();
        while (!(USART1->ISR & USART_ISR_TC) && (HAL_GetTick() - t0 < 50u)) {
            /* Spin until hardware TX complete or 50 ms timeout */
        }
    }
#endif

    int32_t t = App_Sensor_QuickRead();
    int32_t delta = t - entry_temp;
    if (delta < 0) { delta = -delta; }

    if (delta >= SLEEP_TEMP_WAKE_X10) {
        ANBO_LOGW("Sleep: LPTIM wake, temp %d.%d->%d.%d C (delta %d.%d), waking",
                  (int)(entry_temp / 10), (int)(entry_temp % 10),
                  (int)(t / 10), (int)(t % 10),
                  (int)(delta / 10), (int)(delta % 10));
        return 1;
    }

    return 0;
}

/**
 * @brief Resume system after deep sleep: disarm RTC, resume WDT & app modules.
 */
static void sleep_resume(void)
{
#if APP_CONF_SLEEP_FREEZE_IWDG && ANBO_CONF_WDT
    /* IWDG resumes automatically when exiting Stop — feed immediately */
    Anbo_Arch_WDT_Feed();
#endif

    if (s_wake_timeout_s > 0u) {
        BSP_RTC_StopWakeup();
    }

#if ANBO_CONF_WDT
    if (s_wdt_slot >= 0) {
        Anbo_WDT_Resume((Anbo_WDT_Slot)s_wdt_slot);
    }
#endif

    App_Sensor_Resume();
    App_UI_Resume();
    App_Controller_Resume();
    App_IMU_Resume();

    ANBO_LOGI("Sleep: system active");
}

/* ================================================================== */
/*  Deep-sleep loop (blocking)                                         */
/* ================================================================== */

/**
 * @brief Enter system deep-sleep.  Blocks until woken by button, RTC,
 *        UART RX, or significant temperature change.
 */
static void enter_deep_sleep(void)
{
    int32_t  entry_temp = App_Sensor_QuickRead();
    uint32_t total_ms   = 0u;

    sleep_prepare(entry_temp);

    for (;;) {
        total_ms += sleep_enter_stop2();

        WakeReason reason = sleep_check_wake(total_ms);
        if (reason != WAKE_NONE) {
            break;
        }

        if (sleep_maintenance(entry_temp, total_ms)) {
            break;
        }
    }

    sleep_resume();
}

/* ================================================================== */
/*  Public API                                                         */
/* ================================================================== */

#if ANBO_CONF_WDT
/**
 * @brief Ensure the IWDG_STOP Option Byte matches the desired setting.
 *
 * STM32L4 Stop 2 powers down the DBGMCU domain, so the DBGMCU freeze
 * registers have no effect.  The only reliable way to control IWDG
 * behaviour in Stop mode is the Flash Option Byte IWDG_STOP
 * (FLASH_OPTR bit 17).
 *
 *   bit = 0 (OB_IWDG_STOP_FREEZE) → IWDG frozen in Stop
 *   bit = 1 (OB_IWDG_STOP_RUN)    → IWDG running in Stop (factory default)
 *
 * When FREEZE_IWDG=1: programmes freeze so IWDG pauses during Stop.
 * When FREEZE_IWDG=0: restores run so IWDG still guards against
 *   wake-up failures (matching the 1.5 s feed strategy).
 *
 * If the current OB doesn't match @p desired, this function programmes
 * it and triggers an Option-Byte-Load reset (OBL_LAUNCH).  This is a
 * one-time operation — the setting persists across resets and power cycles.
 */
static void ensure_iwdg_stop_ob(uint32_t desired)
{
    /* desired: OB_IWDG_STOP_FREEZE (bit=0) or OB_IWDG_STOP_RUN (bit=1) */
    const uint32_t current = FLASH->OPTR & FLASH_OPTR_IWDG_STOP;
    if (current == desired) {
        return;   /* Already matches — nothing to do */
    }

    ANBO_LOGW("Sleep: OB IWDG_STOP=0x%lx, need 0x%lx — programming (will reset)...",
              (unsigned long)current, (unsigned long)desired);
    Anbo_Log_Flush();
    /* Wait for UART TX to finish before the reset */
    {
        uint32_t t0 = HAL_GetTick();
        while (!(USART1->ISR & USART_ISR_TC) &&
               (HAL_GetTick() - t0 < 100u)) { /* spin */ }
    }

    HAL_FLASH_Unlock();
    HAL_FLASH_OB_Unlock();

    FLASH_OBProgramInitTypeDef ob = {0};
    ob.OptionType = OPTIONBYTE_USER;
    ob.USERType   = OB_USER_IWDG_STOP;
    ob.USERConfig = desired;

    HAL_FLASHEx_OBProgram(&ob);
    HAL_FLASH_OB_Launch();   /* triggers system reset — never returns */
}
#endif

void App_Sleep_Init(int wdt_slot)
{
    s_wdt_slot      = wdt_slot;
    s_sleep_request = 0u;
    s_wake_timeout_s = 0u;
    s_autosleep_armed = 0u;

#if ANBO_CONF_WDT
#if APP_CONF_SLEEP_FREEZE_IWDG
    ensure_iwdg_stop_ob(OB_IWDG_STOP_FREEZE);  /* freeze IWDG in Stop */
#else
    ensure_iwdg_stop_ob(OB_IWDG_STOP_RUN);     /* restore: IWDG runs in Stop */
#endif
#endif

    /* Subscribe to button for long-press detection.
     * This runs ALONGSIDE the existing task_a_button_cb in main.c
     * (EBus supports multiple subscribers per signal). */
    Anbo_EBus_Subscribe(&s_btn_sleep_sub, ANBO_SIG_USER_BUTTON,
                        btn_sleep_handler, NULL);

    ANBO_LOGI("Sleep: long-press 3 s to enter deep sleep");
}

void App_Sleep_SetTimeout(uint32_t seconds)
{
    s_wake_timeout_s = seconds;
}

void App_Sleep_Poll(void)
{
    /* ---- Manual deep-sleep (long-press confirmed) ---- */
    if (s_sleep_request) {
        s_sleep_request = 0u;
        s_autosleep_armed = 0u;
        enter_deep_sleep();
        s_autosleep_armed = 0u;   /* need fresh 60 s after wake */
        return;
    }

    /* ---- Auto deep-sleep: temperature stable for 60 s ---- */
    {
        uint32_t now = Anbo_Arch_GetTick();
        int32_t  t   = App_Sensor_GetLastTemp();

        /* Piggyback on the sensor's last reading instead of doing
         * an extra ADC conversion.  s_last_stable_temp is updated
         * every super-loop iteration from the most recent TempEvent. */

        if (!s_autosleep_armed) {
            s_autosleep_min   = t;
            s_autosleep_max   = t;
            s_autosleep_start = now;
            s_autosleep_armed = 1u;
        } else {
            if (t < s_autosleep_min) { s_autosleep_min = t; }
            if (t > s_autosleep_max) { s_autosleep_max = t; }

            /* Range exceeded? restart window */
            if ((s_autosleep_max - s_autosleep_min) > AUTOSLEEP_RANGE_X10) {
                s_autosleep_min   = t;
                s_autosleep_max   = t;
                s_autosleep_start = now;
            }
            /* Stable long enough? */
            else if ((now - s_autosleep_start) >= (AUTOSLEEP_STABLE_S * 1000u)) {
                ANBO_LOGI("Sleep: temp stable %d.%d C for %u s, auto deep-sleep",
                          (int)(t / 10), (int)(t % 10),
                          AUTOSLEEP_STABLE_S);
                s_autosleep_armed = 0u;
                enter_deep_sleep();
                s_autosleep_armed = 0u;
            }
        }
    }
}
