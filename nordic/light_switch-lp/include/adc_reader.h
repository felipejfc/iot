/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/**
 * @file adc_reader.h
 * @brief ADC voltage reader interface for P0.04 (AIN2)
 *
 * Reads voltage from P0.04, calibrated for 1.5V to 3.3V range.
 */

#ifndef ADC_READER_H
#define ADC_READER_H

#include <stdint.h>

/**
 * @brief Initialize the ADC for voltage reading
 *
 * Configures ADC channel 2 (P0.04/AIN2) for voltage measurement.
 *
 * @return 0 on success, negative error code on failure
 */
int adc_reader_init(void);

/**
 * @brief Read raw ADC value
 *
 * @param[out] raw_value Pointer to store the raw 12-bit ADC value
 * @return 0 on success, negative error code on failure
 */
int adc_read_raw(int16_t *raw_value);

/**
 * @brief Read voltage in millivolts
 *
 * Converts the raw ADC reading to millivolts.
 * Valid range is approximately 0-3600mV with internal reference and 1/6 gain.
 *
 * @param[out] voltage_mv Pointer to store the voltage in millivolts
 * @return 0 on success, negative error code on failure
 */
int adc_read_voltage_mv(int32_t *voltage_mv);

/**
 * @brief Start periodic ADC voltage readings
 *
 * Starts a periodic work item that reads voltage at the configured interval
 * (CONFIG_ADC_READING_INTERVAL_SEC) and updates the Zigbee attribute.
 *
 * @return 0 on success, negative error code on failure
 */
int adc_start_periodic_reading(void);

/**
 * @brief Stop periodic ADC voltage readings
 */
void adc_stop_periodic_reading(void);

#endif /* ADC_READER_H */
