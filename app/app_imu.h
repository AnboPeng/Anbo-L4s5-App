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
 * @file  app_imu.h
 * @brief IMU module — interrupt-driven FIFO readout via LSM6DSL INT1
 *
 * Data flow:
 *   LSM6DSL samples at ODR → FIFO accumulates accel+gyro pairs
 *   → FIFO level ≥ watermark → INT1 (PD11 EXTI) fires
 *   → ISR publishes ANBO_SIG_IMU_INT1 on event bus
 *   → main-loop handler reads FIFO burst via I2C
 *   → publishes latest reading as APP_SIG_IMU_UPDATE (Pool event)
 */

#ifndef APP_IMU_H
#define APP_IMU_H

#include <stdint.h>

/**
 * @brief Initialise the IMU module.
 *
 * Initialises I2C2 bus, configures LSM6DSL (104 Hz, ±4 g, ±500 dps)
 * with FIFO continuous mode (watermark = 10 sample-sets), enables
 * INT1 on PD11, subscribes to ANBO_SIG_IMU_INT1, and registers faults.
 */
void App_IMU_Init(void);

/** Stop the IMU periodic timer (for deep sleep). */
void App_IMU_Stop(void);

/**
 * @brief  Arm IMU low-power wake-up mode for deep sleep.
 *
 * Puts LSM6DSL into accel-only 12.5 Hz mode with wake-up interrupt
 * on INT1 (PD11 EXTI).  Any vibration above ~62 mg will pulse INT1,
 * allowing STM32 to wake from Stop 2.
 */
void App_IMU_SleepArm(void);

/** Resume the IMU periodic timer (after deep sleep). */
void App_IMU_Resume(void);

/**
 * @struct App_IMU_Data
 * @brief  Latest IMU reading snapshot (accel + gyro).
 *
 * Accelerometer values in milli-g, gyroscope in milli-degrees-per-second.
 */
typedef struct {
    int32_t ax;     /**< Accel X [mg] */
    int32_t ay;     /**< Accel Y [mg] */
    int32_t az;     /**< Accel Z [mg] */
    int32_t gx;     /**< Gyro X [mdps] */
    int32_t gy;     /**< Gyro Y [mdps] */
    int32_t gz;     /**< Gyro Z [mdps] */
} App_IMU_Data;

/**
 * @brief  Get the last cached IMU reading.
 * @param  out  Receives the latest accel + gyro values.
 */
void App_IMU_GetLast(App_IMU_Data *out);

#endif /* APP_IMU_H */
