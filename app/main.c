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
 * @file  main.c
 * @brief B-L4S5I-IOT01A — Anbo kernel integration demo (Step 6.4)
 *
 * Two cooperative "tasks" demonstrate the full event-driven architecture:
 *
 *   Task A — Button Handler:
 *     Subscribes to ANBO_SIG_USER_BUTTON (PC13 EXTI).
 *     On press: starts a 200 ms one-shot blink timer and logs the event.
 *
 *   Task B — Blink + Log:
 *     Timer callback fires when the one-shot from Task A expires.
 *     Toggles LED2 (PB14) and prints an async log line via USART1 VCP.
 *     If the button is held, the timer auto-restarts on next press.
 *
 * Also running:
 *   - 1-second heartbeat log   (super-loop)
 *   - USART1 RX echo           (event-bus subscriber)
 *   - Software WDT monitor     (feeds IWDG only when healthy)
 *   - Stop 2 low-power idle    (LPTIM1 wakeup, accurate tick)
 *
 * Code structure:
 *   app_init() — all hardware + kernel + module initialisation
 *   app_run()  — infinite super-loop (timers, events, log, WDT, idle)
 *   main()     — calls app_init() then app_run()
 *
 * Memory layout (linker):
 *   .text/.rodata → FLASH
 *   .data/.bss    → SRAM1  (192 KB)
 *   kernel pools  → SRAM2  ( 64 KB, ECC, Standby-retained)
 *   stack 4 KB    → top of SRAM1
 */

#include "anbo_arch.h"
#include "anbo_config.h"
#include "anbo_timer.h"
#include "anbo_ebus.h"
#include "anbo_log.h"
#include "anbo_wdt.h"
#include "b_l4s5i_hw.h"
#include "b_l4s5i_uart_drv.h"
#include "app_config.h"
#if APP_CONF_PARAM_FLASH && (APP_CONF_PARAM_FLASH_USE_EXT == 1)
#include "b_l4s5i_ospi_flash_drv.h"
#endif
#if APP_CONF_LOG_FLASH
#include "b_l4s5i_log_flash.h"
#if APP_CONF_LOG_FLASH_USE_EXT == 1 && !(APP_CONF_PARAM_FLASH && APP_CONF_PARAM_FLASH_USE_EXT == 1)
#include "b_l4s5i_ospi_drv.h"
#endif
#endif

/* Pool async event path + demo modules */
#if ANBO_CONF_POOL
#include "anbo_pool.h"
#include "app_fault_mgr.h"
#include "app_sensor.h"
#include "app_imu.h"
#include "app_controller.h"
#include "app_ui.h"
#include "app_sleep.h"
#endif

#include "stm32l4xx_hal.h"

/* ================================================================== */
/*  Task B — Blink timer fires -> toggle LED2 + async log             */
/* ================================================================== */

static Anbo_Timer s_blink_timer;
static uint32_t   s_press_count;

static void task_b_blink_cb(Anbo_Timer *tmr)
{
    (void)tmr;

    BSP_LED2_Toggle();
    ANBO_LOGI("TaskB: LED2 toggled (press #%u)", s_press_count);
}

/* ================================================================== */
/*  Task A — Button subscriber -> start one-shot blink timer          */
/* ================================================================== */

static Anbo_Subscriber s_btn_sub;

static void task_a_button_cb(const Anbo_Event *evt, void *ctx)
{
    (void)evt;
    (void)ctx;

    ++s_press_count;
    ANBO_LOGI("TaskA: BTN press #%u -> arm 200 ms blink", s_press_count);

    /* (Re)start one-shot timer — Task B will fire in 200 ms */
    Anbo_Timer_Stop(&s_blink_timer);
    Anbo_Timer_Create(&s_blink_timer, ANBO_TIMER_ONESHOT, 200u,
                      task_b_blink_cb, NULL);
    Anbo_Timer_Start(&s_blink_timer);
}

/* ================================================================== */
/*  UART RX subscriber — echo received bytes to log                    */
/* ================================================================== */

static Anbo_Subscriber s_uart_rx_sub;

#if APP_CONF_LOG_FLASH
/* ---- UART command parser FSM ---- */
typedef enum {
    CMD_IDLE = 0,
    CMD_D,          /* got 'd'   */
    CMD_DU,         /* got 'du'  */
    CMD_DUM,        /* got 'dum' */
    CMD_E,          /* got 'e'   */
    CMD_ER,         /* got 'er'  */
    CMD_ERA,        /* got 'era' */
} CmdState;

static CmdState s_cmd_state;
static volatile uint32_t s_dump_req;   /* ISR sets 1, main loop clears */
static volatile uint32_t s_erase_req;  /* ISR sets 1, main loop clears */

static void cmd_push(uint8_t ch)
{
    /* CR/LF resets — not required for matching but keeps things tidy */
    if (ch == '\r' || ch == '\n') {
        s_cmd_state = CMD_IDLE;
        return;
    }

    switch (s_cmd_state) {
    case CMD_IDLE:
        if (ch == 'd')      { s_cmd_state = CMD_D; }
        else if (ch == 'e') { s_cmd_state = CMD_E; }
        break;
    case CMD_D:
        if (ch == 'u')      { s_cmd_state = CMD_DU; }
        else if (ch == 'd') { s_cmd_state = CMD_D; }
        else if (ch == 'e') { s_cmd_state = CMD_E; }
        else                { s_cmd_state = CMD_IDLE; }
        break;
    case CMD_DU:
        if (ch == 'm')      { s_cmd_state = CMD_DUM; }
        else if (ch == 'd') { s_cmd_state = CMD_D; }
        else if (ch == 'e') { s_cmd_state = CMD_E; }
        else                { s_cmd_state = CMD_IDLE; }
        break;
    case CMD_DUM:
        if (ch == 'p')      { s_dump_req = 1u; s_cmd_state = CMD_IDLE; }
        else if (ch == 'd') { s_cmd_state = CMD_D; }
        else if (ch == 'e') { s_cmd_state = CMD_E; }
        else                { s_cmd_state = CMD_IDLE; }
        break;
    case CMD_E:
        if (ch == 'r')      { s_cmd_state = CMD_ER; }
        else if (ch == 'e') { s_cmd_state = CMD_E; }
        else if (ch == 'd') { s_cmd_state = CMD_D; }
        else                { s_cmd_state = CMD_IDLE; }
        break;
    case CMD_ER:
        if (ch == 'a')      { s_cmd_state = CMD_ERA; }
        else if (ch == 'e') { s_cmd_state = CMD_E; }
        else if (ch == 'd') { s_cmd_state = CMD_D; }
        else                { s_cmd_state = CMD_IDLE; }
        break;
    case CMD_ERA:
        if (ch == 's')      { s_erase_req = 1u; s_cmd_state = CMD_IDLE; }
        else if (ch == 'e') { s_cmd_state = CMD_E; }
        else if (ch == 'd') { s_cmd_state = CMD_D; }
        else                { s_cmd_state = CMD_IDLE; }
        break;
    default:
        s_cmd_state = CMD_IDLE;
        break;
    }
}
#endif

static void uart_rx_handler(const Anbo_Event *evt, void *ctx)
{
    (void)ctx;
    Anbo_Device *dev = (Anbo_Device *)evt->param;
    uint8_t buf[16];
    uint32_t n;

    n = Anbo_Dev_Read(dev, buf, sizeof(buf));
    if (n > 0u) {
#if APP_CONF_LOG_FLASH
        for (uint32_t i = 0u; i < n; i++) {
            cmd_push(buf[i]);
        }
#endif
        /* Sanitise for printing: replace non-printable bytes with '.' */
        char printable[17];
        uint32_t pn = (n < sizeof(printable) - 1u) ? n : (sizeof(printable) - 1u);
        for (uint32_t j = 0u; j < pn; j++) {
            printable[j] = (buf[j] >= 0x20 && buf[j] <= 0x7E)
                           ? (char)buf[j] : '.';
        }
        printable[pn] = '\0';
        ANBO_LOGD("RX %u bytes: %s", n, printable);
    }
}

/* ================================================================== */
/*  FPU smoke test                                                     */
/* ================================================================== */

static volatile float s_fpu_result;

static void fpu_smoke_test(void)
{
    float a = 3.14f;
    float b = 2.71f;
    s_fpu_result = a * b + 1.0f;
    ANBO_LOGI("FPU: 3.14*2.71+1.0 = OK (no HardFault)");
}

/* ================================================================== */
/*  Shared state between init / super-loop                             */
/* ================================================================== */

static Anbo_Device *s_uart_dev;
static uint32_t     s_heartbeat_t0;
#if ANBO_CONF_WDT
static Anbo_WDT_Slot s_main_wdt;
#endif

/* ================================================================== */
/*  app_init — all initialisation (steps 1 – 8b)                       */
/* ================================================================== */

static void app_init(void)
{
    /* ---- 1. BSP: clock 120 MHz, FPU, GPIO (LED2, BTN EXTI), USART1 VCP ---- */
    BSP_Init();

    /*
     * Feed IWDG immediately after clock init.
     * STM32 IWDG cannot be stopped once started — if a previous boot
     * enabled it (2 s timeout), it is still counting down right now.
     * Without this early feed, slow init steps (e.g. OSPI timeout ~5 s)
     * would cause an IWDG reset before the main loop gets a chance to
     * call Anbo_WDT_Monitor().
     */
#if ANBO_CONF_WDT
    IWDG->KR = 0xAAAAu;    /* reload counter — safe even before BSP_IWDG_Init() */
#endif

    /* ---- 2. Kernel subsystems ---- */
    Anbo_EBus_Init();
    Anbo_Timer_Init();
#if ANBO_CONF_WDT
    Anbo_WDT_Init();    /* Must precede any WDT_Register calls in module inits */
#endif

    /* ---- 2b. Persistent config (Flash NVM) ---- */
#if APP_CONF_PARAM_FLASH
#if APP_CONF_PARAM_FLASH_USE_EXT == 1
    /*
     * BSP_OSPI_Flash_Init() talks to external NOR Flash (MX25R6435F) via
     * OCTOSPI.  If the chip is absent or the bus is faulty, HAL polling
     * functions block for up to HAL_OSPI_TIMEOUT_DEFAULT_VALUE (5 000 ms).
     *
     * Feed IWDG before entry — the previous boot may have started IWDG
     * with a 2 s timeout, and that timer is still running.  5 s of
     * blocking would trigger a watchdog reset without this feed.
     */
#if ANBO_CONF_WDT
    IWDG->KR = 0xAAAAu;
#endif
    if (!BSP_OSPI_Flash_Init()) {
        ANBO_LOGI("OSPI Flash init FAILED");
    }
#endif
    App_Config_Init();
#endif

    /* ---- 3. USART1 device driver (ISR-driven, ST-LINK VCP) ---- */
    s_uart_dev = BSP_USART1_GetDevice();
    Anbo_Dev_Open(s_uart_dev);
    Anbo_Log_Init(s_uart_dev);

    /* ---- 3b. Flash log sink (persistent log to Flash) ---- */
#if APP_CONF_LOG_FLASH
#if APP_CONF_LOG_FLASH_USE_EXT == 1 && !(APP_CONF_PARAM_FLASH && APP_CONF_PARAM_FLASH_USE_EXT == 1)
    /*
     * OSPI bus not yet initialised by param NVM — init it now for log sink.
     *
     * Same IWDG hazard as above: BSP_OSPI_Init() may block up to 5 s
     * if the external Flash chip is unpopulated or bus is misconfigured.
     * Feed IWDG first so the watchdog doesn't reset us during the wait.
     */
#if ANBO_CONF_WDT
    IWDG->KR = 0xAAAAu;
#endif
    if (!BSP_OSPI_Init()) {
        ANBO_LOGI("OSPI init for log FAILED");
    }
#endif
    BSP_LogFlash_Init();
    Anbo_Log_SetFlashWriter(BSP_LogFlash_Write);
    /* Route ERROR and WARN to both UART + Flash */
    Anbo_Log_SetSink(ANBO_LOG_LVL_ERROR, ANBO_LOG_SINK_UART | ANBO_LOG_SINK_FLASH);
    Anbo_Log_SetSink(ANBO_LOG_LVL_WARN,  ANBO_LOG_SINK_UART | ANBO_LOG_SINK_FLASH);
#endif

    /* ---- First message: reset reason ---- */
    Anbo_Log_Flush();
    {
        uint32_t csr;
        const char *reason = BSP_GetResetReason(&csr);
        ANBO_LOGW("[REBOOT] reason=%s CSR=0x%08x", reason, csr);
        Anbo_Log_Flush();
    }

    /* ---- 3b. Pool + Async event queue ---- */
#if ANBO_CONF_WDT
    IWDG->KR = 0xAAAAu;    /* feed: Flash/log init complete, module init next */
#endif
#if ANBO_CONF_POOL
    Anbo_Pool_Init();
    Anbo_EvtQ_Init();
    Fault_Manager_Init();
#endif

    /* ---- 4. Event subscriptions ---- */
    /* Task A: button -> timer */
    Anbo_EBus_Subscribe(&s_btn_sub, ANBO_SIG_USER_BUTTON,
                        task_a_button_cb, NULL);
    /* UART RX echo */
    Anbo_EBus_Subscribe(&s_uart_rx_sub, ANBO_SIG_UART_RX,
                        uart_rx_handler, NULL);

    /* ---- 4b. Demo modules (Pool async path) ---- */
#if ANBO_CONF_POOL
    App_Controller_Init();
    App_UI_Init();
    App_Sensor_Init();
    App_IMU_Init();
#endif
    Anbo_Log_DrainAll();

    /* ---- 5. Boot banner ---- */
#if ANBO_CONF_WDT
    IWDG->KR = 0xAAAAu;    /* feed: all modules initialised, banner + peripherals next */
#endif
    ANBO_LOGI("======================================");
    ANBO_LOGI("  Anbo v1.0 on B-L4S5I-IOT01A");
    ANBO_LOGI("  SYSCLK  = %u Hz", SystemCoreClock);
    ANBO_LOGI("  Pools   -> SRAM2 (ECC, retained)");
    ANBO_LOGI("  Stack   = 32 KB (top of SRAM1)");
    ANBO_LOGI("  USART1  = ISR TX/RX  115200 VCP");
    ANBO_LOGI("  IMU     = LSM6DSL I2C2 @0x6A");
#if ANBO_CONF_POOL
    ANBO_LOGI("  Pool    = %u x %uB async events",
              ANBO_CONF_POOL_BLOCK_COUNT, ANBO_CONF_POOL_BLOCK_SIZE);
#endif
    ANBO_LOGI("  Idle    = Stop 2 + LPTIM1 wakeup");
    ANBO_LOGI("======================================");
    Anbo_Log_Flush();

    /* ---- 6. FPU ---- */
    fpu_smoke_test();
    Anbo_Log_Flush();

    /* ---- 7. LPTIM1 (Stop 2 wakeup) + RTC wakeup timer ---- */
#if ANBO_CONF_WDT
    IWDG->KR = 0xAAAAu;    /* feed: RTC init may block ~1 s if DBP was not set */
#endif
    BSP_LPTIM_Init();
    BSP_RTC_Init();
    ANBO_LOGI("LPTIM1 + RTC ready");
    Anbo_Log_Flush();

    /* ---- 8. Software WDT + IWDG ---- */
#if ANBO_CONF_WDT
    /* Anbo_WDT_Init() already called at step 2 — ctrl & sensor WDT slots
     * registered during module inits (step 4b) are preserved. */
    s_main_wdt = Anbo_WDT_Register("main", 1500u);
    BSP_IWDG_Init(2000u);
    ANBO_LOGI("IWDG 2 s, SW-WDT slot=%d", (int)s_main_wdt);
    Anbo_Log_Flush();
#endif

    /* ---- 8b. Deep-sleep module (long-press 3 s) ---- */
#if ANBO_CONF_POOL
    App_Sleep_Init(
#if ANBO_CONF_WDT
        (int)s_main_wdt
#else
        -1
#endif
    );
    App_Sleep_SetTimeout(600);  /* 10 min auto-wake from deep sleep */
#endif

    ANBO_LOGI("Entering main loop t=%u", Anbo_Arch_GetTick());
#if ANBO_CONF_WDT
    ANBO_LOGI("WDT slots: main=%d ctrl=%d sensor=%d",
              (int)s_main_wdt, -1, -1);  /* ctrl/sensor registered in their inits */
#endif
    Anbo_Log_Flush();

    s_heartbeat_t0 = Anbo_Arch_GetTick();
}

/* ================================================================== */
/*  app_run — infinite super-loop                                      */
/* ================================================================== */

static void app_run(void)
{
    for (;;) {
        uint32_t now = Anbo_Arch_GetTick();

        /* Drive soft timers (Task B fires from here) */
        Anbo_Timer_Update(now);

        /* Drain Pool async event queue */
#if ANBO_CONF_POOL
        {
            Anbo_PoolEvent *pevt;
            while (Anbo_EvtQ_Get(&pevt) == 0) {
                Anbo_Pool_Dispatch(pevt);
            }
        }
#endif

        /* Drain async log to USART1 VCP */
        Anbo_Log_Flush();

        /* Handle deferred "dump" command from UART RX ISR */
#if APP_CONF_LOG_FLASH
        if (s_dump_req) {
            s_dump_req = 0u;
            Anbo_Log_Flush();
            BSP_LogFlash_Dump();
        }
        if (s_erase_req) {
            s_erase_req = 0u;
            Anbo_Log_Flush();
            if (BSP_LogFlash_Erase()) {
                ANBO_LOGI("LogFlash: erased OK");
            } else {
                ANBO_LOGE("LogFlash: erase FAILED");
            }
        }
#endif

        /* 1-second heartbeat */
        if ((now - s_heartbeat_t0) >= 1000u) {
            s_heartbeat_t0 = now;
            ANBO_LOGI("tick=%u btn=%u", now, s_press_count);
        }

        /* Software watchdog */
#if ANBO_CONF_WDT
        Anbo_WDT_Checkin(s_main_wdt);
        if (Anbo_WDT_Monitor(now) == 0) {
            const char *who = Anbo_WDT_FirstTimeout(now);
            ANBO_LOGW("WDT TIMEOUT: slot=%s tick=%u", who ? who : "?", now);
            /* Feed IWDG once so the log has time to flush before reset */
            IWDG->KR = 0xAAAAu;
            Anbo_Log_Flush();
        }
#endif

        /* Deep-sleep check (blocks if long-press confirmed) */
#if ANBO_CONF_POOL
        App_Sleep_Poll();
#endif

        /* Low-power idle (Stop 2 via LPTIM1) */
#if ANBO_CONF_IDLE_SLEEP
        {
            uint32_t ms = Anbo_Timer_MsToNext(now);
            if (ms > 0u) {
                /* Only enter Stop 2 when UART TX is idle.
                 * Stop 2 kills USART clocks — bytes in transit would be lost.
                 * If TX is still busy, fall back to WFI (clocks keep running). */
                if ((s_uart_dev->flags & ANBO_DEV_FLAG_TX_BUSY) ||
                    Anbo_RB_Count(s_uart_dev->tx_rb) > 0u) {
                    __WFI();
                } else {
                    Anbo_Arch_Idle(ms);
                }
            }
        }
#endif
    }
}

/* ================================================================== */
/*  Main                                                               */
/* ================================================================== */

int main(void)
{
    app_init();
    app_run();
    return 0;
}
