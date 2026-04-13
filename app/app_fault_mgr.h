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
 * @file  app_fault_mgr.h
 * @brief FaultManager — centralised fault registration, reporting,
 *        querying and auto-recovery scanning
 *
 * Usage:
 *   1. Call Fault_Manager_Init() once at startup.
 *   2. Register each possible fault via Fault_Register().
 *   3. Drivers call Fault_Report() when self-heal fails.
 *   4. FSM subscribes to APP_SIG_FAULT_SET / _CLR / _LATCHED via EBus.
 *   5. FSM queries Fault_IsActive() / Fault_MaxActiveSeverity()
 *      to decide on transitions.
 *
 * The manager runs a periodic timer that calls each ACTIVE fault's
 * recover_check callback; on success it auto-clears the fault and
 * publishes APP_SIG_FAULT_CLR.
 */

#ifndef APP_FAULT_MGR_H
#define APP_FAULT_MGR_H

#include "app_fault.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  Initialise the FaultManager (clears table, starts recovery timer).
 *         Call once at startup, after Timer subsystem is initialised.
 */
void Fault_Manager_Init(void);

/**
 * @brief  Register a fault descriptor.
 *
 * @param  id               Fault identifier (from FaultId enum).
 * @param  name             Human-readable name for logging.
 * @param  severity         Severity level.
 * @param  retry_max        Max retries before latching (0 = latch immediately).
 * @param  retry_interval_ms  Minimum interval between successive Reports
 *                             (currently informational; future debounce).
 * @param  recover_check    Callback probed by the recovery timer (may be NULL).
 * @param  recover_ctx      Context passed to recover_check.
 * @retval 0   Success.
 * @retval -1  Table full.
 */
int Fault_Register(FaultId          id,
                   const char      *name,
                   FaultSeverity    severity,
                   uint8_t          retry_max,
                   uint32_t         retry_interval_ms,
                   FaultRecoverCheck recover_check,
                   void             *recover_ctx);

/**
 * @brief  Report a fault occurrence (driver calls this after self-heal failure).
 *
 * If the fault transitions INACTIVE→ACTIVE or ACTIVE→LATCHED, an async
 * Pool event (APP_SIG_FAULT_SET or APP_SIG_FAULT_LATCHED) is posted.
 *
 * Safe to call from ISR context (uses Pool + EvtQ).
 *
 * @param  id   Fault identifier.
 * @param  now  Current tick (ms).
 */
void Fault_Report(FaultId id, uint32_t now);

/**
 * @brief  Manually clear a fault (FSM calls this after confirming recovery).
 *
 * Posts APP_SIG_FAULT_CLR via Pool+EvtQ.
 *
 * @param  id  Fault identifier.
 */
void Fault_Clear(FaultId id);

/**
 * @brief  Check whether a fault is currently active or latched.
 */
bool Fault_IsActive(FaultId id);

/**
 * @brief  Check whether a fault is latched (retries exhausted).
 */
bool Fault_IsLatched(FaultId id);

/**
 * @brief  Return the highest severity among all ACTIVE/LATCHED faults.
 *         Returns FAULT_SEV_INFO if no faults are active.
 */
FaultSeverity Fault_MaxActiveSeverity(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_FAULT_MGR_H */
