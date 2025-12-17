/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/**
 * @file gpio_control.h
 * @brief GPIO control interface for LEDs and button
 */

#ifndef GPIO_CONTROL_H
#define GPIO_CONTROL_H

#include <stdbool.h>

/**
 * @brief Initialize all GPIO pins (relay, MOSFET, LEDs, and button)
 *
 * Configures relay and MOSFET outputs, power LED, and button input.
 * Does not setup button interrupt.
 *
 * @return 0 on success, negative error code on failure
 */
int gpio_control_init(void);

/**
 * @brief Set the power/status LED state
 *
 * @param on true to turn LED on, false to turn off
 */
void led_power_set(bool on);

/**
 * @brief Set the relay state
 *
 * Controls the relay output on P0.29 (active high).
 *
 * @param on true to turn relay on, false to turn off
 */
void relay_control_set(bool on);

#ifndef CONFIG_DK_LIBRARY
/**
 * @brief Get current button state
 *
 * @return true if button is pressed (logical active), false otherwise
 */
bool button_get_state(void);

/**
 * @brief Get the button GPIO device tree spec
 *
 * Used by button_handler to configure interrupts.
 *
 * @return Pointer to the button GPIO spec
 */
const struct gpio_dt_spec *button_get_dt_spec(void);
#endif

#endif /* GPIO_CONTROL_H */
