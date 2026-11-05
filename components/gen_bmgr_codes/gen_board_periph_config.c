/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * Auto-generated peripheral configuration file
 * DO NOT MODIFY THIS FILE MANUALLY
 *
 * See LICENSE file for details.
 */

#include <stdlib.h>
#include "esp_board_periph.h"
#include "periph_i2s.h"

// Peripheral configuration structures
const static periph_i2s_config_t esp_bmgr_i2s_out_cfg = {
    .port = I2S_NUM_0,
    .role = I2S_ROLE_MASTER,
    .mode = I2S_COMM_MODE_STD,
    .direction = I2S_DIR_TX,
    .i2s_cfg = {
        .std = {
            .clk_cfg = {
                .sample_rate_hz = 16000,
                .clk_src = I2S_CLK_SRC_DEFAULT,
                .ext_clk_freq_hz = 0,
                .mclk_multiple = I2S_MCLK_MULTIPLE_256,
            },
            .slot_cfg = {
                .data_bit_width = I2S_DATA_BIT_WIDTH_16BIT,
                .slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO,
                .slot_mode = I2S_SLOT_MODE_MONO,
                .slot_mask = I2S_STD_SLOT_LEFT,
                .ws_width = 16,
                .ws_pol = false,
                .bit_shift = true,
                .left_align = true,
                .big_endian = false,
                .bit_order_lsb = false,
            },
            .gpio_cfg = {
                .mclk = -1,
                .bclk = 8,
                .ws = 7,
                .dout = 9,
                .din = -1,
                .invert_flags = {
                    .mclk_inv = false,
                    .bclk_inv = false,
                    .ws_inv = false,
                },
            },
        },
    },
};

const static periph_i2s_config_t esp_bmgr_i2s_in_cfg = {
    .port = I2S_NUM_1,
    .role = I2S_ROLE_MASTER,
    .mode = I2S_COMM_MODE_STD,
    .direction = I2S_DIR_RX,
    .i2s_cfg = {
        .std = {
            .clk_cfg = {
                .sample_rate_hz = 16000,
                .clk_src = I2S_CLK_SRC_DEFAULT,
                .ext_clk_freq_hz = 0,
                .mclk_multiple = I2S_MCLK_MULTIPLE_256,
            },
            .slot_cfg = {
                .data_bit_width = I2S_DATA_BIT_WIDTH_16BIT,
                .slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO,
                .slot_mode = I2S_SLOT_MODE_MONO,
                .slot_mask = I2S_STD_SLOT_LEFT,
                .ws_width = 16,
                .ws_pol = false,
                .bit_shift = true,
                .left_align = true,
                .big_endian = false,
                .bit_order_lsb = false,
            },
            .gpio_cfg = {
                .mclk = -1,
                .bclk = 5,
                .ws = 4,
                .dout = -1,
                .din = 6,
                .invert_flags = {
                    .mclk_inv = false,
                    .bclk_inv = false,
                    .ws_inv = false,
                },
            },
        },
    },
};

// Peripheral descriptor array
const esp_board_periph_desc_t g_esp_board_peripherals[] = {
    {
        .next = &g_esp_board_peripherals[1],
        .name = "i2s_out",
        .type = "i2s",
        .format = "std-out",
        .role = ESP_BOARD_PERIPH_ROLE_MASTER,
        .cfg = &esp_bmgr_i2s_out_cfg,
        .cfg_size = sizeof(esp_bmgr_i2s_out_cfg),
        .id = 0,
    },
    {
        .next = NULL,
        .name = "i2s_in",
        .type = "i2s",
        .format = "std-in",
        .role = ESP_BOARD_PERIPH_ROLE_MASTER,
        .cfg = &esp_bmgr_i2s_in_cfg,
        .cfg_size = sizeof(esp_bmgr_i2s_in_cfg),
        .id = 0,
    },
};
