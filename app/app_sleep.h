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
 * @file  app_sleep.h
 * @brief System deep-sleep mode — long-press 3 s to enter, EXTI or timeout to exit
 *
 * Deep sleep differs from the normal tickless idle:
 *   - Tickless idle: automatic, ~1 s between timer events, transparent
 *   - Deep sleep:    user-triggered, indefinite, all app activity stopped
 *
 * Entry:  Long-press USER button for 3 seconds.
 * Exit:   Press USER button again, or optional auto-wake timeout expires.
 *
 * During deep sleep the IWDG is kept alive by periodic LPTIM1 wake-ups
 * (every 1.5 s) that feed the watchdog and immediately re-enter Stop 2.
 */

#ifndef APP_SLEEP_H
#define APP_SLEEP_H

#include <stdint.h>

/**
 * @brief Initialise the deep-sleep module.
 *
 * Must be called after EBus, Timer and WDT are initialised.
 * Subscribes to ANBO_SIG_USER_BUTTON for long-press detection.
 *
 * @param wdt_slot  The main-loop software WDT slot handle (for suspend/resume).
 *                  Pass < 0 if WDT is not used.
 */
void App_Sleep_Init(int wdt_slot);

/**
 * @brief Set the auto-wake timeout (0 = wake only on button press).
 * @param seconds  Maximum sleep duration in seconds (0 = infinite).
 *
 * Can be called at any time; takes effect on the next sleep entry.
 */
void App_Sleep_SetTimeout(uint32_t seconds);

/**
 * @brief Poll function — call once per super-loop iteration.
 *
 * When the long-press is confirmed this function does NOT return until
 * the system wakes up (button press or timeout).  It blocks inside a
 * Stop 2 loop, periodically feeding the IWDG.
 */
void App_Sleep_Poll(void);

#endif /* APP_SLEEP_H */
