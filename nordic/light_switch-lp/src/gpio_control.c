/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/**
 * @file gpio_control.c
 * @brief GPIO control implementation for LEDs, relay, and button
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

#include "gpio_control.h"

#ifdef CONFIG_DK_LIBRARY
#include <dk_buttons_and_leds.h>
#endif

LOG_MODULE_REGISTER(gpio_control, LOG_LEVEL_INF);

/* Relay GPIO - only if relay0 alias exists in devicetree */
#if DT_NODE_EXISTS(DT_ALIAS(relay0))
#define HAS_RELAY 1
static const struct gpio_dt_spec relay_ctrl = GPIO_DT_SPEC_GET(DT_ALIAS(relay0), gpios);
#else
#define HAS_RELAY 0
#endif

#ifndef CONFIG_DK_LIBRARY
/* Custom GPIO specs when DK library is not used */
static const struct gpio_dt_spec led_control = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
static const struct gpio_dt_spec led_power = GPIO_DT_SPEC_GET(DT_ALIAS(led1), gpios);
static const struct gpio_dt_spec button_main = GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);
#endif

int gpio_control_init(void)
{
	int err;

#ifndef CONFIG_DK_LIBRARY
	/* Configure control LED */
	if (!gpio_is_ready_dt(&led_control)) {
		LOG_ERR("Control LED GPIO not ready");
		return -ENODEV;
	}
	err = gpio_pin_configure_dt(&led_control, GPIO_OUTPUT_INACTIVE);
	if (err) {
		LOG_ERR("Failed to configure control LED: %d", err);
		return err;
	}

	/* Configure power LED */
	if (!gpio_is_ready_dt(&led_power)) {
		LOG_ERR("Power LED GPIO not ready");
		return -ENODEV;
	}
	err = gpio_pin_configure_dt(&led_power, GPIO_OUTPUT_INACTIVE);
	if (err) {
		LOG_ERR("Failed to configure power LED: %d", err);
		return err;
	}

	/* Configure button input (interrupt setup done by button_handler) */
	if (!gpio_is_ready_dt(&button_main)) {
		LOG_ERR("Button GPIO not ready");
		return -ENODEV;
	}
	err = gpio_pin_configure_dt(&button_main, GPIO_INPUT);
	if (err) {
		LOG_ERR("Failed to configure button: %d", err);
		return err;
	}
#endif

	/* Configure relay output (only if defined in devicetree) */
#if HAS_RELAY
	if (!gpio_is_ready_dt(&relay_ctrl)) {
		LOG_ERR("Relay GPIO not ready");
		return -ENODEV;
	}
	err = gpio_pin_configure_dt(&relay_ctrl, GPIO_OUTPUT_INACTIVE);
	if (err) {
		LOG_ERR("Failed to configure relay: %d", err);
		return err;
	}
	LOG_INF("GPIO initialized with relay on P%d.%02d",
		relay_ctrl.port == DEVICE_DT_GET(DT_NODELABEL(gpio0)) ? 0 : 1,
		relay_ctrl.pin);
#else
	LOG_INF("GPIO initialized (no relay configured)");
#endif

	return 0;
}

void led_control_set(bool on)
{
#ifdef CONFIG_DK_LIBRARY
	dk_set_led(DK_LED1, on ? 1 : 0);
#else
	gpio_pin_set_dt(&led_control, on ? 1 : 0);
#endif
}

void led_power_set(bool on)
{
#ifdef CONFIG_DK_LIBRARY
	dk_set_led(DK_LED2, on ? 1 : 0);
#else
	gpio_pin_set_dt(&led_power, on ? 1 : 0);
#endif
}

void relay_control_set(bool on)
{
#if HAS_RELAY
	gpio_pin_set_dt(&relay_ctrl, on ? 1 : 0);
#else
	ARG_UNUSED(on);
#endif
}

#ifndef CONFIG_DK_LIBRARY
bool button_get_state(void)
{
	/* `gpio_pin_get_dt()` already applies `GPIO_ACTIVE_LOW` from devicetree. */
	return gpio_pin_get_dt(&button_main) > 0;
}

const struct gpio_dt_spec *button_get_dt_spec(void)
{
	return &button_main;
}
#endif
