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
 * @file  app_imu.c
 * @brief IMU module — interrupt-driven LSM6DSL FIFO readout
 *
 * Data flow (no polling timer):
 *   LSM6DSL FIFO watermark → INT1 (PD11) EXTI → ANBO_SIG_IMU_INT1
 *   → EBus subscriber (main-loop context)
 *   → BSP_IMU_FIFO_Read burst → cache latest
 *   → Pool_Alloc(ImuEvent) → EvtQ_Post → EBus broadcast APP_SIG_IMU_UPDATE
 */

#include "anbo_config.h"

#if ANBO_CONF_POOL

#include "app_imu.h"
#include "app_signals.h"
#include "anbo_ebus.h"
#include "anbo_pool.h"
#include "anbo_log.h"
#include "anbo_arch.h"

#include "app_fault_mgr.h"
#include "b_l4s5i_i2c_drv.h"
#include "b_l4s5i_imu_drv.h"
#include "b_l4s5i_uart_drv.h"    /* ANBO_SIG_IMU_INT1 */

/* ================================================================== */
/*  Configuration                                                      */
/* ================================================================== */

/**
 * FIFO watermark in sample-sets (accel + gyro).
 * At 104 Hz ODR, 10 sets ≈ 96 ms between interrupts.
 */
#define IMU_FIFO_WTM_SETS       10u

/** Max sets to read in one FIFO burst */
#define IMU_FIFO_MAX_READ       32u

/** Consecutive failure count before fault report */
#define IMU_FAIL_THRESHOLD      5u

/**
 * Vibration / shake detection — squared-magnitude thresholds (integer-only).
 * Gravity ≈ 1000 mg.  If |accel| deviates by > VIBRATION_MG, report.
 *   low²  = (1000 − 50)² =  902 500
 *   high² = (1000 + 50)² = 1 102 500
 */
#define VIBRATION_MG            50
#define GRAVITY_MG              1000
#define VIB_LOW_SQ   ((int32_t)(GRAVITY_MG - VIBRATION_MG) * (GRAVITY_MG - VIBRATION_MG))
#define VIB_HIGH_SQ  ((int32_t)(GRAVITY_MG + VIBRATION_MG) * (GRAVITY_MG + VIBRATION_MG))

/* ================================================================== */
/*  Derived Pool Event: IMU reading                                    */
/* ================================================================== */

typedef struct {
    Anbo_PoolEvent super;       /* base class — must be first */
    int32_t ax;                 /* accel X [mg]   */
    int32_t ay;                 /* accel Y [mg]   */
    int32_t az;                 /* accel Z [mg]   */
    int32_t gx;                 /* gyro X [mdps]  */
    int32_t gy;                 /* gyro Y [mdps]  */
    int32_t gz;                 /* gyro Z [mdps]  */
} ImuEvent;

/* ================================================================== */
/*  State                                                              */
/* ================================================================== */

static uint8_t      s_fail_count;
static App_IMU_Data s_last;
static bool         s_active;

void App_IMU_GetLast(App_IMU_Data *out)
{
    if (out != NULL) {
        *out = s_last;
    }
}

/* ================================================================== */
/*  Recovery probe                                                     */
/* ================================================================== */

static bool imu_recover_check(FaultId id, void *ctx)
{
    (void)id; (void)ctx;
    return BSP_I2C2_IsReady(LSM6DSL_I2C_ADDR);
}

/* ================================================================== */
/*  EBus handler: INT1 watermark reached → read FIFO                   */
/* ================================================================== */

static void imu_int1_handler(const Anbo_Event *evt, void *ctx)
{
    (void)evt; (void)ctx;

    if (!s_active) {
        return;
    }

    LSM6DSL_Sample buf[IMU_FIFO_MAX_READ];
    uint16_t n = BSP_IMU_FIFO_Read(buf, IMU_FIFO_MAX_READ);

    if (n == 0u) {
        s_fail_count++;
        if (s_fail_count >= IMU_FAIL_THRESHOLD) {
            Fault_Report(FAULT_IMU_COMM, Anbo_Arch_GetTick());
            s_fail_count = 0u;
        }
        return;
    }

    /* Reset failure counter on any successful read */
    s_fail_count = 0u;

    /* Cache the newest sample (last in the burst) */
    const LSM6DSL_Sample *newest = &buf[n - 1u];
    s_last.ax = newest->accel.x;
    s_last.ay = newest->accel.y;
    s_last.az = newest->accel.z;
    s_last.gx = newest->gyro.x;
    s_last.gy = newest->gyro.y;
    s_last.gz = newest->gyro.z;

    /* ---- Vibration detection (integer-only, no sqrt) ---- */
    int32_t sum_sq = (s_last.ax * s_last.ax)
                   + (s_last.ay * s_last.ay)
                   + (s_last.az * s_last.az);

    if (sum_sq < VIB_LOW_SQ || sum_sq > VIB_HIGH_SQ) {
        Anbo_Log_Printf(ANBO_LOG_LVL_INFO, "\r\n");
        ANBO_LOGI("IMU: vibration! ax=%d ay=%d az=%d mg",
                  (int)s_last.ax, (int)s_last.ay, (int)s_last.az);

        /* Publish IMU reading only on vibration */
        ImuEvent *pe = (ImuEvent *)Anbo_Pool_Alloc();
        if (pe != NULL) {
            pe->super.sig = APP_SIG_IMU_UPDATE;
            pe->ax = s_last.ax;
            pe->ay = s_last.ay;
            pe->az = s_last.az;
            pe->gx = s_last.gx;
            pe->gy = s_last.gy;
            pe->gz = s_last.gz;

            if (Anbo_EvtQ_Post(&pe->super) != 0) {
                Anbo_Pool_Free(pe);
            }
        }
    }
}

/* ================================================================== */
/*  EBus subscriber node                                               */
/* ================================================================== */

static Anbo_Subscriber s_imu_sub;

/* ================================================================== */
/*  Public API                                                         */
/* ================================================================== */

void App_IMU_Init(void)
{
    /* Initialise I2C2 bus */
    if (!BSP_I2C2_Init()) {
        ANBO_LOGE("IMU: I2C2 init fail");
        return;
    }

    /* Initialise LSM6DSL with FIFO + INT1 */
    if (!BSP_IMU_Init(LSM6DSL_ODR_104HZ, LSM6DSL_XL_FS_4G,
                      LSM6DSL_ODR_104HZ, LSM6DSL_GY_FS_500DPS,
                      IMU_FIFO_WTM_SETS)) {
        ANBO_LOGE("IMU: LSM6DSL init fail");
        return;
    }

    /* Configure PD11 EXTI for INT1 */
    BSP_IMU_INT1_Init();

    /* Register fault */
    Fault_Register(FAULT_IMU_COMM, "ImuComm",
                   FAULT_SEV_WARNING, 3, 1000u,
                   imu_recover_check, NULL);

    /* Subscribe to INT1 signal */
    Anbo_EBus_Subscribe(&s_imu_sub, ANBO_SIG_IMU_INT1,
                        imu_int1_handler, NULL);

    s_fail_count = 0u;
    s_active = true;

    ANBO_LOGI("IMU: FIFO wtm=%u sets, INT1->EXTI, 104Hz +/-4g +/-500dps",
              (unsigned)IMU_FIFO_WTM_SETS);
}

void App_IMU_Stop(void)
{
    s_active = false;
    Anbo_EBus_Unsubscribe(&s_imu_sub);
    BSP_IMU_PowerDown();
}

void App_IMU_SleepArm(void)
{
    s_active = false;
    Anbo_EBus_Unsubscribe(&s_imu_sub);
    /* Low-power accel 12.5 Hz, wake-up on ~62 mg (thresh=1) → INT1 → EXTI */
    BSP_IMU_ConfigWakeup(1u);
}

void App_IMU_Resume(void)
{
    /* Re-init LSM6DSL + FIFO + INT1 after deep sleep */
    BSP_IMU_Init(LSM6DSL_ODR_104HZ, LSM6DSL_XL_FS_4G,
                 LSM6DSL_ODR_104HZ, LSM6DSL_GY_FS_500DPS,
                 IMU_FIFO_WTM_SETS);

    BSP_IMU_INT1_Init();

    Anbo_EBus_Subscribe(&s_imu_sub, ANBO_SIG_IMU_INT1,
                        imu_int1_handler, NULL);
    s_active = true;
}

#endif /* ANBO_CONF_POOL */
