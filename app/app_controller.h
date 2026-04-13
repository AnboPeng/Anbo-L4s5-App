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
 * @file  app_controller.h
 * @brief Temperature alarm controller FSM
 *
 * Subscribes to APP_SIG_TEMP_UPDATE and APP_SIG_THRESHOLD_SET via
 * Anbo_FSM (EBus auto-routing).  Publishes APP_SIG_ALARM_STATE.
 *
 * Two states: Normal / Alarm.
 */

#ifndef APP_CONTROLLER_H
#define APP_CONTROLLER_H

/**
 * @brief Initialise the controller FSM and subscribe to temperature /
 *        threshold signals.
 */
void App_Controller_Init(void);

/** Suspend controller WDT slot (for deep sleep). */
void App_Controller_Stop(void);

/** Resume controller WDT slot (after deep sleep). */
void App_Controller_Resume(void);

#endif /* APP_CONTROLLER_H */
