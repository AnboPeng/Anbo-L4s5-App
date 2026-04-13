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
 * @file  app_controller.c
 * @brief Temperature alarm controller — 3-state FSM (Normal / Alarm / Fault)
 *
 * Demonstrates FSM + EBus auto-routing + Pool async events + FaultManager:
 *
 *   APP_SIG_TEMP_UPDATE    → compare against threshold → Normal ↔ Alarm
 *   APP_SIG_THRESHOLD_SET  → update threshold from UART command
 *   APP_SIG_FAULT_SET      → enter Fault state (degraded mode)
 *   APP_SIG_FAULT_CLR      → recover to pre-fault state
 *   APP_SIG_FAULT_LATCHED  → remain in Fault (retries exhausted)
 *   State change           → publish APP_SIG_ALARM_STATE (sync EBus)
 *
 * The "pre_fault_state" pointer in the FSM user_data remembers where we
 * came from, so recovery can route back to Normal or Alarm correctly.
 */

#include "anbo_config.h"

#if ANBO_CONF_POOL

#include "app_controller.h"
#include "app_signals.h"
#include "app_config.h"
#include "anbo_fsm.h"
#include "anbo_ebus.h"
#include "anbo_log.h"
#include "app_fault.h"
#include "app_fault_mgr.h"

#if ANBO_CONF_WDT
#include "anbo_wdt.h"
#endif

/* ================================================================== */
/*  Derived Pool Event (shared definition — must match app_sensor.c)   */
/* ================================================================== */

#include "anbo_pool.h"

typedef struct {
    Anbo_PoolEvent super;
    int32_t        temp_x10;
} TempEvent;

typedef struct {
    Anbo_PoolEvent super;
    int32_t        threshold_x10;
} ThresholdEvent;

/** Fault event payload (must match app_fault_mgr.c) */
typedef struct {
    Anbo_PoolEvent super;
    FaultId        fault_id;
    FaultSeverity  severity;
    FaultState     new_state;
} FaultEvent;

/* ================================================================== */
/*  Controller context (carries pre-fault memory)                      */
/* ================================================================== */

typedef struct {
    const Anbo_State *pre_fault_state;   /**< State before entering Fault */
} CtrlContext;

static Anbo_FSM    s_ctrl_fsm;
static CtrlContext  s_ctrl_ctx;

#if ANBO_CONF_WDT
static Anbo_WDT_Slot s_ctrl_wdt = ANBO_WDT_SLOT_INVALID;
#endif

/** Runtime alarm threshold (0.1 °C).  Initialised from NVM config. */
static int32_t s_threshold_x10;

/** Last known alarm state (0 = normal, 1 = alarm). */
static uint8_t s_alarm_active;

/* ---- Forward-declared states ---- */
static void normal_on_entry(Anbo_FSM *fsm);
static void normal_on_event(Anbo_FSM *fsm, const Anbo_Event *evt);

static void alarm_on_entry(Anbo_FSM *fsm);
static void alarm_on_event(Anbo_FSM *fsm, const Anbo_Event *evt);

static void fault_on_entry(Anbo_FSM *fsm);
static void fault_on_exit(Anbo_FSM *fsm);
static void fault_on_event(Anbo_FSM *fsm, const Anbo_Event *evt);

static const Anbo_State s_state_normal = {
    .name     = "Normal",
    .on_entry = normal_on_entry,
    .on_exit  = NULL,
    .on_event = normal_on_event,
};

static const Anbo_State s_state_alarm = {
    .name     = "Alarm",
    .on_entry = alarm_on_entry,
    .on_exit  = NULL,
    .on_event = alarm_on_event,
};

static const Anbo_State s_state_fault = {
    .name     = "Fault",
    .on_entry = fault_on_entry,
    .on_exit  = fault_on_exit,
    .on_event = fault_on_event,
};

/* ================================================================== */
/*  Helper — publish alarm state change via sync EBus                  */
/* ================================================================== */

static void publish_alarm(uint8_t active)
{
    s_alarm_active = active;
    /* param carries 0/1 as integer cast to void* (no allocation) */
    Anbo_EBus_PublishSig(APP_SIG_ALARM_STATE, (void *)(uintptr_t)active);
}

/* ================================================================== */
/*  Helper — handle threshold-set in any state                         */
/* ================================================================== */

static void handle_threshold(const Anbo_Event *evt)
{
    const ThresholdEvent *te = (const ThresholdEvent *)evt->param;
    if (te == NULL) {
        return;
    }
    s_threshold_x10 = te->threshold_x10;

#if APP_CONF_PARAM_FLASH
    g_app_cfg.threshold = (uint32_t)s_threshold_x10;
    App_Config_Save();
#endif

    ANBO_LOGI("Ctrl: threshold -> %d.%d C",
              (int)(s_threshold_x10 / 10),
              (int)(s_threshold_x10 % 10));
}

/* ================================================================== */
/*  Helper — enter Fault state, remembering where we came from         */
/* ================================================================== */

static void enter_fault(Anbo_FSM *fsm)
{
    CtrlContext *ctx = (CtrlContext *)fsm->user_data;
    ctx->pre_fault_state = fsm->current;
    Anbo_FSM_Transfer(fsm, &s_state_fault);
}

/* ================================================================== */
/*  Helper — common fault signal handler (used in Normal & Alarm)      */
/* ================================================================== */

/**
 * @retval true  Signal was a fault signal and was handled.
 * @retval false Signal is not a fault signal.
 */
static bool handle_fault_signal(Anbo_FSM *fsm, const Anbo_Event *evt)
{
    switch (evt->sig) {
    case APP_SIG_FAULT_SET:
    case APP_SIG_FAULT_LATCHED: {
        const FaultEvent *fe = (const FaultEvent *)evt->param;
        if (fe == NULL) { return true; }
        if (fe->severity >= FAULT_SEV_WARNING) {
            ANBO_LOGW("Ctrl: fault %x sev=%u -> Fault state",
                      (unsigned)fe->fault_id, (unsigned)fe->severity);
            enter_fault(fsm);
        }
        return true;
    }
    case APP_SIG_FAULT_CLR:
        /* Already in a normal/alarm state — nothing to do */
        return true;
    default:
        return false;
    }
}

/* ================================================================== */
/*  State: Normal                                                      */
/* ================================================================== */

static void normal_on_entry(Anbo_FSM *fsm)
{
    (void)fsm;
    ANBO_LOGI("Ctrl: -> Normal");
    publish_alarm(0u);
}

static void normal_on_event(Anbo_FSM *fsm, const Anbo_Event *evt)
{
#if ANBO_CONF_WDT
    if (s_ctrl_wdt >= 0) {
        Anbo_WDT_Checkin(s_ctrl_wdt);
    }
#endif

    /* Check fault signals first */
    if (handle_fault_signal(fsm, evt)) { return; }

    switch (evt->sig) {
    case APP_SIG_TEMP_UPDATE: {
        const TempEvent *te = (const TempEvent *)evt->param;
        if (te != NULL && te->temp_x10 >= s_threshold_x10) {
            ANBO_LOGI("Ctrl: %d.%d >= %d.%d => ALARM",
                      (int)(te->temp_x10 / 10), (int)(te->temp_x10 % 10),
                      (int)(s_threshold_x10 / 10), (int)(s_threshold_x10 % 10));
            Anbo_FSM_Transfer(fsm, &s_state_alarm);
        }
        break;
    }
    case APP_SIG_THRESHOLD_SET:
        handle_threshold(evt);
        break;
    default:
        break;
    }
}

/* ================================================================== */
/*  State: Alarm                                                       */
/* ================================================================== */

static void alarm_on_entry(Anbo_FSM *fsm)
{
    (void)fsm;
    ANBO_LOGI("Ctrl: -> Alarm");
    publish_alarm(1u);
}

static void alarm_on_event(Anbo_FSM *fsm, const Anbo_Event *evt)
{
#if ANBO_CONF_WDT
    if (s_ctrl_wdt >= 0) {
        Anbo_WDT_Checkin(s_ctrl_wdt);
    }
#endif

    /* Check fault signals first */
    if (handle_fault_signal(fsm, evt)) { return; }

    switch (evt->sig) {
    case APP_SIG_TEMP_UPDATE: {
        const TempEvent *te = (const TempEvent *)evt->param;
        /* Add 5 (0.5 °C) hysteresis to avoid oscillation */
        if (te != NULL && te->temp_x10 < (s_threshold_x10 - 5)) {
            ANBO_LOGI("Ctrl: %d.%d < %d.%d => NORMAL",
                      (int)(te->temp_x10 / 10), (int)(te->temp_x10 % 10),
                      (int)((s_threshold_x10 - 5) / 10),
                      (int)((s_threshold_x10 - 5) % 10));
            Anbo_FSM_Transfer(fsm, &s_state_normal);
        }
        break;
    }
    case APP_SIG_THRESHOLD_SET:
        handle_threshold(evt);
        break;
    default:
        break;
    }
}

/* ================================================================== */
/*  State: Fault (degraded mode — waiting for recovery)                */
/* ================================================================== */

static void fault_on_entry(Anbo_FSM *fsm)
{
    CtrlContext *ctx = (CtrlContext *)fsm->user_data;
    ANBO_LOGE("Ctrl: -> Fault (from %s)", ctx->pre_fault_state->name);

    /* Suspend WDT monitoring — Fault is a designed idle state,
     * not a stuck task.  Will resume when we leave Fault. */
#if ANBO_CONF_WDT
    if (s_ctrl_wdt >= 0) {
        Anbo_WDT_Suspend(s_ctrl_wdt);
    }
#endif

    /* Publish alarm=1 so UI shows fast blink while in fault */
    publish_alarm(1u);
}

static void fault_on_exit(Anbo_FSM *fsm)
{
    (void)fsm;

    /* Resume WDT monitoring — back to active event processing */
#if ANBO_CONF_WDT
    if (s_ctrl_wdt >= 0) {
        Anbo_WDT_Resume(s_ctrl_wdt);
    }
#endif

    ANBO_LOGI("Ctrl: <- Fault (recovered)");
}

static void fault_on_event(Anbo_FSM *fsm, const Anbo_Event *evt)
{
    /* No WDT checkin here — slot is suspended while in Fault state */

    switch (evt->sig) {
    case APP_SIG_FAULT_CLR: {
        /* All faults cleared? Return to pre-fault state */
        if (Fault_MaxActiveSeverity() < FAULT_SEV_WARNING) {
            CtrlContext *ctx = (CtrlContext *)fsm->user_data;
            ANBO_LOGI("Ctrl: faults cleared -> %s",
                      ctx->pre_fault_state->name);
            Anbo_FSM_Transfer(fsm, ctx->pre_fault_state);
        }
        break;
    }
    case APP_SIG_FAULT_LATCHED: {
        /* Fault escalated while already in Fault state — stay here */
        const FaultEvent *fe = (const FaultEvent *)evt->param;
        if (fe != NULL) {
            ANBO_LOGE("Ctrl: fault %x LATCHED while in Fault state",
                      (unsigned)fe->fault_id);
        }
        break;
    }
    case APP_SIG_FAULT_SET:
        /* Another fault while already degraded — stay here */
        break;
    case APP_SIG_TEMP_UPDATE:
        /* Silently ignore temperature while in fault (sensor unreliable) */
        break;
    case APP_SIG_THRESHOLD_SET:
        /* Still allow threshold changes */
        handle_threshold(evt);
        break;
    default:
        break;
    }
}

/* ================================================================== */
/*  Public init                                                        */
/* ================================================================== */

void App_Controller_Init(void)
{
    /* Threshold from NVM — g_app_cfg.threshold is in 0.1 °C units.
     * Default 350 => 35.0 °C  (see App_Config_Init defaults). */
#if APP_CONF_PARAM_FLASH
    s_threshold_x10 = (int32_t)g_app_cfg.threshold;
#else
    s_threshold_x10 = 350;  /* 35.0 °C default */
#endif

    s_alarm_active = 0u;
    s_ctrl_ctx.pre_fault_state = &s_state_normal;

#if ANBO_CONF_WDT
    s_ctrl_wdt = Anbo_WDT_Register("ctrl", 2000u);
#endif

    /* Init FSM — enters Normal state immediately, carries CtrlContext */
    Anbo_FSM_Init(&s_ctrl_fsm, "TempCtrl", &s_state_normal, &s_ctrl_ctx);

    /* Subscribe to temperature + threshold + fault signals */
    Anbo_FSM_Subscribe(&s_ctrl_fsm, APP_SIG_TEMP_UPDATE);
    Anbo_FSM_Subscribe(&s_ctrl_fsm, APP_SIG_THRESHOLD_SET);
    Anbo_FSM_Subscribe(&s_ctrl_fsm, APP_SIG_FAULT_SET);
    Anbo_FSM_Subscribe(&s_ctrl_fsm, APP_SIG_FAULT_CLR);
    Anbo_FSM_Subscribe(&s_ctrl_fsm, APP_SIG_FAULT_LATCHED);

    ANBO_LOGI("Controller: FSM ready (Normal/Alarm/Fault), threshold=%d.%d C",
              (int)(s_threshold_x10 / 10), (int)(s_threshold_x10 % 10));
}

void App_Controller_Stop(void)
{
#if ANBO_CONF_WDT
    if (s_ctrl_wdt >= 0) {
        Anbo_WDT_Suspend(s_ctrl_wdt);
    }
#endif
}

void App_Controller_Resume(void)
{
#if ANBO_CONF_WDT
    if (s_ctrl_wdt >= 0) {
        Anbo_WDT_Resume(s_ctrl_wdt);
    }
#endif
}

#endif /* ANBO_CONF_POOL */
