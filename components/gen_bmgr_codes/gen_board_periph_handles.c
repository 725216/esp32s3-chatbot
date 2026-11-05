/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * Auto-generated peripheral handle definition file
 * DO NOT MODIFY THIS FILE MANUALLY
 *
 * See LICENSE file for details.
 */

#include <stddef.h>
#include "esp_board_periph.h"
#include "periph_i2s.h"

// Peripheral handle array
esp_board_periph_entry_t g_esp_board_periph_handles[] = {
    {
        .next = NULL,
        .type = "i2s",
        .role = ESP_BOARD_PERIPH_ROLE_MASTER,
        .init = periph_i2s_init,
        .deinit = periph_i2s_deinit
    },
};
