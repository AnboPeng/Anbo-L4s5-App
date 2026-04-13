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
 * @file  test_anbo_isr.c
 * @brief Unit tests for hardware interrupt mock — ISR bottom-half pattern.
 *
 * Tests the Anbo_Dev_ISR_Post / Anbo_Dev_TxComplete / Anbo_Dev_RxNotify
 * interrupt bottom-half functions, verifying that ISR-published signals
 * are correctly routed through the EBus to FSM state handlers.
 *
 * This validates the full ISR -> EBus -> FSM chain on the host.
 */

#include "unity.h"
#include "anbo_ebus.h"
#include "anbo_dev.h"
#include "anbo_fsm.h"
#include "anbo_rb.h"
#include "anbo_arch_host.h"

/* ---- Test signal definitions ---- */
#define SIG_UART_RX     1
#define SIG_UART_TX     2
#define SIG_BTN_PRESS   3
#define SIG_SENSOR      4

/* ---- Event log for verification ---- */
#define MAX_EVT_LOG  32

typedef struct {
    uint16_t sig;
    void    *param;
    void    *ctx;
} EvtRecord;

static EvtRecord s_evt_log[MAX_EVT_LOG];
static uint32_t  s_evt_count;

static void evt_handler(const Anbo_Event *evt, void *ctx)
{
    if (s_evt_count < MAX_EVT_LOG) {
        s_evt_log[s_evt_count].sig   = evt->sig;
        s_evt_log[s_evt_count].param = evt->param;
        s_evt_log[s_evt_count].ctx   = ctx;
        s_evt_count++;
    }
}

/* ---- Setup / Teardown ---- */

void setUp(void)
{
    s_evt_count = 0;
    Anbo_Arch_Host_ResetCritical();
    Anbo_EBus_Init();
}

void tearDown(void)
{
    TEST_ASSERT_EQUAL_INT32(0, Anbo_Arch_Host_GetCriticalDepth());
}

/* ================================================================ */
/*  ISR_Post basic — signal reaches subscriber                      */
/* ================================================================ */

void test_isr_post_delivers_signal(void)
{
    Anbo_Subscriber sub;
    Anbo_EBus_Subscribe(&sub, SIG_BTN_PRESS, evt_handler, NULL);

    /* Simulate button ISR */
    Anbo_Dev_ISR_Post(SIG_BTN_PRESS, NULL);

    TEST_ASSERT_EQUAL_UINT32(1, s_evt_count);
    TEST_ASSERT_EQUAL_UINT16(SIG_BTN_PRESS, s_evt_log[0].sig);
}

void test_isr_post_zero_sig_ignored(void)
{
    Anbo_Subscriber sub;
    Anbo_EBus_Subscribe(&sub, 0, evt_handler, NULL);

    Anbo_Dev_ISR_Post(0, NULL);

    TEST_ASSERT_EQUAL_UINT32(0, s_evt_count);
}

void test_isr_post_passes_param(void)
{
    Anbo_Subscriber sub;
    int payload = 99;
    Anbo_EBus_Subscribe(&sub, SIG_SENSOR, evt_handler, NULL);

    Anbo_Dev_ISR_Post(SIG_SENSOR, &payload);

    TEST_ASSERT_EQUAL_UINT32(1, s_evt_count);
    TEST_ASSERT_EQUAL_PTR(&payload, s_evt_log[0].param);
}

/* ================================================================ */
/*  ISR_Post -> multiple subscribers                                 */
/* ================================================================ */

void test_isr_post_multiple_subscribers(void)
{
    Anbo_Subscriber sub1, sub2, sub3;
    int ctx1 = 1, ctx2 = 2, ctx3 = 3;

    Anbo_EBus_Subscribe(&sub1, SIG_BTN_PRESS, evt_handler, &ctx1);
    Anbo_EBus_Subscribe(&sub2, SIG_BTN_PRESS, evt_handler, &ctx2);
    Anbo_EBus_Subscribe(&sub3, SIG_SENSOR,    evt_handler, &ctx3);

    Anbo_Dev_ISR_Post(SIG_BTN_PRESS, NULL);

    /* sub1 and sub2 should fire; sub3 (different sig) should not */
    TEST_ASSERT_EQUAL_UINT32(2, s_evt_count);
    TEST_ASSERT_EQUAL_PTR(&ctx1, s_evt_log[0].ctx);
    TEST_ASSERT_EQUAL_PTR(&ctx2, s_evt_log[1].ctx);
}

/* ================================================================ */
/*  TxComplete bottom-half                                           */
/* ================================================================ */

static uint32_t s_txdone_bytes;
static void txdone_cb(Anbo_Device *dev, uint32_t nbytes)
{
    (void)dev;
    s_txdone_bytes = nbytes;
}

void test_tx_complete_clears_busy_and_fires_callback(void)
{
    s_txdone_bytes = 0;

    Anbo_Device dev = {
        .name    = "uart1",
        .ops     = NULL,
        .tx_rb   = NULL,
        .rx_rb   = NULL,
        .tx_done = txdone_cb,
        .rx_ready = NULL,
        .sig_tx  = SIG_UART_TX,
        .sig_rx  = 0,
        .priv    = NULL,
        .flags   = ANBO_DEV_FLAG_OPENED | ANBO_DEV_FLAG_TX_BUSY,
    };

    Anbo_Subscriber sub;
    Anbo_EBus_Subscribe(&sub, SIG_UART_TX, evt_handler, NULL);

    Anbo_Dev_TxComplete(&dev, 64);

    /* TX_BUSY should be cleared */
    TEST_ASSERT_EQUAL_UINT8(ANBO_DEV_FLAG_OPENED, dev.flags);
    /* Callback should have been called */
    TEST_ASSERT_EQUAL_UINT32(64, s_txdone_bytes);
    /* EBus should have received the signal */
    TEST_ASSERT_EQUAL_UINT32(1, s_evt_count);
    TEST_ASSERT_EQUAL_UINT16(SIG_UART_TX, s_evt_log[0].sig);
    TEST_ASSERT_EQUAL_PTR(&dev, s_evt_log[0].param);
}

void test_tx_complete_null_dev(void)
{
    /* Should not crash */
    Anbo_Dev_TxComplete(NULL, 0);
    TEST_ASSERT_EQUAL_UINT32(0, s_evt_count);
}

/* ================================================================ */
/*  RxNotify bottom-half                                             */
/* ================================================================ */

static uint32_t s_rxready_bytes;
static void rxready_cb(Anbo_Device *dev, uint32_t nbytes)
{
    (void)dev;
    s_rxready_bytes = nbytes;
}

void test_rx_notify_fires_callback_and_signal(void)
{
    s_rxready_bytes = 0;

    Anbo_Device dev = {
        .name     = "uart1",
        .ops      = NULL,
        .tx_rb    = NULL,
        .rx_rb    = NULL,
        .tx_done  = NULL,
        .rx_ready = rxready_cb,
        .sig_tx   = 0,
        .sig_rx   = SIG_UART_RX,
        .priv     = NULL,
        .flags    = ANBO_DEV_FLAG_OPENED,
    };

    Anbo_Subscriber sub;
    Anbo_EBus_Subscribe(&sub, SIG_UART_RX, evt_handler, NULL);

    Anbo_Dev_RxNotify(&dev, 10);

    TEST_ASSERT_EQUAL_UINT32(10, s_rxready_bytes);
    TEST_ASSERT_EQUAL_UINT32(1, s_evt_count);
    TEST_ASSERT_EQUAL_UINT16(SIG_UART_RX, s_evt_log[0].sig);
}

void test_rx_notify_no_callback_still_sends_signal(void)
{
    Anbo_Device dev = {
        .name     = "uart1",
        .ops      = NULL,
        .tx_rb    = NULL,
        .rx_rb    = NULL,
        .tx_done  = NULL,
        .rx_ready = NULL,
        .sig_tx   = 0,
        .sig_rx   = SIG_UART_RX,
        .priv     = NULL,
        .flags    = ANBO_DEV_FLAG_OPENED,
    };

    Anbo_Subscriber sub;
    Anbo_EBus_Subscribe(&sub, SIG_UART_RX, evt_handler, NULL);

    Anbo_Dev_RxNotify(&dev, 5);

    /* Callback not called (NULL), but EBus signal still published */
    TEST_ASSERT_EQUAL_UINT32(1, s_evt_count);
}

/* ================================================================ */
/*  Full ISR -> EBus -> FSM chain                                    */
/* ================================================================ */

static uint32_t s_fsm_evt_count;
static uint16_t s_fsm_last_sig;

static void fsm_state_on_event(Anbo_FSM *fsm, const Anbo_Event *evt)
{
    (void)fsm;
    s_fsm_last_sig = evt->sig;
    s_fsm_evt_count++;
}

void test_isr_to_ebus_to_fsm(void)
{
    s_fsm_evt_count = 0;
    s_fsm_last_sig  = 0;

    static const Anbo_State state_idle = {
        .name     = "idle",
        .on_entry = NULL,
        .on_exit  = NULL,
        .on_event = fsm_state_on_event,
    };

    Anbo_FSM fsm;
    Anbo_FSM_Init(&fsm, "test_fsm", &state_idle, NULL);
    Anbo_FSM_Subscribe(&fsm, SIG_BTN_PRESS);

    /* Simulate button ISR */
    Anbo_Dev_ISR_Post(SIG_BTN_PRESS, NULL);

    TEST_ASSERT_EQUAL_UINT32(1, s_fsm_evt_count);
    TEST_ASSERT_EQUAL_UINT16(SIG_BTN_PRESS, s_fsm_last_sig);
}

/* ---- FSM state transition helpers (file scope for C99 compliance) ---- */
static const Anbo_State s_state_active_tx;

static void idle_tx_on_event(Anbo_FSM *fsm, const Anbo_Event *evt)
{
    (void)evt;
    s_fsm_evt_count++;
    Anbo_FSM_Transfer(fsm, &s_state_active_tx);
}

static void active_tx_on_entry(Anbo_FSM *fsm)
{
    (void)fsm;
    s_fsm_evt_count += 100;  /* sentinel value */
}

static const Anbo_State s_state_idle_tx = {
    .name     = "idle",
    .on_entry = NULL,
    .on_exit  = NULL,
    .on_event = idle_tx_on_event,
};

static const Anbo_State s_state_active_tx = {
    .name     = "active",
    .on_entry = active_tx_on_entry,
    .on_exit  = NULL,
    .on_event = NULL,
};

void test_isr_uart_rx_to_fsm_state_transition(void)
{
    s_fsm_evt_count = 0;

    Anbo_FSM fsm;
    Anbo_FSM_Init(&fsm, "uart_fsm", &s_state_idle_tx, NULL);
    Anbo_FSM_Subscribe(&fsm, SIG_UART_RX);

    /* Simulate UART RX ISR */
    Anbo_Dev_ISR_Post(SIG_UART_RX, NULL);

    /* 1 from on_event + 100 from on_entry */
    TEST_ASSERT_EQUAL_UINT32(101, s_fsm_evt_count);
}

/* ================================================================ */
/*  Unsubscribe stops delivery                                       */
/* ================================================================ */

void test_unsubscribe_stops_isr_delivery(void)
{
    Anbo_Subscriber sub;
    Anbo_EBus_Subscribe(&sub, SIG_BTN_PRESS, evt_handler, NULL);

    Anbo_Dev_ISR_Post(SIG_BTN_PRESS, NULL);
    TEST_ASSERT_EQUAL_UINT32(1, s_evt_count);

    Anbo_EBus_Unsubscribe(&sub);
    Anbo_Dev_ISR_Post(SIG_BTN_PRESS, NULL);
    TEST_ASSERT_EQUAL_UINT32(1, s_evt_count);  /* no new delivery */
}
