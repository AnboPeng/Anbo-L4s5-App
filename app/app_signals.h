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
 * @file  app_signals.h
 * @brief Application-level signal definitions (business events)
 *
 * Driver-level signals (UART_RX, USER_BUTTON etc.) live in port headers.
 * Business signals live here — they are the "common language" between
 * decoupled app modules.
 */

#ifndef APP_SIGNALS_H
#define APP_SIGNALS_H

/* ---- Business signals (0x0100+) ---- */

/** Temperature reading updated.  PoolEvent payload: TempEvent */
#define APP_SIG_TEMP_UPDATE     0x0100u

/** Alarm threshold changed.     PoolEvent payload: ThresholdEvent */
#define APP_SIG_THRESHOLD_SET   0x0101u

/** Alarm state toggled.         param: 0 = normal, 1 = alarm */
#define APP_SIG_ALARM_STATE     0x0102u

/* ---- IMU domain (0x0110+) ---- */

/** IMU reading updated.  PoolEvent payload: ImuEvent (accel + gyro) */
#define APP_SIG_IMU_UPDATE      0x0110u

/* ---- Fault Manager signals (0x0200+) ---- */

/** A fault has become ACTIVE.   PoolEvent payload: FaultEvent */
#define APP_SIG_FAULT_SET       0x0200u

/** A fault has been cleared.    PoolEvent payload: FaultEvent */
#define APP_SIG_FAULT_CLR       0x0201u

/** A fault is LATCHED (retries exhausted). PoolEvent payload: FaultEvent */
#define APP_SIG_FAULT_LATCHED   0x0202u

#endif /* APP_SIGNALS_H */
