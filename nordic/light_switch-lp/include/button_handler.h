/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/**
 * @file button_handler.h
 * @brief Button input handling with debounce and factory reset support
 */

#ifndef BUTTON_HANDLER_H
#define BUTTON_HANDLER_H

#include <stdbool.h>

/**
 * @brief Button event callback type
 *
 * Called when a button press event is processed (after debounce).
 * The callback runs in work queue context (not ISR).
 *
 * @param long_press true if this was a long press (factory reset), false for short press
 */
typedef void (*button_event_cb_t)(bool long_press);

/**
 * @brief Initialize button handler
 *
 * Sets up button interrupt, debounce timer, and factory reset timer.
 * Must be called after gpio_control_init().
 *
 * @param callback Function to call on button events (can be NULL)
 * @return 0 on success, negative error code on failure
 */
int button_handler_init(button_event_cb_t callback);

#endif /* BUTTON_HANDLER_H */
