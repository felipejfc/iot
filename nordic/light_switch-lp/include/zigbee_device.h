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
#define VOLTAGE_SENSOR_ENDPOINT    2

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
 * @brief Update voltage sensor reading and Zigbee attribute
 *
 * Updates the Analog Input cluster present_value attribute with the
 * measured voltage. Value is stored in centivolts (e.g., 330 = 3.30V).
 *
 * @param voltage_mv Voltage in millivolts
 */
void zigbee_device_update_voltage(int32_t voltage_mv);

/**
 * @brief Get current voltage reading in centivolts
 *
 * @return Voltage in centivolts (e.g., 330 = 3.30V)
 */
int16_t zigbee_device_get_voltage_centivolts(void);

/**
 * @brief Set network joined status
 *
 * Called from signal handler when network join status changes.
 * Voltage reports are only sent when joined.
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
