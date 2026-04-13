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
 * @file  anbo_arch_host.c
 * @brief Stub implementations of anbo_arch.h for host-based unit testing.
 *
 * Provides no-op / simple implementations so kernel modules can be
 * compiled and tested with a native host compiler (gcc/clang).
 */

#include "anbo_arch.h"
#include "anbo_arch_host.h"

/* ---- Critical section tracking ---- */
static uint32_t s_crit_enter_count = 0u;
static uint32_t s_crit_exit_count  = 0u;
static int32_t  s_crit_depth       = 0;

void Anbo_Arch_Critical_Enter(void)
{
    s_crit_enter_count++;
    s_crit_depth++;
}

void Anbo_Arch_Critical_Exit(void)
{
    s_crit_exit_count++;
    s_crit_depth--;
}

void Anbo_Arch_Host_ResetCritical(void)
{
    s_crit_enter_count = 0u;
    s_crit_exit_count  = 0u;
    s_crit_depth       = 0;
}

uint32_t Anbo_Arch_Host_GetCriticalEnterCount(void) { return s_crit_enter_count; }
uint32_t Anbo_Arch_Host_GetCriticalExitCount(void)  { return s_crit_exit_count; }
int32_t  Anbo_Arch_Host_GetCriticalDepth(void)      { return s_crit_depth; }

/* ---- Monotonic tick ---- */
static uint32_t s_host_tick = 0u;

uint32_t Anbo_Arch_GetTick(void)
{
    return s_host_tick;
}

void Anbo_Arch_Host_SetTick(uint32_t tick)
{
    s_host_tick = tick;
}

void Anbo_Arch_Host_AdvanceTick(uint32_t delta_ms)
{
    s_host_tick += delta_ms;
}

/* ---- UART stub ---- */
void Anbo_Arch_UART_PutChar(char c) { (void)c; }

int Anbo_Arch_UART_Transmit_DMA(const uint8_t *buf, uint32_t len)
{
    (void)buf; (void)len;
    return 0;
}

/* ---- Idle stub ---- */
#if ANBO_CONF_IDLE_SLEEP
void Anbo_Arch_Idle(uint32_t ms) { (void)ms; }
#endif
