/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/**
 * @file main.c
 * @brief Zigbee LED Controller - Application entry point
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/pm/device.h>
#include <ram_pwrdn.h>

#include <zboss_api.h>
#include <zigbee/zigbee_app_utils.h>
#include <zb_nrf_platform.h>

#include "zb_mem_config_custom.h"
#include "gpio_control.h"
#include "zigbee_device.h"
#include "zigbee_handlers.h"
#include "adc_reader.h"

#ifdef CONFIG_DK_LIBRARY
#include <dk_buttons_and_leds.h>
#else
#include "button_handler.h"
#endif

#if defined(CONFIG_USB_DEVICE_STACK)
#include <zephyr/usb/usb_device.h>
#endif

#if !defined ZB_ED_ROLE
#error Define ZB_ED_ROLE to compile light switch (End Device) source code.
#endif

LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);

#ifdef CONFIG_DK_LIBRARY
/* Factory reset timer for long press detection */
#define FACTORY_RESET_TIME_MS 5000

static struct k_timer factory_reset_timer;
static volatile bool factory_reset_pending = false;

/* Callback to perform factory reset in ZBOSS context */
static void do_factory_reset(zb_uint8_t param)
{
	ARG_UNUSED(param);
	LOG_INF("Performing factory reset...");
	zb_bdb_reset_via_local_action(0);
}

static void factory_reset_timer_handler(struct k_timer *timer)
{
	factory_reset_pending = true;
	LOG_INF("Factory reset triggered!");
	/* Schedule reset in ZBOSS context - cannot call directly from timer/ISR */
	ZB_SCHEDULE_APP_CALLBACK(do_factory_reset, 0);
}

/**
 * @brief DK button handler callback
 *
 * Button 1 (sw0): Toggle Relay (endpoint 1)
 * Long press Button 1: Factory reset
 */
static void dk_button_handler(uint32_t button_state, uint32_t has_changed)
{
	/* Button 1 pressed - Relay control */
	if (has_changed & DK_BTN1_MSK) {
		if (button_state & DK_BTN1_MSK) {
			/* Button pressed - start factory reset timer */
			k_timer_start(&factory_reset_timer,
				      K_MSEC(FACTORY_RESET_TIME_MS), K_NO_WAIT);
		} else {
			/* Button released */
			k_timer_stop(&factory_reset_timer);
			if (!factory_reset_pending) {
				/* Short press - toggle relay */
				user_input_indicate();
				zigbee_device_toggle_relay();
			}
			factory_reset_pending = false;
		}
	}
}
#endif /* CONFIG_DK_LIBRARY */

/* QSPI flash device - put into deep power-down for low power */
#define QSPI_FLASH_NODE DT_NODELABEL(p25q16h)
#if DT_NODE_EXISTS(QSPI_FLASH_NODE)
static const struct device *qspi_flash = DEVICE_DT_GET_OR_NULL(QSPI_FLASH_NODE);
#endif

static void qspi_flash_suspend(void)
{
#if DT_NODE_EXISTS(QSPI_FLASH_NODE)
	if (qspi_flash && device_is_ready(qspi_flash)) {
		int err = pm_device_action_run(qspi_flash, PM_DEVICE_ACTION_SUSPEND);
		if (err < 0 && err != -EALREADY) {
			LOG_WRN("Failed to suspend QSPI flash: %d", err);
		} else {
			LOG_INF("QSPI flash in deep power-down");
		}
	}
#endif
}

int main(void)
{
	int err;

#if defined(CONFIG_USB_DEVICE_STACK)
	/* Enable USB CDC ACM for console */
	err = usb_enable(NULL);
	if (err && err != -EALREADY) {
		/* USB failed but continue anyway */
	}
	/* Give USB a moment to enumerate */
	k_sleep(K_MSEC(1000));
#endif

	/* Put QSPI flash into deep power-down to save power */
	qspi_flash_suspend();

	LOG_INF("Starting Zigbee LED Controller");

	/* Initialize GPIO (relay pin, and LEDs/buttons when not using DK library) */
	err = gpio_control_init();
	if (err) {
		LOG_ERR("GPIO initialization failed: %d", err);
		return err;
	}

#ifdef CONFIG_DK_LIBRARY
	/* Initialize DK buttons and LEDs */
	err = dk_buttons_init(dk_button_handler);
	if (err) {
		LOG_ERR("DK buttons initialization failed: %d", err);
		return err;
	}

	err = dk_leds_init();
	if (err) {
		LOG_ERR("DK LEDs initialization failed: %d", err);
		return err;
	}

	/* Initialize factory reset timer */
	k_timer_init(&factory_reset_timer, factory_reset_timer_handler, NULL);
#else
	/* Turn off power LED to save power */
	led_power_set(false);

	/* Initialize button handler with interrupts and debounce */
	err = button_handler_init(NULL);
	if (err) {
		LOG_ERR("Button handler initialization failed: %d", err);
		return err;
	}
#endif

	/* Configure Zigbee stack */
	zigbee_erase_persistent_storage(ERASE_PERSISTENT_CONFIG);
	zb_set_ed_timeout(ED_AGING_TIMEOUT_64MIN);

	/* Configure as sleepy end device if USB is not enabled */
#if !defined(CONFIG_USB_DEVICE_STACK)
	zigbee_configure_sleepy_behavior(true);

	/* Set keep-alive for low power (10 seconds) */
	zb_set_keepalive_timeout(ZB_MILLISECONDS_TO_BEACON_INTERVAL(10000));
#endif

	/* Power off unused sections of RAM to lower device power consumption */
	if (IS_ENABLED(CONFIG_RAM_POWER_DOWN_LIBRARY)) {
		power_down_unused_ram();
	}

	/* Initialize FOTA if enabled */
	zigbee_handlers_init();

	/* Initialize Zigbee device clusters and attributes */
	zigbee_device_init();

	/* Register device context and callbacks */
	zigbee_device_register();

	/* Start Zigbee stack */
	zigbee_enable();

	LOG_INF("Zigbee Relay Controller started - Relay is %s",
		zigbee_device_get_relay_state() ? "ON" : "OFF");

	/* Initialize ADC for voltage sensing */
	err = adc_reader_init();
	if (err) {
		LOG_ERR("ADC initialization failed: %d", err);
	} else {
		/* Start periodic voltage readings */
		adc_start_periodic_reading();
	}

	while (1) {
		k_sleep(K_FOREVER);
	}
}
