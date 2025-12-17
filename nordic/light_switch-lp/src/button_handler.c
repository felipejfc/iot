/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/**
 * @file button_handler.c
 * @brief Button input handling with proper debounce and factory reset support
 *
 * Uses "sample after quiet period" debounce approach:
 * - Any edge interrupt restarts a short debounce timer
 * - When timer fires (no edges for 30ms), we sample the actual button state
 * - This ensures we act on the settled state, not bouncy edges
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

#include <zboss_api.h>
#include <zigbee/zigbee_app_utils.h>

#include "button_handler.h"
#include "gpio_control.h"
#include "zigbee_device.h"

LOG_MODULE_REGISTER(button_handler, LOG_LEVEL_INF);

/* Timing constants */
#define DEBOUNCE_MS           30    /* Wait for button to settle */
#define FACTORY_RESET_TIME_MS 5000  /* Hold time for factory reset */

/* Button state machine */
typedef enum {
	BTN_IDLE,           /* Button released, waiting for press */
	BTN_PRESSED,        /* Button pressed, waiting for release or long-press timeout */
	BTN_LONG_PRESS,     /* Long press detected, factory reset in progress */
} button_state_t;

static volatile button_state_t btn_state = BTN_IDLE;

/* Timers */
static struct k_timer debounce_timer;      /* Debounce: sample state after quiet period */
static struct k_timer factory_reset_timer; /* Long press detection */

/* Work items for thread context execution */
static struct k_work short_press_work;
static struct k_work factory_reset_work;

/* GPIO callback */
static struct gpio_callback button_cb_data;

/* User callback */
static button_event_cb_t user_callback = NULL;

/* Forward declarations */
static void process_button_state(bool pressed);

/**
 * @brief Debounce timer handler - samples button state after quiet period
 *
 * Called 30ms after the last edge interrupt. The button should be settled now.
 */
static void debounce_timer_handler(struct k_timer *timer)
{
	bool pressed = button_get_state();
	process_button_state(pressed);
}

/**
 * @brief Process the debounced button state
 *
 * Called from debounce timer handler (ISR context).
 */
static void process_button_state(bool pressed)
{
	switch (btn_state) {
	case BTN_IDLE:
		if (pressed) {
			/* Transition: IDLE -> PRESSED */
			btn_state = BTN_PRESSED;
			LOG_DBG("Button pressed, starting long-press timer");
			k_timer_start(&factory_reset_timer,
				      K_MSEC(FACTORY_RESET_TIME_MS), K_NO_WAIT);
		}
		/* else: still idle, ignore */
		break;

	case BTN_PRESSED:
		if (!pressed) {
			/* Transition: PRESSED -> IDLE (short press) */
			btn_state = BTN_IDLE;
			k_timer_stop(&factory_reset_timer);
			LOG_DBG("Button released (short press)");
			k_work_submit(&short_press_work);
		}
		/* else: still pressed, waiting for release or timeout */
		break;

	case BTN_LONG_PRESS:
		if (!pressed) {
			/* Transition: LONG_PRESS -> IDLE */
			btn_state = BTN_IDLE;
			LOG_DBG("Button released after long press");
		}
		/* else: still pressed during/after factory reset */
		break;
	}
}

/**
 * @brief Factory reset timer handler - long press detected
 */
static void factory_reset_timer_handler(struct k_timer *timer)
{
	if (btn_state == BTN_PRESSED) {
		/* Still pressed after 5 seconds - trigger factory reset */
		btn_state = BTN_LONG_PRESS;
		LOG_INF("Long press detected - triggering factory reset");
		k_work_submit(&factory_reset_work);
	}
}

/**
 * @brief Button ISR - just restarts debounce timer
 *
 * We don't process the button state here - we wait for it to settle.
 */
static void button_isr(const struct device *dev, struct gpio_callback *cb,
		       uint32_t pins)
{
	/* Any edge restarts the debounce timer */
	k_timer_start(&debounce_timer, K_MSEC(DEBOUNCE_MS), K_NO_WAIT);
}

/**
 * @brief Short press work handler (thread context)
 */
static void short_press_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	/* Inform Zigbee stack about user input */
	user_input_indicate();

	/* Toggle relay */
	zigbee_device_toggle_relay();

	/* Notify user callback */
	if (user_callback) {
		user_callback(false);
	}
}

/* Callback to perform factory reset in ZBOSS context */
static void do_factory_reset(zb_uint8_t param)
{
	ARG_UNUSED(param);
	LOG_INF("Performing factory reset...");
	zb_bdb_reset_via_local_action(0);
}

/**
 * @brief Factory reset work handler (thread context)
 */
static void factory_reset_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	LOG_INF("Factory reset triggered!");

	/* Notify user callback */
	if (user_callback) {
		user_callback(true);
	}

	/* Schedule reset in ZBOSS context */
	ZB_SCHEDULE_APP_CALLBACK(do_factory_reset, 0);
}

int button_handler_init(button_event_cb_t callback)
{
	int err;
	const struct gpio_dt_spec *button_spec = button_get_dt_spec();

	user_callback = callback;

	/* Initialize timers */
	k_timer_init(&debounce_timer, debounce_timer_handler, NULL);
	k_timer_init(&factory_reset_timer, factory_reset_timer_handler, NULL);

	/* Initialize work items */
	k_work_init(&short_press_work, short_press_work_handler);
	k_work_init(&factory_reset_work, factory_reset_work_handler);

	/* Setup button interrupt for both edges */
	err = gpio_pin_interrupt_configure_dt(button_spec, GPIO_INT_EDGE_BOTH);
	if (err) {
		LOG_ERR("Failed to configure button interrupt: %d", err);
		return err;
	}

	gpio_init_callback(&button_cb_data, button_isr, BIT(button_spec->pin));
	err = gpio_add_callback(button_spec->port, &button_cb_data);
	if (err) {
		LOG_ERR("Failed to add button callback: %d", err);
		return err;
	}

	LOG_INF("Button handler initialized (debounce=%dms, long_press=%dms)",
		DEBOUNCE_MS, FACTORY_RESET_TIME_MS);
	return 0;
}
