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
 * @file  app_sensor.h
 * @brief Temperature sensor module — periodic ADC sampling via Pool async path
 */

#ifndef APP_SENSOR_H
#define APP_SENSOR_H

#include <stdint.h>

/**
 * @brief Initialise the sensor module.
 *
 * Configures ADC1 channel 17 (internal temperature sensor) and starts
 * a 1-second periodic soft-timer.  Each tick allocates a TempEvent from
 * the Pool, fills it with the ADC reading, and posts it to the async
 * event queue for deferred dispatch in the main loop.
 */
void App_Sensor_Init(void);

/** Stop the sensor periodic timer (for deep sleep). */
void App_Sensor_Stop(void);

/** Resume the sensor periodic timer (after deep sleep). */
void App_Sensor_Resume(void);

/**
 * @brief Quick blocking ADC temperature read (0.1 °C units).
 *
 * Re-initialises ADC from scratch (safe after Stop 2 where ADC
 * registers are lost), reads one sample, and returns.  Does NOT
 * publish events or check faults.
 */
int32_t App_Sensor_QuickRead(void);

/** Return the last normal-operation temperature reading (0.1 °C). */
int32_t App_Sensor_GetLastTemp(void);

#endif /* APP_SENSOR_H */
