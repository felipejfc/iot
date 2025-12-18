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

/* Relay GPIO on P0.29 - only if relay0 alias exists in devicetree */
#if DT_NODE_EXISTS(DT_ALIAS(relay0))
#define HAS_RELAY 1
static const struct gpio_dt_spec relay_ctrl = GPIO_DT_SPEC_GET(DT_ALIAS(relay0), gpios);
#else
#define HAS_RELAY 0
#endif

#ifndef CONFIG_DK_LIBRARY
/* Custom GPIO specs when DK library is not used */
static const struct gpio_dt_spec button_main = GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);

/* VCC power control on P0.13 - HIGH = VCC on, LOW = VCC off */
#if DT_NODE_EXISTS(DT_ALIAS(vcc_ctrl))
#define HAS_VCC_CTRL 1
static const struct gpio_dt_spec vcc_ctrl = GPIO_DT_SPEC_GET(DT_ALIAS(vcc_ctrl), gpios);
#else
#define HAS_VCC_CTRL 0
#endif
#endif

int gpio_control_init(void)
{
	int err;

#ifndef CONFIG_DK_LIBRARY
	/* Configure VCC control pin (P0.13) - set HIGH to keep VCC on */
#if HAS_VCC_CTRL
	if (!gpio_is_ready_dt(&vcc_ctrl)) {
		LOG_ERR("VCC control GPIO not ready");
		return -ENODEV;
	}
	/* Start with VCC ON (P0.13 HIGH) - set LOW to cut VCC for low power */
	err = gpio_pin_configure_dt(&vcc_ctrl, GPIO_OUTPUT_ACTIVE);
	if (err) {
		LOG_ERR("Failed to configure VCC control: %d", err);
		return err;
	}
	LOG_INF("VCC control initialized (P0.13 HIGH = VCC on)");
#endif

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

	/* Configure relay output on P0.29 (only if defined in devicetree) */
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
	LOG_INF("Relay GPIO initialized on P%d.%02d",
		relay_ctrl.port == DEVICE_DT_GET(DT_NODELABEL(gpio0)) ? 0 : 1,
		relay_ctrl.pin);
#else
	LOG_INF("GPIO initialized (no relay configured)");
#endif

	return 0;
}

void led_power_set(bool on)
{
#ifdef CONFIG_DK_LIBRARY
	dk_set_led(DK_LED2, on ? 1 : 0);
#else
	/* On Pro Micro, this controls VCC via P0.13 */
	/* For low power: call led_power_set(false) to cut VCC */
#if HAS_VCC_CTRL
	gpio_pin_set_dt(&vcc_ctrl, on ? 1 : 0);
#endif
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
