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
 * @file  anbo_arch_host.h
 * @brief Test-only helpers for the host arch stub.
 */
#ifndef ANBO_ARCH_HOST_H
#define ANBO_ARCH_HOST_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Set the tick value returned by Anbo_Arch_GetTick(). */
void Anbo_Arch_Host_SetTick(uint32_t tick);

/** Advance tick by delta ms. */
void Anbo_Arch_Host_AdvanceTick(uint32_t delta_ms);

/** Reset critical section counters. */
void Anbo_Arch_Host_ResetCritical(void);

/** Get how many times Critical_Enter was called since last reset. */
uint32_t Anbo_Arch_Host_GetCriticalEnterCount(void);

/** Get how many times Critical_Exit was called since last reset. */
uint32_t Anbo_Arch_Host_GetCriticalExitCount(void);

/** Get current critical section nesting depth. */
int32_t Anbo_Arch_Host_GetCriticalDepth(void);

#ifdef __cplusplus
}
#endif

#endif /* ANBO_ARCH_HOST_H */
