/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * Board-specific LCD panel factory for esp_hi_skill.
 */

#include <string.h>
#include "esp_lcd_st7789.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"

static const char *TAG = "ESP_HI_SKILL_SETUP";

/* Use component default ST7789 init sequence to avoid panel mismatch. */
static const st7789_vendor_config_t s_vendor_config = {
    .init_cmds = NULL,
    .init_cmds_size = 0,
};

esp_err_t lcd_panel_factory_entry_t(esp_lcd_panel_io_handle_t io,
                                    const esp_lcd_panel_dev_config_t *panel_dev_config,
                                    esp_lcd_panel_handle_t *ret_panel)
{
    esp_lcd_panel_dev_config_t panel_dev_cfg = {0};
    memcpy(&panel_dev_cfg, panel_dev_config, sizeof(esp_lcd_panel_dev_config_t));
    panel_dev_cfg.vendor_config = (void *)&s_vendor_config;

    esp_err_t ret = esp_lcd_new_panel_st7789(io, &panel_dev_cfg, ret_panel);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_lcd_new_panel_st7789 failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_lcd_panel_set_gap(*ret_panel, 1, 26);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_lcd_panel_set_gap failed: %s", esp_err_to_name(ret));
        return ret;
    }

    return ESP_OK;
}
