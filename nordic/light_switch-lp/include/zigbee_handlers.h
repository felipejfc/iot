/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/**
 * @file zigbee_handlers.h
 * @brief Zigbee stack event handlers and identify callbacks
 */

#ifndef ZIGBEE_HANDLERS_H
#define ZIGBEE_HANDLERS_H

#include <zboss_api.h>

/**
 * @brief Initialize Zigbee FOTA if enabled
 *
 * Should be called before zigbee_enable().
 */
void zigbee_handlers_init(void);

/**
 * @brief Identify notification callback
 *
 * Handles identify mode - blinks LED when in identify mode.
 * This is registered as the identify notification handler for endpoints.
 *
 * @param bufid Buffer ID from ZBOSS scheduler
 */
void identify_cb(zb_bufid_t bufid);

#endif /* ZIGBEE_HANDLERS_H */
