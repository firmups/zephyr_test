/*
 * Copyright (c) 2012-2014 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <firmups-device-sdk/sdk.h>
#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "button.h"
#include "led.h"
#include "firmware_updater.h"
#include "fatal_error.h"

LOG_MODULE_REGISTER(main, CONFIG_FIRMUPS_LOG_LEVEL);
static const uint32_t firmware_version = 1;
static struct firmware_updater_context *fw_u_c;

static void firmware_check(void)
{
#ifdef NRF5340DK
	if (!firmware_updater_connect(fw_u_c)) {
		return;
	}
#endif
	if (firmware_update_available(fw_u_c)) {
		int ret = firmware_updater_update_firmware(fw_u_c);
		if (ret < 0) {
			LOG_ERR("Firmware update failed");
		}
	} else {
		LOG_INF("No firmware update required");
	}
#ifdef NRF5340DK
	firmware_updater_disconnect(fw_u_c);
#endif
}

int main(void)
{
	uint32_t device_id = 1; // Example device ID
	uint8_t sdk_work_buffer[2048];

	led_initialize();
	led_blink_start(LED_BLUE);
	fw_u_c = firmware_updater_initialize(sdk_work_buffer, sizeof(sdk_work_buffer), device_id,
					     firmware_version);
	if (fw_u_c == NULL) {
		LOG_ERR("Firmware updater context could not be initialized");
		blink_red_led_forever();
	}
	button_init(firmware_check);

	while (true) {
		k_sleep(K_MSEC(100000));
	}
}
