/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/**
 * @file zigbee_device.h
 * @brief Zigbee device configuration and ZCL cluster definitions
 */

#ifndef ZIGBEE_DEVICE_H
#define ZIGBEE_DEVICE_H

#include <zboss_api.h>

/* Source endpoint for Zigbee device */
#define RELAY_SWITCH_ENDPOINT      1

/* Do not erase NVRAM to save the network parameters after device reboot or
 * power-off. NOTE: If this option is set to ZB_TRUE then do full device erase
 * for all network devices before running other samples.
 */
#define ERASE_PERSISTENT_CONFIG    ZB_FALSE

/**
 * @brief Initialize Zigbee device clusters and attributes
 *
 * Sets up all ZCL cluster attributes with default values.
 */
void zigbee_device_init(void);

/**
 * @brief Register Zigbee device context and callbacks
 *
 * Registers the device context, ZCL callback, and identify handlers.
 */
void zigbee_device_register(void);

/**
 * @brief Set relay state and update Zigbee On/Off attribute
 *
 * @param on true to turn relay on, false to turn off
 */
void zigbee_device_set_relay(bool on);

/**
 * @brief Toggle relay state and update Zigbee On/Off attribute
 *
 * @return New relay state after toggle
 */
bool zigbee_device_toggle_relay(void);

/**
 * @brief Get current relay state
 *
 * @return true if relay is on, false otherwise
 */
bool zigbee_device_get_relay_state(void);

/**
 * @brief Update battery level and report to Zigbee2MQTT
 *
 * Updates the Power Configuration cluster attributes:
 * - Battery voltage (0x0020) in units of 100mV
 * - Battery percentage (0x0021) in half-percent units (200 = 100%)
 *
 * Sends attribute report if voltage change exceeds threshold.
 * Uses Li-ion battery curve: 3.0V = 0%, 4.2V = 100%
 *
 * @param voltage_mv Battery voltage in millivolts
 */
void zigbee_device_update_battery(int32_t voltage_mv);

/**
 * @brief Set network joined status
 *
 * Called from signal handler when network join status changes.
 * Battery reports are only sent when joined.
 *
 * @param joined true if device has joined network, false otherwise
 */
void zigbee_device_set_network_joined(bool joined);

/**
 * @brief Check if device is joined to network
 *
 * @return true if device has joined network, false otherwise
 */
bool zigbee_device_is_network_joined(void);

#endif /* ZIGBEE_DEVICE_H */
