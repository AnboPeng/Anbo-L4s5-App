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
 * @file  app_config.h
 * @brief Persistent system configuration (NVM-backed)
 *
 * The SystemConfig structure is stored in internal Flash via the
 * log-structured NVM driver.  It is 8-byte aligned to match STM32
 * double-word programming granularity.
 *
 * Configuration includes a magic number, a version field for OTA
 * compatibility, application parameters, and a software CRC32 for
 * data integrity verification.
 */

#ifndef APP_CONFIG_H
#define APP_CONFIG_H

/* ------------------------------------------------------------------ */
/*  App-level NVM feature switches (not part of kernel)                */
/* ------------------------------------------------------------------ */

/**
 * @def   APP_CONF_PARAM_FLASH
 * @brief Persistent config storage switch.
 *        1 = enable Flash-backed App_Config.
 *        0 = no persistent storage.
 */
#ifndef APP_CONF_PARAM_FLASH
#define APP_CONF_PARAM_FLASH                1
#endif

/**
 * @def   APP_CONF_PARAM_FLASH_USE_EXT
 * @brief NVM storage backend selection (only when APP_CONF_PARAM_FLASH == 1).
 *        0 = Internal Flash (default, no extra HW needed)
 *        1 = External OSPI NOR Flash (MX25R6435F on OCTOSPI1)
 */
#ifndef APP_CONF_PARAM_FLASH_USE_EXT
#define APP_CONF_PARAM_FLASH_USE_EXT  0
#endif

/**
 * @def   APP_CONF_SLEEP_FREEZE_IWDG
 * @brief Deep-sleep IWDG strategy.
 *        0 = Periodic wake every 1.5 s to feed IWDG (safe default —
 *            IWDG still resets the MCU if wake-up code fails).
 *        1 = Freeze IWDG via Flash Option Byte IWDG_STOP during
 *            Stop 2 (lower power — wake every ~65 s instead of
 *            1.5 s, but if wake-up fails the watchdog will NOT
 *            reset the MCU).
 *
 *        Switching between 0 and 1 automatically programmes the
 *        Option Byte on the next boot (one-time reset).
 */
#ifndef APP_CONF_SLEEP_FREEZE_IWDG
#define APP_CONF_SLEEP_FREEZE_IWDG  0
#endif

/**
 * @def   APP_CONF_SLEEP_MAINTENANCE_LOG
 * @brief LPTIM maintenance wake log switch.
 *        1 = print + flush a log line every LPTIM wake leg (debug).
 *        0 = silent maintenance wake (lower power, no UART TX).
 */
#ifndef APP_CONF_SLEEP_MAINTENANCE_LOG
#define APP_CONF_SLEEP_MAINTENANCE_LOG    1
#endif

/* ---- Internal Flash geometry (BACKEND=0) ---- */
/*
 * Constraints:
 *   - INT_ADDR must be page-aligned (multiple of INT_SIZE).
 *   - INT_SIZE must match the MCU page size (STM32L4S5 dual-bank = 4096).
 *   - INT_ADDR + INT_SIZE must not exceed Flash end (0x08200000 for 2 MB).
 *   - INT_BANK and INT_PAGE must correspond to INT_ADDR.
 *   - Keep away from firmware code region — use high pages only.
 */

/** @brief NVM page start address (Bank 2 last page on STM32L4S5). */
#ifndef APP_CONF_PARAM_FLASH_INT_ADDR
#define APP_CONF_PARAM_FLASH_INT_ADDR       0x081FF000u
#endif
/** @brief NVM page size in bytes (must equal MCU Flash page size). */
#ifndef APP_CONF_PARAM_FLASH_INT_SIZE
#define APP_CONF_PARAM_FLASH_INT_SIZE       0x1000u     /* 4096 */
#endif
/** @brief Number of consecutive pages for NVM rotation (1..N).
 *        More pages = longer Flash endurance (×N), uses more space.
 *        Pages are laid out downward from INT_ADDR:
 *          page 0 = INT_ADDR, page 1 = INT_ADDR - INT_SIZE, ...
 *        Default 2: ensures that when one page is full and the next
 *        page is being erased, the previous page still holds valid
 *        records for power-fail recovery. */
#ifndef APP_CONF_PARAM_FLASH_INT_PAGES
#define APP_CONF_PARAM_FLASH_INT_PAGES      2u
#endif
/** @brief Flash bank number for erase (1 or 2). */
#ifndef APP_CONF_PARAM_FLASH_INT_BANK
#define APP_CONF_PARAM_FLASH_INT_BANK       2u          /* FLASH_BANK_2 */
#endif
/** @brief Page number within the bank for the LAST (highest) NVM page.
 *        With multi-page rotation, pages occupy INT_PAGE down to
 *        INT_PAGE - (INT_PAGES - 1). */
#ifndef APP_CONF_PARAM_FLASH_INT_PAGE
#define APP_CONF_PARAM_FLASH_INT_PAGE       255u
#endif

/* ---- Flash Log Area (separate from parameter NVM) ---- */
/*
 * Log storage is completely independent from the parameter NVM area.
 * It can use either internal Flash or external OSPI NOR Flash,
 * controlled by APP_CONF_LOG_FLASH_USE_EXT — independent of
 * APP_CONF_PARAM_FLASH_USE_EXT (parameter storage backend).
 *
 * Internal layout (Bank 2, below NVM params):
 *   Page 255 (0x081FF000): NVM params page 0
 *   Page 254 (0x081FE000): NVM params page 1
 *   Page 253 (0x081FD000): Log page 0
 *   Page 252 (0x081FC000): Log page 1
 *   ...
 *
 * External layout (MX25R6435F, below NVM sector if also external):
 *   0x7FE000: Log sector 0
 *   0x7FD000: Log sector 1
 *   ...
 */

/** @brief Flash log feature switch. 1 = enable persistent log. */
#ifndef APP_CONF_LOG_FLASH
#define APP_CONF_LOG_FLASH          1
#endif

/**
 * @def   APP_CONF_LOG_FLASH_USE_EXT
 * @brief Log storage backend selection (independent from parameter NVM).
 *        0 = Internal Flash (default)
 *        1 = External OSPI NOR Flash (MX25R6435F)
 */
#ifndef APP_CONF_LOG_FLASH_USE_EXT
#define APP_CONF_LOG_FLASH_USE_EXT  1
#endif

/* ---- Internal Flash log geometry (LOG_USE_EXT=0) ---- */

/** @brief Internal Flash log start address (highest page of log region). */
#ifndef APP_CONF_LOG_FLASH_INT_ADDR
#define APP_CONF_LOG_FLASH_INT_ADDR     0x081FD000u
#endif
/** @brief Internal Flash log page size. */
#ifndef APP_CONF_LOG_FLASH_INT_SIZE
#define APP_CONF_LOG_FLASH_INT_SIZE     0x1000u     /* 4096 */
#endif
/** @brief Number of internal Flash pages for log ring. */
#ifndef APP_CONF_LOG_FLASH_INT_PAGES
#define APP_CONF_LOG_FLASH_INT_PAGES    2u
#endif
/** @brief Internal Flash bank for log pages. */
#ifndef APP_CONF_LOG_FLASH_INT_BANK
#define APP_CONF_LOG_FLASH_INT_BANK     2u
#endif
/** @brief Page number of the internal log area start (within the bank). */
#ifndef APP_CONF_LOG_FLASH_INT_PAGE_NUM
#define APP_CONF_LOG_FLASH_INT_PAGE_NUM 253u
#endif

/* ---- External OSPI Flash log geometry (LOG_USE_EXT=1) ---- */
/*
 * Default: sits below the NVM parameter sector (0x7FF000).
 * 100 sectors × 4 KB = 400 KB log storage = 3200 entries.
 * Address range: 0x79B000 .. 0x7FEFFF
 *
 * MX25R6435F has 100,000 erase cycles per sector and 8 MB total.
 * Plenty of room — adjust PAGES count as needed.
 */

/** @brief External Flash log start address (highest sector). */
#ifndef APP_CONF_LOG_FLASH_EXT_ADDR
#define APP_CONF_LOG_FLASH_EXT_ADDR     0x7FE000u
#endif
/** @brief External Flash log sector size. */
#ifndef APP_CONF_LOG_FLASH_EXT_SIZE
#define APP_CONF_LOG_FLASH_EXT_SIZE     0x1000u     /* 4096 */
#endif
/** @brief Number of external Flash sectors for log ring.
 *        100 sectors = 400 KB = 3200 log entries. */
#ifndef APP_CONF_LOG_FLASH_EXT_PAGES
#define APP_CONF_LOG_FLASH_EXT_PAGES    4u
#endif

/* ---- Unified log geometry macros (selected by LOG_USE_EXT) ---- */

#if APP_CONF_LOG_FLASH
#if APP_CONF_LOG_FLASH_USE_EXT == 0
#define APP_CONF_LOG_FLASH_ADDR         APP_CONF_LOG_FLASH_INT_ADDR
#define APP_CONF_LOG_FLASH_PAGE_SIZE    APP_CONF_LOG_FLASH_INT_SIZE
#define APP_CONF_LOG_FLASH_PAGES        APP_CONF_LOG_FLASH_INT_PAGES
#define APP_CONF_LOG_FLASH_BANK         APP_CONF_LOG_FLASH_INT_BANK
#define APP_CONF_LOG_FLASH_PAGE_NUM     APP_CONF_LOG_FLASH_INT_PAGE_NUM
#else
#define APP_CONF_LOG_FLASH_ADDR         APP_CONF_LOG_FLASH_EXT_ADDR
#define APP_CONF_LOG_FLASH_PAGE_SIZE    APP_CONF_LOG_FLASH_EXT_SIZE
#define APP_CONF_LOG_FLASH_PAGES        APP_CONF_LOG_FLASH_EXT_PAGES
#endif
#endif

/* ---- External OSPI Flash geometry (BACKEND=1) ---- */
/*
 * Constraints:
 *   - EXT_ADDR must be sector-aligned (multiple of EXT_SIZE).
 *   - EXT_SIZE must match the chip's smallest erasable unit (MX25R6435F = 4096).
 *   - EXT_ADDR + EXT_SIZE must not exceed chip capacity (8 MB = 0x800000).
 *   - Avoid sectors used by OTA / file system / other storage.
 */

/** @brief NVM sector start address inside external NOR (MX25R6435F). */
#ifndef APP_CONF_PARAM_FLASH_EXT_ADDR
#define APP_CONF_PARAM_FLASH_EXT_ADDR       0x7FF000u
#endif
/** @brief NVM sector size in bytes (must equal chip erase sector size). */
#ifndef APP_CONF_PARAM_FLASH_EXT_SIZE
#define APP_CONF_PARAM_FLASH_EXT_SIZE       0x1000u     /* 4096 */
#endif
/** @brief Number of consecutive sectors for NVM rotation (1..N).
 *        Sectors are laid out downward from EXT_ADDR:
 *          sector 0 = EXT_ADDR, sector 1 = EXT_ADDR - EXT_SIZE, ...
 *        Default 1 = single-sector (backward compatible). */
#ifndef APP_CONF_PARAM_FLASH_EXT_PAGES
#define APP_CONF_PARAM_FLASH_EXT_PAGES      1u
#endif

#if APP_CONF_PARAM_FLASH

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================== */
/*  Magic / version                                                    */
/* ================================================================== */

#define APP_CONFIG_MAGIC    0xA5A55A5Au
#define APP_CONFIG_VERSION  1u

/* ================================================================== */
/*  Configuration object (32 bytes, 8-byte aligned)                    */
/* ================================================================== */

typedef struct {
    uint32_t magic;         /**< Header magic (APP_CONFIG_MAGIC)     */
    uint32_t version;       /**< Structure version for OTA compat    */

    /* ---- Application parameters ---- */
    uint32_t threshold;     /**< Alarm threshold (e.g. temperature)  */
    uint32_t led_mode;      /**< LED behaviour mode                  */
    uint32_t device_id;     /**< Device identifier                   */

    /* ---- Padding & integrity ---- */
    uint32_t reserved[2];   /**< Reserved for future use (set to 0)  */
    uint32_t crc32;         /**< CRC32 over bytes [0 .. crc32)       */
} App_Config;

/* Compile-time guard: structure must be 8-byte aligned for Flash */
typedef char app_config_align_chk[(sizeof(App_Config) % 8u == 0u) ? 1 : -1];

/* Compile-time guard: page size must be evenly divisible by record size */
#if (APP_CONF_PARAM_FLASH_USE_EXT == 0)
typedef char app_config_page_div_chk[
    (APP_CONF_PARAM_FLASH_INT_SIZE % sizeof(App_Config) == 0u) ? 1 : -1];
#else
typedef char app_config_page_div_chk[
    (APP_CONF_PARAM_FLASH_EXT_SIZE % sizeof(App_Config) == 0u) ? 1 : -1];
#endif

/* ================================================================== */
/*  Global instance (RAM shadow of the NVM record)                     */
/* ================================================================== */

extern App_Config g_app_cfg;

/* ================================================================== */
/*  API                                                                */
/* ================================================================== */

/**
 * @brief Load configuration from Flash, or apply defaults if empty/corrupt.
 *
 * Call once at boot, after BSP_Init().
 */
void App_Config_Init(void);

/**
 * @brief Persist the current g_app_cfg to Flash (log-structured append).
 *
 * Automatically recalculates the CRC32 before writing.
 *
 * @return true on success, false on Flash write error.
 */
bool App_Config_Save(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_CONF_PARAM_FLASH */
#endif /* APP_CONFIG_H */
