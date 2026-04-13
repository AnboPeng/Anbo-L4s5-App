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
 * @file  app_config.c
 * @brief Persistent system configuration — manager implementation
 *
 * Responsibilities:
 *   - Load latest config from Flash (with magic + CRC validation)
 *   - Apply factory defaults when no valid record exists
 *   - Save with automatic CRC32 calculation
 *
 * CRC32 uses nibble-based lookup (16-entry table, 64 B Flash) with
 * the standard Ethernet polynomial (0xEDB88320, reflected).
 */

#include "app_config.h"

#if APP_CONF_PARAM_FLASH

#if APP_CONF_PARAM_FLASH_USE_EXT == 0
#include "b_l4s5i_flash_drv.h"
#else
#include "b_l4s5i_ospi_flash_drv.h"
#endif
#include "anbo_log.h"
#include <string.h>

/* ================================================================== */
/*  Global instance                                                    */
/* ================================================================== */

App_Config g_app_cfg;

/* ================================================================== */
/*  Software CRC32 (nibble-based, 16-entry LUT)                       */
/* ================================================================== */

static const uint32_t s_crc32_lut[16] = {
    0x00000000u, 0x1DB71064u, 0x3B6E20C8u, 0x26D930ACu,
    0x76DC4190u, 0x6B6B51F4u, 0x4DB26158u, 0x5005713Cu,
    0xEDB88320u, 0xF00F9344u, 0xD6D6A3E8u, 0xCB61B38Cu,
    0x9B64C2B0u, 0x86D3D2D4u, 0xA00AE278u, 0xBDBDF21Cu
};

static uint32_t crc32_calc(const void *data, uint32_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    uint32_t crc = 0xFFFFFFFFu;
    uint32_t i;

    for (i = 0u; i < len; i++) {
        crc = s_crc32_lut[(crc ^ p[i]) & 0x0Fu] ^ (crc >> 4u);
        crc = s_crc32_lut[(crc ^ (p[i] >> 4u)) & 0x0Fu] ^ (crc >> 4u);
    }
    return crc ^ 0xFFFFFFFFu;
}

/* ================================================================== */
/*  Defaults                                                           */
/* ================================================================== */

static void config_set_defaults(void)
{
    memset(&g_app_cfg, 0, sizeof(g_app_cfg));
    g_app_cfg.magic     = APP_CONFIG_MAGIC;
    g_app_cfg.version   = APP_CONFIG_VERSION;
    g_app_cfg.threshold = 30u;       /* default 30 */
    g_app_cfg.led_mode  = 0u;
    g_app_cfg.device_id = 0u;
}

/* ================================================================== */
/*  Validation                                                         */
/* ================================================================== */

static bool config_is_valid(const App_Config *cfg)
{
    uint32_t crc;

    if (cfg->magic != APP_CONFIG_MAGIC) {
        return false;
    }
    /* CRC covers everything up to (but not including) the crc32 field */
    crc = crc32_calc(cfg, sizeof(*cfg) - sizeof(cfg->crc32));
    return crc == cfg->crc32;
}

/**
 * @brief NVM validator callback for BSP_Flash_ReadValidated().
 *
 * Checks magic number and CRC32 of a candidate App_Config record.
 * Used to skip partially-written (power-fail) records and automatically
 * fall back to the previous valid record.
 */
static bool config_nvm_validator(const void *rec, uint32_t len)
{
    (void)len;
    return config_is_valid((const App_Config *)rec);
}

/* ================================================================== */
/*  Public API                                                         */
/* ================================================================== */

void App_Config_Init(void)
{
    if (BSP_Flash_ReadValidated(&g_app_cfg, sizeof(g_app_cfg),
                                config_nvm_validator)) {
        ANBO_LOGI("NVM: restored (v%u thresh=%u)",
                  g_app_cfg.version, g_app_cfg.threshold);
    } else {
        ANBO_LOGI("NVM: no valid record, loading defaults");
        config_set_defaults();
        App_Config_Save();      /* persist defaults on first boot */
    }
}

bool App_Config_Save(void)
{
    /* Recalculate CRC before write */
    g_app_cfg.crc32 = crc32_calc(&g_app_cfg,
                                 sizeof(g_app_cfg) - sizeof(g_app_cfg.crc32));

    if (BSP_Flash_WriteAppend(&g_app_cfg, sizeof(g_app_cfg))) {
        ANBO_LOGD("NVM: saved OK");
        return true;
    }
    ANBO_LOGI("NVM: save FAILED");
    return false;
}

#endif /* APP_CONF_PARAM_FLASH */
