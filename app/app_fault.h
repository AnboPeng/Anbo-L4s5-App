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
 * @file  app_fault.h
 * @brief Business-level fault definitions (IDs, severity, state)
 *
 * This header defines the "vocabulary" of faults in the application.
 * The actual management logic lives in app_fault_mgr.h/.c.
 *
 * Design:
 *   - Fault IDs are grouped by subsystem (0x00-0x0F sensor, 0x10-0x1F power, ...)
 *   - Severity levels map to FSM actions via a policy table in the controller.
 *   - FaultEntry tracks lifecycle: INACTIVE → ACTIVE → LATCHED / CLEARED.
 */

#ifndef APP_FAULT_H
#define APP_FAULT_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================== */
/*  Fault IDs (grouped by subsystem)                                   */
/* ================================================================== */

typedef enum {
    /* Sensor subsystem 0x00-0x0F */
    FAULT_SENSOR_RANGE   = 0x00,    /**< ADC reading out of physical range */
    FAULT_SENSOR_TIMEOUT = 0x01,    /**< No data for extended period */

    /* IMU subsystem 0x08-0x0F */
    FAULT_IMU_COMM       = 0x08,    /**< I2C communication failure with LSM6DSL */

    /* Power subsystem 0x10-0x1F */
    FAULT_POWER_UNDERVOLT = 0x10,

    FAULT_ID_COUNT                  /**< Sentinel — not a real fault */
} FaultId;

/* ================================================================== */
/*  Fault lifecycle state                                              */
/* ================================================================== */

typedef enum {
    FAULT_STATE_INACTIVE = 0,   /**< No fault present */
    FAULT_STATE_ACTIVE   = 1,   /**< Fault detected, retries in progress */
    FAULT_STATE_LATCHED  = 2,   /**< Retries exhausted, locked until manual clear */
} FaultState;

/* ================================================================== */
/*  Fault severity                                                     */
/* ================================================================== */

typedef enum {
    FAULT_SEV_INFO    = 0,  /**< Log only, no action */
    FAULT_SEV_WARNING = 1,  /**< Degrade (e.g. extend sample interval) */
    FAULT_SEV_ERROR   = 2,  /**< Enter fault state */
    FAULT_SEV_FATAL   = 3,  /**< Emergency — stop immediately */
} FaultSeverity;

/* ================================================================== */
/*  Recovery check callback                                            */
/* ================================================================== */

/**
 * @typedef FaultRecoverCheck
 * @brief   Called periodically by FaultManager to probe whether the
 *          fault condition has resolved.
 * @retval  true   Condition cleared — fault may be auto-cleared.
 * @retval  false  Condition persists.
 */
typedef bool (*FaultRecoverCheck)(FaultId id, void *ctx);

/* ================================================================== */
/*  Fault table entry                                                  */
/* ================================================================== */

typedef struct {
    FaultId           id;               /**< Fault identifier */
    FaultState        state;            /**< Current lifecycle state */
    FaultSeverity     severity;         /**< Severity level */
    const char       *name;             /**< Human-readable name (debug) */

    uint8_t           retry_count;      /**< Current retry counter */
    uint8_t           retry_max;        /**< Max retries before LATCHED (0 = immediate) */
    uint32_t          retry_interval_ms;/**< Interval between retries */

    uint32_t          first_ts;         /**< Timestamp of first occurrence (ms) */
    uint32_t          last_ts;          /**< Timestamp of most recent Report() */
    uint32_t          occurrence_count; /**< Lifetime occurrence counter */

    FaultRecoverCheck recover_check;    /**< Recovery probe callback (may be NULL) */
    void             *recover_ctx;      /**< Context passed to recover_check */
} FaultEntry;

#ifdef __cplusplus
}
#endif

#endif /* APP_FAULT_H */
