/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

/**
 * @brief Convenience umbrella header for the public service-helper API set.
 */

#include "service_helper/media/audio.hpp"
#include "service_helper/media/bt_speaker.hpp"
#include "brookesia/service_manager/helper/base.hpp"
#include "service_helper/system/device.hpp"
#include "service_helper/framework/manager.hpp"
#include "service_helper/framework/dataflow.hpp"
#include "service_helper/framework/utils.hpp"
#include "service_helper/media/display.hpp"
#include "service_helper/media/picodet.hpp"
#include "service_helper/network/http.hpp"
#include "service_helper/network/ble.hpp"
#include "service_helper/network/bt.hpp"
#include "service_helper/emulation/nes.hpp"
#include "service_helper/system/storage.hpp"
#include "service_helper/system/usb.hpp"
#include "service_helper/media/video.hpp"
#include "service_helper/network/wifi.hpp"
#include "service_helper/network/sntp.hpp"
#include "service_helper/agent/coze.hpp"
#include "service_helper/agent/manager.hpp"
#include "service_helper/agent/openai.hpp"
#include "service_helper/agent/xiaozhi.hpp"
#include "service_helper/expression/emote.hpp"
