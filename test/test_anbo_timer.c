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
 * @file  test_anbo_timer.c
 * @brief Unit tests for Anbo_Timer — software timer with mock tick.
 *
 * Uses anbo_arch_host stub to control system tick and verify:
 *   - One-shot timer fires exactly once
 *   - Periodic timer fires repeatedly
 *   - Timer stop/restart
 *   - Multiple timers ordered by deadline
 *   - Wrap-around handling (uint32 overflow)
 *   - MsToNext calculation
 *   - Critical section pairing
 */

#include "unity.h"
#include "anbo_timer.h"
#include "anbo_arch.h"
#include "anbo_arch_host.h"

/* ---- Callback tracking ---- */
#define MAX_CB_LOG  32

static struct {
    Anbo_Timer *timer;
    uint32_t    tick;
} s_cb_log[MAX_CB_LOG];

static uint32_t s_cb_count;

static void cb_record(Anbo_Timer *tmr)
{
    if (s_cb_count < MAX_CB_LOG) {
        s_cb_log[s_cb_count].timer = tmr;
        s_cb_log[s_cb_count].tick  = Anbo_Arch_GetTick();
        s_cb_count++;
    }
}

/* ---- Setup / Teardown ---- */

void setUp(void)
{
    s_cb_count = 0;
    Anbo_Arch_Host_SetTick(0);
    Anbo_Arch_Host_ResetCritical();
    Anbo_Timer_Init();
}

void tearDown(void)
{
    /* Verify critical sections are always balanced */
    TEST_ASSERT_EQUAL_INT32(0, Anbo_Arch_Host_GetCriticalDepth());
}

/* ================================================================ */
/*  One-shot timer                                                   */
/* ================================================================ */

void test_oneshot_fires_once(void)
{
    Anbo_Timer tmr;
    Anbo_Timer_Create(&tmr, ANBO_TIMER_ONESHOT, 100, cb_record, NULL);
    Anbo_Timer_Start(&tmr);
    TEST_ASSERT_TRUE(Anbo_Timer_IsRunning(&tmr));

    /* Before deadline — should not fire */
    Anbo_Arch_Host_SetTick(99);
    Anbo_Timer_Update(99);
    TEST_ASSERT_EQUAL_UINT32(0, s_cb_count);

    /* At deadline — fires */
    Anbo_Arch_Host_SetTick(100);
    Anbo_Timer_Update(100);
    TEST_ASSERT_EQUAL_UINT32(1, s_cb_count);
    TEST_ASSERT_EQUAL_PTR(&tmr, s_cb_log[0].timer);
    TEST_ASSERT_FALSE(Anbo_Timer_IsRunning(&tmr));

    /* After — should not fire again */
    Anbo_Arch_Host_SetTick(200);
    Anbo_Timer_Update(200);
    TEST_ASSERT_EQUAL_UINT32(1, s_cb_count);
}

void test_oneshot_fires_if_past_deadline(void)
{
    Anbo_Timer tmr;
    Anbo_Timer_Create(&tmr, ANBO_TIMER_ONESHOT, 50, cb_record, NULL);
    Anbo_Timer_Start(&tmr);

    /* Jump well past the deadline */
    Anbo_Arch_Host_SetTick(200);
    Anbo_Timer_Update(200);
    TEST_ASSERT_EQUAL_UINT32(1, s_cb_count);
    TEST_ASSERT_FALSE(Anbo_Timer_IsRunning(&tmr));
}

/* ================================================================ */
/*  Periodic timer                                                   */
/* ================================================================ */

void test_periodic_fires_repeatedly(void)
{
    Anbo_Timer tmr;
    Anbo_Timer_Create(&tmr, ANBO_TIMER_PERIODIC, 100, cb_record, NULL);
    Anbo_Timer_Start(&tmr);

    /* Fire 1 */
    Anbo_Arch_Host_SetTick(100);
    Anbo_Timer_Update(100);
    TEST_ASSERT_EQUAL_UINT32(1, s_cb_count);
    TEST_ASSERT_TRUE(Anbo_Timer_IsRunning(&tmr));

    /* Fire 2 */
    Anbo_Arch_Host_SetTick(200);
    Anbo_Timer_Update(200);
    TEST_ASSERT_EQUAL_UINT32(2, s_cb_count);

    /* Fire 3 */
    Anbo_Arch_Host_SetTick(300);
    Anbo_Timer_Update(300);
    TEST_ASSERT_EQUAL_UINT32(3, s_cb_count);
}

void test_periodic_multiple_expirations_in_single_update(void)
{
    Anbo_Timer tmr;
    Anbo_Timer_Create(&tmr, ANBO_TIMER_PERIODIC, 10, cb_record, NULL);
    Anbo_Timer_Start(&tmr);

    /* Jump far ahead — should fire at least once per Update call
     * (periodic re-inserts at now + period, so each Update fires once) */
    Anbo_Arch_Host_SetTick(10);
    Anbo_Timer_Update(10);
    TEST_ASSERT_EQUAL_UINT32(1, s_cb_count);

    Anbo_Arch_Host_SetTick(20);
    Anbo_Timer_Update(20);
    TEST_ASSERT_EQUAL_UINT32(2, s_cb_count);
}

/* ================================================================ */
/*  Stop / Restart                                                   */
/* ================================================================ */

void test_stop_prevents_firing(void)
{
    Anbo_Timer tmr;
    Anbo_Timer_Create(&tmr, ANBO_TIMER_PERIODIC, 50, cb_record, NULL);
    Anbo_Timer_Start(&tmr);

    Anbo_Timer_Stop(&tmr);
    TEST_ASSERT_FALSE(Anbo_Timer_IsRunning(&tmr));

    Anbo_Arch_Host_SetTick(100);
    Anbo_Timer_Update(100);
    TEST_ASSERT_EQUAL_UINT32(0, s_cb_count);
}

void test_restart_resets_deadline(void)
{
    Anbo_Timer tmr;
    Anbo_Timer_Create(&tmr, ANBO_TIMER_ONESHOT, 100, cb_record, NULL);

    Anbo_Arch_Host_SetTick(0);
    Anbo_Timer_Start(&tmr);   /* deadline = 100 */

    Anbo_Arch_Host_SetTick(50);
    Anbo_Timer_Start(&tmr);   /* restart: deadline = 150 */

    Anbo_Arch_Host_SetTick(100);
    Anbo_Timer_Update(100);
    TEST_ASSERT_EQUAL_UINT32(0, s_cb_count);  /* old deadline missed */

    Anbo_Arch_Host_SetTick(150);
    Anbo_Timer_Update(150);
    TEST_ASSERT_EQUAL_UINT32(1, s_cb_count);  /* new deadline hit */
}

/* ================================================================ */
/*  SetPeriod                                                        */
/* ================================================================ */

void test_set_period_takes_effect_on_next_start(void)
{
    Anbo_Timer tmr;
    Anbo_Timer_Create(&tmr, ANBO_TIMER_ONESHOT, 100, cb_record, NULL);
    Anbo_Timer_SetPeriod(&tmr, 200);

    Anbo_Arch_Host_SetTick(0);
    Anbo_Timer_Start(&tmr);   /* deadline = 200 */

    Anbo_Arch_Host_SetTick(100);
    Anbo_Timer_Update(100);
    TEST_ASSERT_EQUAL_UINT32(0, s_cb_count);

    Anbo_Arch_Host_SetTick(200);
    Anbo_Timer_Update(200);
    TEST_ASSERT_EQUAL_UINT32(1, s_cb_count);
}

/* ================================================================ */
/*  Multiple timers — ordering                                       */
/* ================================================================ */

void test_multiple_timers_fire_in_order(void)
{
    Anbo_Timer t1, t2, t3;
    Anbo_Timer_Create(&t1, ANBO_TIMER_ONESHOT, 300, cb_record, NULL);
    Anbo_Timer_Create(&t2, ANBO_TIMER_ONESHOT, 100, cb_record, NULL);
    Anbo_Timer_Create(&t3, ANBO_TIMER_ONESHOT, 200, cb_record, NULL);

    /* Start in non-sorted order */
    Anbo_Timer_Start(&t1);
    Anbo_Timer_Start(&t2);
    Anbo_Timer_Start(&t3);

    Anbo_Arch_Host_SetTick(350);
    Anbo_Timer_Update(350);

    TEST_ASSERT_EQUAL_UINT32(3, s_cb_count);
    /* Verify ascending deadline order */
    TEST_ASSERT_EQUAL_PTR(&t2, s_cb_log[0].timer);  /* 100 */
    TEST_ASSERT_EQUAL_PTR(&t3, s_cb_log[1].timer);  /* 200 */
    TEST_ASSERT_EQUAL_PTR(&t1, s_cb_log[2].timer);  /* 300 */
}

/* ================================================================ */
/*  MsToNext                                                         */
/* ================================================================ */

void test_ms_to_next_no_timers(void)
{
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, Anbo_Timer_MsToNext(0));
}

void test_ms_to_next_reports_remaining(void)
{
    Anbo_Timer tmr;
    Anbo_Timer_Create(&tmr, ANBO_TIMER_ONESHOT, 500, cb_record, NULL);
    Anbo_Timer_Start(&tmr);  /* deadline = 500 */

    TEST_ASSERT_EQUAL_UINT32(500, Anbo_Timer_MsToNext(0));
    TEST_ASSERT_EQUAL_UINT32(250, Anbo_Timer_MsToNext(250));
    TEST_ASSERT_EQUAL_UINT32(0,   Anbo_Timer_MsToNext(500));
    TEST_ASSERT_EQUAL_UINT32(0,   Anbo_Timer_MsToNext(600));
}

/* ================================================================ */
/*  Wrap-around (uint32 overflow)                                    */
/* ================================================================ */

void test_timer_wraparound(void)
{
    Anbo_Timer tmr;
    Anbo_Timer_Create(&tmr, ANBO_TIMER_ONESHOT, 100, cb_record, NULL);

    /* Start near overflow boundary */
    Anbo_Arch_Host_SetTick(UINT32_MAX - 50);
    Anbo_Timer_Start(&tmr);  /* deadline wraps to ~49 */

    /* Before wrap — should not fire */
    Anbo_Arch_Host_SetTick(UINT32_MAX);
    Anbo_Timer_Update(UINT32_MAX);
    TEST_ASSERT_EQUAL_UINT32(0, s_cb_count);

    /* After wrap — should fire */
    uint32_t wrap_tick = (UINT32_MAX - 50) + 100;
    Anbo_Arch_Host_SetTick(wrap_tick);
    Anbo_Timer_Update(wrap_tick);
    TEST_ASSERT_EQUAL_UINT32(1, s_cb_count);
}

/* ================================================================ */
/*  user_data passthrough                                            */
/* ================================================================ */

static int s_user_value;

static void cb_userdata(Anbo_Timer *tmr)
{
    s_user_value = *(int *)tmr->user_data;
}

void test_user_data_accessible_in_callback(void)
{
    int payload = 42;
    s_user_value = 0;

    Anbo_Timer tmr;
    Anbo_Timer_Create(&tmr, ANBO_TIMER_ONESHOT, 10, cb_userdata, &payload);
    Anbo_Timer_Start(&tmr);

    Anbo_Arch_Host_SetTick(10);
    Anbo_Timer_Update(10);
    TEST_ASSERT_EQUAL_INT(42, s_user_value);
}

/* ================================================================ */
/*  NULL safety                                                      */
/* ================================================================ */

void test_null_timer_operations(void)
{
    /* Should not crash */
    Anbo_Timer_Create(NULL, ANBO_TIMER_ONESHOT, 100, cb_record, NULL);
    Anbo_Timer_Start(NULL);
    Anbo_Timer_Stop(NULL);
    Anbo_Timer_SetPeriod(NULL, 50);
    TEST_ASSERT_FALSE(Anbo_Timer_IsRunning(NULL));
}

void test_timer_without_callback_does_not_start(void)
{
    Anbo_Timer tmr;
    Anbo_Timer_Create(&tmr, ANBO_TIMER_ONESHOT, 100, NULL, NULL);
    Anbo_Timer_Start(&tmr);
    TEST_ASSERT_FALSE(Anbo_Timer_IsRunning(&tmr));
}

/* ================================================================ */
/*  Critical section balance                                         */
/* ================================================================ */

void test_critical_sections_balanced(void)
{
    Anbo_Arch_Host_ResetCritical();

    Anbo_Timer tmr;
    Anbo_Timer_Create(&tmr, ANBO_TIMER_PERIODIC, 10, cb_record, NULL);
    Anbo_Timer_Start(&tmr);

    Anbo_Arch_Host_SetTick(10);
    Anbo_Timer_Update(10);

    Anbo_Timer_Stop(&tmr);

    uint32_t enters = Anbo_Arch_Host_GetCriticalEnterCount();
    uint32_t exits  = Anbo_Arch_Host_GetCriticalExitCount();
    TEST_ASSERT_EQUAL_UINT32(enters, exits);
    TEST_ASSERT_TRUE(enters > 0);
}
