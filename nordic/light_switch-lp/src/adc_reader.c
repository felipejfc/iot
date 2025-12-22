/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/**
 * @file adc_reader.c
 * @brief ADC voltage reader implementation for P0.04 (AIN2)
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>
#include <errno.h>

#include "adc_reader.h"
#include "zigbee_device.h"

LOG_MODULE_REGISTER(adc_reader, LOG_LEVEL_INF);

#ifdef CONFIG_ADC

#include <zephyr/drivers/adc.h>

/* Reading interval from Kconfig (60s for low-power, 10s for development) */
#define ADC_READING_INTERVAL_MS (CONFIG_ADC_READING_INTERVAL_SEC * 1000)

/* Oversampling configuration for noise reduction */
#define ADC_SAMPLE_DELAY_US     100     /* Delay between samples in microseconds */

#if !DT_NODE_EXISTS(DT_PATH(zephyr_user)) || \
    !DT_NODE_HAS_PROP(DT_PATH(zephyr_user), io_channels)
#error "No suitable devicetree overlay specified for ADC"
#endif

#define DT_SPEC_AND_COMMA(node_id, prop, idx) \
	ADC_DT_SPEC_GET_BY_IDX(node_id, idx),

/* ADC channels defined in devicetree */
static const struct adc_dt_spec adc_channel = ADC_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), 0);

/* Buffer for ADC sample */
static int16_t adc_buf;

/* ADC sequence configuration */
static struct adc_sequence sequence = {
	.buffer = &adc_buf,
	.buffer_size = sizeof(adc_buf),
};

int adc_reader_init(void)
{
	int err;

	if (!adc_is_ready_dt(&adc_channel)) {
		LOG_ERR("ADC controller device %s not ready", adc_channel.dev->name);
		return -ENODEV;
	}

	err = adc_channel_setup_dt(&adc_channel);
	if (err < 0) {
		LOG_ERR("Could not setup ADC channel (%d)", err);
		return err;
	}

	LOG_INF("ADC initialized on channel %d", adc_channel.channel_id);

	return 0;
}

int adc_read_raw(int16_t *raw_value)
{
	int err;

	if (raw_value == NULL) {
		return -EINVAL;
	}

	err = adc_sequence_init_dt(&adc_channel, &sequence);
	if (err < 0) {
		LOG_ERR("Could not initialize ADC sequence (%d)", err);
		return err;
	}

	err = adc_read_dt(&adc_channel, &sequence);
	if (err < 0) {
		LOG_ERR("Could not read ADC (%d)", err);
		return err;
	}

	*raw_value = adc_buf;

	return 0;
}

int adc_read_voltage_mv(int32_t *voltage_mv)
{
	int err;
	int16_t raw;
	int32_t sum = 0;
	int valid_samples = 0;

	if (voltage_mv == NULL) {
		return -EINVAL;
	}

	/* Take multiple samples and average to reduce noise */
	for (int i = 0; i < CONFIG_ADC_OVERSAMPLE_COUNT; i++) {
		err = adc_read_raw(&raw);
		if (err == 0) {
			sum += raw;
			valid_samples++;
		}

		/* Small delay between samples to let ADC settle */
		if (i < CONFIG_ADC_OVERSAMPLE_COUNT - 1) {
			k_busy_wait(ADC_SAMPLE_DELAY_US);
		}
	}

	if (valid_samples == 0) {
		LOG_ERR("All ADC samples failed");
		return -EIO;
	}

	/* Calculate average raw value */
	int16_t avg_raw = (int16_t)(sum / valid_samples);

	/* Convert averaged raw value to millivolts */
	*voltage_mv = (int32_t)avg_raw;
	err = adc_raw_to_millivolts_dt(&adc_channel, voltage_mv);
	if (err < 0) {
		LOG_ERR("Could not convert to millivolts (%d)", err);
		return err;
	}

	/* VDDHDIV5 divides VDDH by 5, so multiply to get actual voltage */
	*voltage_mv *= 5;

	LOG_DBG("ADC: %d samples, avg raw=%d, VDDH=%d mV",
		valid_samples, avg_raw, *voltage_mv);

	return 0;
}

/* Periodic reading work item and timer */
static struct k_work_delayable adc_work;
static bool periodic_reading_enabled = false;

static void adc_work_handler(struct k_work *work)
{
	int32_t voltage_mv;
	int err;

	err = adc_read_voltage_mv(&voltage_mv);
	if (err == 0) {
		/* Update Zigbee battery attribute with new voltage reading */
		zigbee_device_update_battery(voltage_mv);
	} else {
		LOG_WRN("Failed to read ADC voltage: %d", err);
	}

	/* Schedule next reading if still enabled */
	if (periodic_reading_enabled) {
		k_work_schedule(&adc_work, K_MSEC(ADC_READING_INTERVAL_MS));
	}
}

int adc_start_periodic_reading(void)
{
	if (periodic_reading_enabled) {
		return 0; /* Already running */
	}

	k_work_init_delayable(&adc_work, adc_work_handler);
	periodic_reading_enabled = true;

	/* Take first reading immediately */
	k_work_schedule(&adc_work, K_NO_WAIT);

	LOG_INF("ADC periodic reading started (interval: %d sec)", CONFIG_ADC_READING_INTERVAL_SEC);

	return 0;
}

void adc_stop_periodic_reading(void)
{
	periodic_reading_enabled = false;
	k_work_cancel_delayable(&adc_work);
	LOG_INF("ADC periodic reading stopped");
}

#else /* !CONFIG_ADC */

/* Stub implementations when ADC is disabled */

int adc_reader_init(void)
{
	LOG_INF("ADC disabled");
	return 0;
}

int adc_read_raw(int16_t *raw_value)
{
	return -ENOTSUP;
}

int adc_read_voltage_mv(int32_t *voltage_mv)
{
	return -ENOTSUP;
}

int adc_start_periodic_reading(void)
{
	return 0;
}

void adc_stop_periodic_reading(void)
{
}

#endif /* CONFIG_ADC */
