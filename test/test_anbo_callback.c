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
 * @file  test_anbo_callback.c
 * @brief Unit tests for callback patterns across Anbo kernel modules.
 *
 * Tests the callback plumbing between EBus, FSM, Timer, and Device,
 * verifying context propagation, ordering, and chaining scenarios.
 *
 * This covers:
 *   - EBus handler receives correct context
 *   - FSM on_entry / on_exit / on_event ordering
 *   - Timer -> EBus -> FSM callback chains
 *   - Device tx_done / rx_ready callback delivery
 */

#include "unity.h"
#include "anbo_ebus.h"
#include "anbo_fsm.h"
#include "anbo_dev.h"
#include "anbo_timer.h"
#include "anbo_arch.h"
#include "anbo_arch_host.h"

/* ---- Signal definitions ---- */
#define SIG_A       10
#define SIG_B       11
#define SIG_TMR     12

/* ---- Order-tracking log ---- */
#define MAX_LOG  64

static const char *s_order_log[MAX_LOG];
static uint32_t    s_order_count;

static void log_tag(const char *tag)
{
    if (s_order_count < MAX_LOG) {
        s_order_log[s_order_count++] = tag;
    }
}

/* ---- Setup / Teardown ---- */

void setUp(void)
{
    s_order_count = 0;
    Anbo_Arch_Host_SetTick(0);
    Anbo_Arch_Host_ResetCritical();
    Anbo_EBus_Init();
    Anbo_Timer_Init();
}

void tearDown(void)
{
    TEST_ASSERT_EQUAL_INT32(0, Anbo_Arch_Host_GetCriticalDepth());
}

/* ================================================================ */
/*  EBus handler context propagation                                 */
/* ================================================================ */

static void ctx_handler(const Anbo_Event *evt, void *ctx)
{
    (void)evt;
    log_tag((const char *)ctx);
}

void test_ebus_handler_receives_context(void)
{
    Anbo_Subscriber sub;
    Anbo_EBus_Subscribe(&sub, SIG_A, ctx_handler, (void *)"hello");

    Anbo_EBus_PublishSig(SIG_A, NULL);

    TEST_ASSERT_EQUAL_UINT32(1, s_order_count);
    TEST_ASSERT_EQUAL_STRING("hello", s_order_log[0]);
}

void test_ebus_different_ctx_per_subscriber(void)
{
    Anbo_Subscriber sub1, sub2;
    Anbo_EBus_Subscribe(&sub1, SIG_A, ctx_handler, (void *)"alpha");
    Anbo_EBus_Subscribe(&sub2, SIG_A, ctx_handler, (void *)"beta");

    Anbo_EBus_PublishSig(SIG_A, NULL);

    TEST_ASSERT_EQUAL_UINT32(2, s_order_count);
    TEST_ASSERT_EQUAL_STRING("alpha", s_order_log[0]);
    TEST_ASSERT_EQUAL_STRING("beta",  s_order_log[1]);
}

void test_ebus_null_context_ok(void)
{
    static void *captured_ctx = (void *)0xDEAD;
    /* inline-friendly: use a file-scope helper */
    Anbo_Subscriber sub;

    Anbo_EBus_Subscribe(&sub, SIG_A, ctx_handler, NULL);
    Anbo_EBus_PublishSig(SIG_A, NULL);

    /* ctx_handler logs NULL, which is fine — no crash */
    TEST_ASSERT_EQUAL_UINT32(1, s_order_count);
    (void)captured_ctx;
}

void test_ebus_param_propagation(void)
{
    Anbo_Subscriber sub;
    int payload = 777;

    Anbo_EBus_Subscribe(&sub, SIG_B, ctx_handler, (void *)"param_test");
    Anbo_EBus_PublishSig(SIG_B, &payload);

    TEST_ASSERT_EQUAL_UINT32(1, s_order_count);
    TEST_ASSERT_EQUAL_STRING("param_test", s_order_log[0]);
}

/* ================================================================ */
/*  FSM on_entry / on_exit / on_event ordering                       */
/* ================================================================ */

static void fsm_entry_a(Anbo_FSM *fsm) { (void)fsm; log_tag("entry_a"); }
static void fsm_exit_a(Anbo_FSM  *fsm) { (void)fsm; log_tag("exit_a");  }
static void fsm_entry_b(Anbo_FSM *fsm) { (void)fsm; log_tag("entry_b"); }
static void fsm_exit_b(Anbo_FSM  *fsm) { (void)fsm; log_tag("exit_b");  }

/* Forward declaration for state_b (needed by evt_a handler) */
static const Anbo_State s_cb_state_b;

static void fsm_evt_a(Anbo_FSM *fsm, const Anbo_Event *evt)
{
    (void)evt;
    log_tag("event_a");
    Anbo_FSM_Transfer(fsm, &s_cb_state_b);
}

static void fsm_evt_b(Anbo_FSM *fsm, const Anbo_Event *evt)
{
    (void)fsm; (void)evt;
    log_tag("event_b");
}

static const Anbo_State s_cb_state_a = {
    .name     = "A",
    .on_entry = fsm_entry_a,
    .on_exit  = fsm_exit_a,
    .on_event = fsm_evt_a,
};

static const Anbo_State s_cb_state_b = {
    .name     = "B",
    .on_entry = fsm_entry_b,
    .on_exit  = fsm_exit_b,
    .on_event = fsm_evt_b,
};

void test_fsm_init_calls_entry(void)
{
    Anbo_FSM fsm;
    Anbo_FSM_Init(&fsm, "test", &s_cb_state_a, NULL);

    TEST_ASSERT_EQUAL_UINT32(1, s_order_count);
    TEST_ASSERT_EQUAL_STRING("entry_a", s_order_log[0]);
}

void test_fsm_transfer_order_exit_then_entry(void)
{
    Anbo_FSM fsm;
    Anbo_FSM_Init(&fsm, "test", &s_cb_state_a, NULL);
    s_order_count = 0; /* reset after init's entry_a */

    Anbo_FSM_Subscribe(&fsm, SIG_A);
    Anbo_EBus_PublishSig(SIG_A, NULL);

    /* Expected order: event_a -> exit_a -> entry_b */
    TEST_ASSERT_EQUAL_UINT32(3, s_order_count);
    TEST_ASSERT_EQUAL_STRING("event_a", s_order_log[0]);
    TEST_ASSERT_EQUAL_STRING("exit_a",  s_order_log[1]);
    TEST_ASSERT_EQUAL_STRING("entry_b", s_order_log[2]);
}

void test_fsm_event_in_new_state_after_transfer(void)
{
    Anbo_FSM fsm;
    Anbo_FSM_Init(&fsm, "test", &s_cb_state_a, NULL);
    Anbo_FSM_Subscribe(&fsm, SIG_A);

    /* First event triggers transition A -> B */
    Anbo_EBus_PublishSig(SIG_A, NULL);
    s_order_count = 0;

    /* Second event should go to state B's handler */
    Anbo_EBus_PublishSig(SIG_A, NULL);

    TEST_ASSERT_EQUAL_UINT32(1, s_order_count);
    TEST_ASSERT_EQUAL_STRING("event_b", s_order_log[0]);
}

void test_fsm_user_data_accessible(void)
{
    int my_data = 42;
    Anbo_FSM fsm;
    Anbo_FSM_Init(&fsm, "test", &s_cb_state_a, &my_data);

    TEST_ASSERT_EQUAL_PTR(&my_data, fsm.user_data);
}

/* ================================================================ */
/*  Timer -> EBus -> FSM callback chain                              */
/* ================================================================ */

static uint32_t s_chain_count;

static void chain_fsm_on_event(Anbo_FSM *fsm, const Anbo_Event *evt)
{
    (void)fsm; (void)evt;
    s_chain_count++;
    log_tag("chain_fsm");
}

static const Anbo_State s_chain_state = {
    .name     = "chain",
    .on_entry = NULL,
    .on_exit  = NULL,
    .on_event = chain_fsm_on_event,
};

static void timer_fires_ebus(Anbo_Timer *tmr)
{
    (void)tmr;
    log_tag("timer_cb");
    Anbo_EBus_PublishSig(SIG_TMR, NULL);
}

void test_timer_ebus_fsm_chain(void)
{
    s_chain_count = 0;

    Anbo_FSM fsm;
    Anbo_FSM_Init(&fsm, "chain_fsm", &s_chain_state, NULL);
    Anbo_FSM_Subscribe(&fsm, SIG_TMR);

    Anbo_Timer tmr;
    Anbo_Timer_Create(&tmr, ANBO_TIMER_ONESHOT, 50, timer_fires_ebus, NULL);
    Anbo_Timer_Start(&tmr);

    /* Fire the timer */
    Anbo_Arch_Host_SetTick(50);
    Anbo_Timer_Update(50);

    TEST_ASSERT_EQUAL_UINT32(1, s_chain_count);
    /* Order: timer_cb -> chain_fsm */
    TEST_ASSERT_TRUE(s_order_count >= 2);
    TEST_ASSERT_EQUAL_STRING("timer_cb",  s_order_log[s_order_count - 2]);
    TEST_ASSERT_EQUAL_STRING("chain_fsm", s_order_log[s_order_count - 1]);
}

void test_periodic_timer_chains_multiple_times(void)
{
    s_chain_count = 0;

    Anbo_FSM fsm;
    Anbo_FSM_Init(&fsm, "chain_fsm", &s_chain_state, NULL);
    Anbo_FSM_Subscribe(&fsm, SIG_TMR);

    Anbo_Timer tmr;
    Anbo_Timer_Create(&tmr, ANBO_TIMER_PERIODIC, 100, timer_fires_ebus, NULL);
    Anbo_Timer_Start(&tmr);

    Anbo_Arch_Host_SetTick(100);
    Anbo_Timer_Update(100);
    Anbo_Arch_Host_SetTick(200);
    Anbo_Timer_Update(200);
    Anbo_Arch_Host_SetTick(300);
    Anbo_Timer_Update(300);

    TEST_ASSERT_EQUAL_UINT32(3, s_chain_count);
}

/* ================================================================ */
/*  Device tx_done / rx_ready callbacks                              */
/* ================================================================ */

static uint32_t s_txdone_count;
static uint32_t s_txdone_nbytes;

static void dev_txdone_cb(Anbo_Device *dev, uint32_t nbytes)
{
    (void)dev;
    s_txdone_count++;
    s_txdone_nbytes = nbytes;
    log_tag("tx_done");
}

static uint32_t s_rxready_count;
static uint32_t s_rxready_nbytes;

static void dev_rxready_cb(Anbo_Device *dev, uint32_t nbytes)
{
    (void)dev;
    s_rxready_count++;
    s_rxready_nbytes = nbytes;
    log_tag("rx_ready");
}

void test_dev_txdone_callback_fires(void)
{
    s_txdone_count  = 0;
    s_txdone_nbytes = 0;

    Anbo_Device dev = {
        .name     = "uart",
        .ops      = NULL,
        .tx_rb    = NULL,
        .rx_rb    = NULL,
        .tx_done  = dev_txdone_cb,
        .rx_ready = NULL,
        .sig_tx   = SIG_A,
        .sig_rx   = 0,
        .priv     = NULL,
        .flags    = ANBO_DEV_FLAG_OPENED | ANBO_DEV_FLAG_TX_BUSY,
    };

    Anbo_Dev_TxComplete(&dev, 128);

    TEST_ASSERT_EQUAL_UINT32(1, s_txdone_count);
    TEST_ASSERT_EQUAL_UINT32(128, s_txdone_nbytes);
    TEST_ASSERT_EQUAL_UINT8(ANBO_DEV_FLAG_OPENED, dev.flags);
}

void test_dev_rxready_callback_fires(void)
{
    s_rxready_count  = 0;
    s_rxready_nbytes = 0;

    Anbo_Device dev = {
        .name     = "uart",
        .ops      = NULL,
        .tx_rb    = NULL,
        .rx_rb    = NULL,
        .tx_done  = NULL,
        .rx_ready = dev_rxready_cb,
        .sig_tx   = 0,
        .sig_rx   = SIG_B,
        .priv     = NULL,
        .flags    = ANBO_DEV_FLAG_OPENED,
    };

    Anbo_Dev_RxNotify(&dev, 32);

    TEST_ASSERT_EQUAL_UINT32(1, s_rxready_count);
    TEST_ASSERT_EQUAL_UINT32(32, s_rxready_nbytes);
}

void test_dev_txdone_plus_ebus_signal(void)
{
    s_txdone_count = 0;
    s_order_count  = 0;

    Anbo_Device dev = {
        .name     = "uart",
        .ops      = NULL,
        .tx_rb    = NULL,
        .rx_rb    = NULL,
        .tx_done  = dev_txdone_cb,
        .rx_ready = NULL,
        .sig_tx   = SIG_A,
        .sig_rx   = 0,
        .priv     = NULL,
        .flags    = ANBO_DEV_FLAG_OPENED | ANBO_DEV_FLAG_TX_BUSY,
    };

    Anbo_Subscriber sub;
    Anbo_EBus_Subscribe(&sub, SIG_A, ctx_handler, (void *)"ebus_tx");

    Anbo_Dev_TxComplete(&dev, 64);

    /* Both callback and EBus signal should fire */
    TEST_ASSERT_EQUAL_UINT32(1, s_txdone_count);
    TEST_ASSERT_TRUE(s_order_count >= 2);
    TEST_ASSERT_EQUAL_STRING("tx_done",  s_order_log[0]);
    TEST_ASSERT_EQUAL_STRING("ebus_tx",  s_order_log[1]);
}

void test_dev_null_callback_no_crash(void)
{
    Anbo_Device dev = {
        .name     = "uart",
        .ops      = NULL,
        .tx_rb    = NULL,
        .rx_rb    = NULL,
        .tx_done  = NULL,
        .rx_ready = NULL,
        .sig_tx   = SIG_A,
        .sig_rx   = SIG_B,
        .priv     = NULL,
        .flags    = ANBO_DEV_FLAG_OPENED | ANBO_DEV_FLAG_TX_BUSY,
    };

    /* Should not crash even with NULL callbacks */
    Anbo_Dev_TxComplete(&dev, 0);
    Anbo_Dev_RxNotify(&dev, 0);
}
