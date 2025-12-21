/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/**
 * @file zigbee_handlers.c
 * @brief Zigbee stack event handlers and identify callbacks
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <zboss_api.h>
#include <zboss_api_addons.h>
#include <zigbee/zigbee_app_utils.h>
#include <zigbee/zigbee_error_handler.h>
#include <ram_pwrdn.h>

#include "zigbee_handlers.h"
#include "zigbee_device.h"
#include "gpio_control.h"

#if CONFIG_ZIGBEE_FOTA
#include <zigbee/zigbee_fota.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/dfu/mcuboot.h>
#endif

LOG_MODULE_REGISTER(zigbee_handlers, LOG_LEVEL_INF);

/**@brief Function to toggle the identify LED.
 *
 * @param  bufid  Unused parameter, required by ZBOSS scheduler API.
 */
static void toggle_identify_led(zb_bufid_t bufid)
{
	static int blink_status;

#ifdef CONFIG_DK_LIBRARY
	led_power_set((++blink_status) % 2);
	ZB_SCHEDULE_APP_ALARM(toggle_identify_led, bufid,
			      ZB_MILLISECONDS_TO_BEACON_INTERVAL(100));
#else
	/* On non-DK targets `led_power_set()` controls external VCC and must not blink. */
	ARG_UNUSED(blink_status);
	led_power_set(false);
	ARG_UNUSED(bufid);
#endif
}

void identify_cb(zb_bufid_t bufid)
{
	zb_ret_t zb_err_code;

	if (bufid) {
#ifndef CONFIG_DK_LIBRARY
		/* No identify indication in low-power/non-DK builds. */
		zb_buf_free(bufid);
		return;
#endif
		/* Schedule a self-scheduling function that will toggle the LED. */
		ZB_SCHEDULE_APP_CALLBACK(toggle_identify_led, bufid);
	} else {
		/* Cancel the toggling function alarm and turn off LED. */
		zb_err_code = ZB_SCHEDULE_APP_ALARM_CANCEL(toggle_identify_led, ZB_ALARM_ANY_PARAM);
		ZVUNUSED(zb_err_code);

		/* Turn off power LED to save power */
		led_power_set(false);
	}
}

#ifdef CONFIG_ZIGBEE_FOTA
static void confirm_image(void)
{
	if (!boot_is_img_confirmed()) {
		int ret = boot_write_img_confirmed();

		if (ret) {
			LOG_ERR("Couldn't confirm image: %d", ret);
		} else {
			LOG_INF("Marked image as OK");
		}
	}
}

static void ota_evt_handler(const struct zigbee_fota_evt *evt)
{
	switch (evt->id) {
	case ZIGBEE_FOTA_EVT_PROGRESS:
		led_power_set(evt->dl.progress % 2);
		break;

	case ZIGBEE_FOTA_EVT_FINISHED:
		LOG_INF("Reboot application.");
		if (IS_ENABLED(CONFIG_RAM_POWER_DOWN_LIBRARY)) {
			power_up_unused_ram();
		}
		sys_reboot(SYS_REBOOT_COLD);
		break;

	case ZIGBEE_FOTA_EVT_ERROR:
		LOG_ERR("OTA image transfer failed.");
		break;

	default:
		break;
	}
}
#endif /* CONFIG_ZIGBEE_FOTA */

void zigbee_handlers_init(void)
{
#ifdef CONFIG_ZIGBEE_FOTA
	zigbee_fota_init(ota_evt_handler);
	confirm_image();
#endif
}

/**@brief Zigbee stack event handler. */
void zboss_signal_handler(zb_bufid_t bufid)
{
	zb_zdo_app_signal_hdr_t *sig_hndler = NULL;
	zb_zdo_app_signal_type_t sig = zb_get_app_signal(bufid, &sig_hndler);
	zb_ret_t status = ZB_GET_APP_SIGNAL_STATUS(bufid);

#if IS_ENABLED(CONFIG_DK_LIBRARY) && (IS_ENABLED(CONFIG_CONSOLE) || IS_ENABLED(CONFIG_LOG))
	/* Development mode - indicate network status using LEDs. */
	if (sig == ZB_BDB_SIGNAL_DEVICE_FIRST_START ||
	    sig == ZB_BDB_SIGNAL_DEVICE_REBOOT ||
	    sig == ZB_BDB_SIGNAL_STEERING) {
		if (status == RET_OK) {
			led_power_set(true);  /* Joined - LED on */
		} else {
			led_power_set(false); /* Not joined - LED off */
		}
	}
#else
	/* Low power mode - always keep VCC/LED off */
	(void)sig;
	(void)status;
	led_power_set(false);
#endif

#ifdef CONFIG_ZIGBEE_FOTA
	zigbee_fota_signal_handler(bufid);
#endif

	switch (sig) {
	case ZB_BDB_SIGNAL_DEVICE_REBOOT:
		/* Device rebooted - check if we're still connected */
		if (status == RET_OK) {
			zigbee_device_set_network_joined(true);
		}
		ZB_ERROR_CHECK(zigbee_default_signal_handler(bufid));
		break;
	case ZB_BDB_SIGNAL_STEERING:
		/* Network steering completed */
		if (status == RET_OK) {
			zigbee_device_set_network_joined(true);
		}
		ZB_ERROR_CHECK(zigbee_default_signal_handler(bufid));
		break;
	case ZB_ZDO_SIGNAL_LEAVE:
		/* Left network */
		zigbee_device_set_network_joined(false);
		ZB_ERROR_CHECK(zigbee_default_signal_handler(bufid));
		break;
	default:
		ZB_ERROR_CHECK(zigbee_default_signal_handler(bufid));
		break;
	}

	if (bufid) {
		zb_buf_free(bufid);
	}
}
