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
 * @file  test_anbo_rb.c
 * @brief Unit tests for Anbo_RB (ring buffer) module.
 */

#include "unity.h"
#include "anbo_rb.h"

/* ---- Test fixture ---- */
#define BUF_ORDER  4                /* 2^4 = 16 bytes */
#define BUF_SIZE   (1u << BUF_ORDER)

static uint8_t  s_storage[BUF_SIZE];
static Anbo_RB  s_rb;

void setUp(void)
{
    TEST_ASSERT_EQUAL_INT(0, Anbo_RB_Init(&s_rb, s_storage, BUF_SIZE));
}

void tearDown(void) { }

/* ================================================================ */
/*  Init / Reset                                                     */
/* ================================================================ */

void test_init_valid(void)
{
    TEST_ASSERT_EQUAL_UINT32(BUF_SIZE, s_rb.size);
    TEST_ASSERT_EQUAL_UINT32(BUF_SIZE - 1, s_rb.mask);
    TEST_ASSERT_EQUAL_UINT32(0u, s_rb.head);
    TEST_ASSERT_EQUAL_UINT32(0u, s_rb.tail);
    TEST_ASSERT_TRUE(Anbo_RB_IsEmpty(&s_rb));
}

void test_init_not_power_of_two(void)
{
    uint8_t tmp[12];
    Anbo_RB rb2;
    TEST_ASSERT_EQUAL_INT(-1, Anbo_RB_Init(&rb2, tmp, 12));
}

void test_init_null_ptr(void)
{
    Anbo_RB rb2;
    TEST_ASSERT_EQUAL_INT(-1, Anbo_RB_Init(NULL, s_storage, BUF_SIZE));
    TEST_ASSERT_EQUAL_INT(-1, Anbo_RB_Init(&rb2, NULL, BUF_SIZE));
}

void test_reset(void)
{
    Anbo_RB_PutByte(&s_rb, 0xAA);
    Anbo_RB_PutByte(&s_rb, 0xBB);
    Anbo_RB_Reset(&s_rb);
    TEST_ASSERT_TRUE(Anbo_RB_IsEmpty(&s_rb));
    TEST_ASSERT_EQUAL_UINT32(BUF_SIZE, Anbo_RB_Free(&s_rb));
}

/* ================================================================ */
/*  Single-byte Put / Get                                            */
/* ================================================================ */

void test_put_get_single(void)
{
    uint8_t out = 0;
    TEST_ASSERT_EQUAL_INT(0, Anbo_RB_PutByte(&s_rb, 0x42));
    TEST_ASSERT_EQUAL_UINT32(1u, Anbo_RB_Count(&s_rb));
    TEST_ASSERT_EQUAL_INT(0, Anbo_RB_GetByte(&s_rb, &out));
    TEST_ASSERT_EQUAL_HEX8(0x42, out);
    TEST_ASSERT_TRUE(Anbo_RB_IsEmpty(&s_rb));
}

void test_get_empty_returns_error(void)
{
    uint8_t out;
    TEST_ASSERT_EQUAL_INT(-1, Anbo_RB_GetByte(&s_rb, &out));
}

void test_fill_then_put_returns_error(void)
{
    uint32_t i;
    for (i = 0; i < BUF_SIZE; i++) {
        TEST_ASSERT_EQUAL_INT(0, Anbo_RB_PutByte(&s_rb, (uint8_t)i));
    }
    TEST_ASSERT_TRUE(Anbo_RB_IsFull(&s_rb));
    TEST_ASSERT_EQUAL_INT(-1, Anbo_RB_PutByte(&s_rb, 0xFF));
}

/* ================================================================ */
/*  Bulk Write / Read                                                */
/* ================================================================ */

void test_write_read_bulk(void)
{
    const uint8_t tx[] = { 1, 2, 3, 4, 5 };
    uint8_t rx[5] = { 0 };

    TEST_ASSERT_EQUAL_UINT32(5, Anbo_RB_Write(&s_rb, tx, 5));
    TEST_ASSERT_EQUAL_UINT32(5, Anbo_RB_Count(&s_rb));
    TEST_ASSERT_EQUAL_UINT32(5, Anbo_RB_Read(&s_rb, rx, 5));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(tx, rx, 5);
}

void test_write_overflow_clips(void)
{
    uint8_t big[20];
    uint32_t i;
    for (i = 0; i < sizeof(big); i++) big[i] = (uint8_t)i;

    uint32_t written = Anbo_RB_Write(&s_rb, big, sizeof(big));
    TEST_ASSERT_EQUAL_UINT32(BUF_SIZE, written);
    TEST_ASSERT_TRUE(Anbo_RB_IsFull(&s_rb));
}

/* ================================================================ */
/*  Wrap-around                                                      */
/* ================================================================ */

void test_wraparound(void)
{
    uint8_t out;
    uint32_t i;

    /* Fill half, drain half, then fill again to force wrap */
    for (i = 0; i < BUF_SIZE / 2; i++) {
        Anbo_RB_PutByte(&s_rb, (uint8_t)(i + 0x10));
    }
    for (i = 0; i < BUF_SIZE / 2; i++) {
        Anbo_RB_GetByte(&s_rb, &out);
    }
    TEST_ASSERT_TRUE(Anbo_RB_IsEmpty(&s_rb));

    /* Now write full capacity — this wraps around the internal array */
    for (i = 0; i < BUF_SIZE; i++) {
        TEST_ASSERT_EQUAL_INT(0, Anbo_RB_PutByte(&s_rb, (uint8_t)(i + 0xA0)));
    }
    TEST_ASSERT_TRUE(Anbo_RB_IsFull(&s_rb));

    for (i = 0; i < BUF_SIZE; i++) {
        TEST_ASSERT_EQUAL_INT(0, Anbo_RB_GetByte(&s_rb, &out));
        TEST_ASSERT_EQUAL_HEX8((uint8_t)(i + 0xA0), out);
    }
    TEST_ASSERT_TRUE(Anbo_RB_IsEmpty(&s_rb));
}

/* ================================================================ */
/*  Count / Free consistency                                         */
/* ================================================================ */

void test_count_free_consistency(void)
{
    uint32_t i;
    for (i = 0; i <= BUF_SIZE; i++) {
        TEST_ASSERT_EQUAL_UINT32(BUF_SIZE, Anbo_RB_Count(&s_rb) + Anbo_RB_Free(&s_rb));
        if (i < BUF_SIZE) {
            Anbo_RB_PutByte(&s_rb, (uint8_t)i);
        }
    }
}
