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
 * @file  app_fault_mgr.c
 * @brief FaultManager implementation
 *
 * Centralised fault table + periodic recovery scanning.
 * Publishes fault state changes as Pool async events through EBus,
 * so the controller FSM receives them like any other business signal.
 */

#include "anbo_config.h"

#if ANBO_CONF_POOL

#include "app_fault_mgr.h"
#include "app_signals.h"
#include "anbo_pool.h"
#include "anbo_ebus.h"
#include "anbo_timer.h"
#include "anbo_log.h"
#include "anbo_arch.h"

/* ================================================================== */
/*  Pool event payload for fault notifications                         */
/* ================================================================== */

typedef struct {
    Anbo_PoolEvent super;       /* must be first */
    FaultId        fault_id;
    FaultSeverity  severity;
    FaultState     new_state;
} FaultEvent;

/* ================================================================== */
/*  Static fault table                                                 */
/* ================================================================== */

#define FAULT_TABLE_SIZE    8

static FaultEntry s_fault_table[FAULT_TABLE_SIZE];
static uint8_t    s_fault_count;

/* Recovery-scan timer (500 ms periodic) */
static Anbo_Timer s_recover_timer;

/* ================================================================== */
/*  Internal: find entry by ID                                         */
/* ================================================================== */

static FaultEntry *find_entry(FaultId id)
{
    for (uint8_t i = 0; i < s_fault_count; i++) {
        if (s_fault_table[i].id == id) {
            return &s_fault_table[i];
        }
    }
    return NULL;
}

/* ================================================================== */
/*  Internal: post a fault event to the async queue                    */
/* ================================================================== */

static void fault_notify(FaultId id, FaultSeverity sev,
                          FaultState new_state, uint16_t sig)
{
    FaultEvent *evt = (FaultEvent *)Anbo_Pool_Alloc();
    if (evt == NULL) {
        ANBO_LOGE("FaultMgr: pool exhausted, fault %x lost", (unsigned)id);
        return;
    }
    evt->super.sig = sig;
    evt->fault_id  = id;
    evt->severity  = sev;
    evt->new_state = new_state;

    if (Anbo_EvtQ_Post(&evt->super) != 0) {
        Anbo_Pool_Free(evt);
        ANBO_LOGE("FaultMgr: evtq full, fault %x lost", (unsigned)id);
    }
}

/* ================================================================== */
/*  Recovery-scan timer callback                                       */
/* ================================================================== */

static void recover_scan_cb(Anbo_Timer *tmr)
{
    (void)tmr;

    for (uint8_t i = 0; i < s_fault_count; i++) {
        FaultEntry *e = &s_fault_table[i];

        /* Only probe ACTIVE faults that have a recovery check */
        if (e->state != FAULT_STATE_ACTIVE) { continue; }
        if (e->recover_check == NULL)       { continue; }

        if (e->recover_check(e->id, e->recover_ctx)) {
            ANBO_LOGI("FaultMgr: [%s] auto-recovered", e->name);
            e->state       = FAULT_STATE_INACTIVE;
            e->retry_count = 0;
            fault_notify(e->id, e->severity,
                         FAULT_STATE_INACTIVE, APP_SIG_FAULT_CLR);
        }
    }
}

/* ================================================================== */
/*  Public API                                                         */
/* ================================================================== */

void Fault_Manager_Init(void)
{
    s_fault_count = 0;

    /* Start periodic recovery scanner */
    Anbo_Timer_Create(&s_recover_timer, ANBO_TIMER_PERIODIC, 500u,
                      recover_scan_cb, NULL);
    Anbo_Timer_Start(&s_recover_timer);

    ANBO_LOGI("FaultMgr: init, recover scan 500 ms");
}

int Fault_Register(FaultId          id,
                   const char      *name,
                   FaultSeverity    severity,
                   uint8_t          retry_max,
                   uint32_t         retry_interval_ms,
                   FaultRecoverCheck recover_check,
                   void             *recover_ctx)
{
    if (s_fault_count >= FAULT_TABLE_SIZE) {
        ANBO_LOGE("FaultMgr: table full");
        return -1;
    }

    FaultEntry *e       = &s_fault_table[s_fault_count++];
    e->id               = id;
    e->name             = name;
    e->severity         = severity;
    e->state            = FAULT_STATE_INACTIVE;
    e->retry_count      = 0;
    e->retry_max        = retry_max;
    e->retry_interval_ms = retry_interval_ms;
    e->first_ts         = 0;
    e->last_ts          = 0;
    e->occurrence_count = 0;
    e->recover_check    = recover_check;
    e->recover_ctx      = recover_ctx;
    return 0;
}

void Fault_Report(FaultId id, uint32_t now)
{
    FaultEntry *e = find_entry(id);
    if (e == NULL) {
        ANBO_LOGE("FaultMgr: unknown fault %x", (unsigned)id);
        return;
    }

    e->occurrence_count++;
    e->last_ts = now;

    if (e->state == FAULT_STATE_INACTIVE) {
        e->first_ts    = now;
        e->retry_count = 0;
    }

    /* Already latched — no further escalation */
    if (e->state == FAULT_STATE_LATCHED) {
        return;
    }

    /* Check retry budget */
    if (e->retry_count >= e->retry_max) {
        e->state = FAULT_STATE_LATCHED;
        ANBO_LOGE("FaultMgr: [%s] LATCHED (retries exhausted)", e->name);
        fault_notify(id, e->severity,
                     FAULT_STATE_LATCHED, APP_SIG_FAULT_LATCHED);
    } else {
        e->state = FAULT_STATE_ACTIVE;
        e->retry_count++;
        ANBO_LOGW("FaultMgr: [%s] ACTIVE (retry %u/%u)",
                  e->name,
                  (unsigned)e->retry_count, (unsigned)e->retry_max);
        fault_notify(id, e->severity,
                     FAULT_STATE_ACTIVE, APP_SIG_FAULT_SET);
    }
}

void Fault_Clear(FaultId id)
{
    FaultEntry *e = find_entry(id);
    if (e == NULL) { return; }
    if (e->state == FAULT_STATE_INACTIVE) { return; }

    ANBO_LOGI("FaultMgr: [%s] CLEARED", e->name);
    e->state       = FAULT_STATE_INACTIVE;
    e->retry_count = 0;
    fault_notify(id, e->severity, FAULT_STATE_INACTIVE, APP_SIG_FAULT_CLR);
}

bool Fault_IsActive(FaultId id)
{
    FaultEntry *e = find_entry(id);
    return (e != NULL) && (e->state >= FAULT_STATE_ACTIVE);
}

bool Fault_IsLatched(FaultId id)
{
    FaultEntry *e = find_entry(id);
    return (e != NULL) && (e->state == FAULT_STATE_LATCHED);
}

FaultSeverity Fault_MaxActiveSeverity(void)
{
    FaultSeverity max_sev = FAULT_SEV_INFO;
    for (uint8_t i = 0; i < s_fault_count; i++) {
        if (s_fault_table[i].state >= FAULT_STATE_ACTIVE &&
            s_fault_table[i].severity > max_sev) {
            max_sev = s_fault_table[i].severity;
        }
    }
    return max_sev;
}

#endif /* ANBO_CONF_POOL */
