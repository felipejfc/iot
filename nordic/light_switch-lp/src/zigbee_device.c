/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/**
 * @file zigbee_device.c
 * @brief Zigbee device configuration and ZCL cluster definitions
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <zboss_api.h>
#include <zboss_api_addons.h>
#include <zigbee/zigbee_app_utils.h>
#include <zigbee/zigbee_error_handler.h>
#include <zcl/zb_zcl_power_config.h>

#include "zigbee_device.h"
#include "zigbee_handlers.h"
#include "gpio_control.h"

#if CONFIG_ZIGBEE_FOTA
#include <zigbee/zigbee_fota.h>
#endif

LOG_MODULE_REGISTER(zigbee_device, LOG_LEVEL_INF);

/* Relay context */
struct relay_context {
	bool relay_state;  /* Current relay state: true = ON, false = OFF */
};

/* Relay endpoint device context (simpler - just On/Off server) */
struct zb_relay_ctx {
	zb_zcl_basic_attrs_t basic_attr;
	zb_zcl_identify_attrs_t identify_attr;
	zb_zcl_on_off_attrs_t on_off_attr;
	zb_char_t manufacturer_name[17];
	zb_char_t model_id[17];
};

/* Power Configuration cluster attributes for battery reporting */
static zb_uint16_t battery_voltage;           /* Units of 10mV (e.g., 406 = 4.06V) */
static zb_uint8_t battery_percentage;         /* Half-percent units (200 = 100%) */
static zb_uint16_t battery_voltage_last_reported;  /* For threshold reporting */

/* Li-ion battery voltage range for percentage calculation */
#define BATTERY_MIN_MV  3000  /* 3.0V = 0% */
#define BATTERY_MAX_MV  4200  /* 4.2V = 100% */
#define BATTERY_REPORT_THRESHOLD  5  /* Report when voltage changes by 50mV (5 x 10mV units) */

static struct relay_context relay_ctx;
static struct zb_relay_ctx relay_dev_ctx;

/* Network join status - only send reports when joined */
static bool network_joined = false;

/* Forward declaration */
static void zcl_device_cb(zb_bufid_t bufid);
static zb_uint8_t zcl_on_off_handler(zb_bufid_t bufid);

/* =============================================================================
 * RELAY ENDPOINT (EP 2) - On/Off Switch device type
 * =============================================================================
 */

/* Declare attribute list for Basic cluster (server) for relay endpoint */
#define ZB_ZCL_BASIC_RELAY_ATTRIB_LIST                                           \
	ZB_ZCL_START_DECLARE_ATTRIB_LIST_CLUSTER_REVISION(relay_basic_attr_list, ZB_ZCL_BASIC) \
	ZB_ZCL_SET_ATTR_DESC(ZB_ZCL_ATTR_BASIC_ZCL_VERSION_ID, (&relay_dev_ctx.basic_attr.zcl_version)) \
	ZB_ZCL_SET_ATTR_DESC(ZB_ZCL_ATTR_BASIC_MANUFACTURER_NAME_ID, (relay_dev_ctx.manufacturer_name)) \
	ZB_ZCL_SET_ATTR_DESC(ZB_ZCL_ATTR_BASIC_MODEL_IDENTIFIER_ID, (relay_dev_ctx.model_id)) \
	ZB_ZCL_SET_ATTR_DESC(ZB_ZCL_ATTR_BASIC_POWER_SOURCE_ID, (&relay_dev_ctx.basic_attr.power_source)) \
	ZB_ZCL_FINISH_DECLARE_ATTRIB_LIST

ZB_ZCL_BASIC_RELAY_ATTRIB_LIST;

/* Declare attribute list for Identify cluster (server) for relay endpoint */
ZB_ZCL_DECLARE_IDENTIFY_SERVER_ATTRIB_LIST(
	relay_identify_server_attr_list,
	&relay_dev_ctx.identify_attr.identify_time);

/* Power Configuration cluster attribute list - using custom U16 for voltage (10mV units) */
static zb_uint16_t power_config_cluster_revision = ZB_ZCL_POWER_CONFIG_CLUSTER_REVISION_DEFAULT;

static zb_zcl_attr_t relay_power_config_attr_list[] = {
	{
		ZB_ZCL_ATTR_POWER_CONFIG_BATTERY_VOLTAGE_ID,
		ZB_ZCL_ATTR_TYPE_U16,  /* Custom: U16 in 10mV units instead of U8 in 100mV */
		ZB_ZCL_ATTR_ACCESS_READ_ONLY | ZB_ZCL_ATTR_ACCESS_REPORTING,
		ZB_ZCL_NON_MANUFACTURER_SPECIFIC,
		(void *)&battery_voltage
	},
	{
		ZB_ZCL_ATTR_POWER_CONFIG_BATTERY_PERCENTAGE_REMAINING_ID,
		ZB_ZCL_ATTR_TYPE_U8,
		ZB_ZCL_ATTR_ACCESS_READ_ONLY | ZB_ZCL_ATTR_ACCESS_REPORTING,
		ZB_ZCL_NON_MANUFACTURER_SPECIFIC,
		(void *)&battery_percentage
	},
	{
		ZB_ZCL_ATTR_GLOBAL_CLUSTER_REVISION_ID,
		ZB_ZCL_ATTR_TYPE_U16,
		ZB_ZCL_ATTR_ACCESS_READ_ONLY,
		ZB_ZCL_NON_MANUFACTURER_SPECIFIC,
		(void *)&power_config_cluster_revision
	},
	{
		ZB_ZCL_NULL_ID,
		0,
		0,
		0,
		NULL
	}
};

/* Declare attribute list for On/Off cluster (server) for relay endpoint */
ZB_ZCL_DECLARE_ON_OFF_ATTRIB_LIST(
	relay_on_off_server_attr_list,
	&relay_dev_ctx.on_off_attr.on_off);

/* Declare cluster list for Relay endpoint - simple On/Off Output device */
zb_zcl_cluster_desc_t relay_switch_clusters[] =
{
	ZB_ZCL_CLUSTER_DESC(
		ZB_ZCL_CLUSTER_ID_BASIC,
		ZB_ZCL_ARRAY_SIZE(relay_basic_attr_list, zb_zcl_attr_t),
		(relay_basic_attr_list),
		ZB_ZCL_CLUSTER_SERVER_ROLE,
		ZB_ZCL_MANUF_CODE_INVALID
	),
	ZB_ZCL_CLUSTER_DESC(
		ZB_ZCL_CLUSTER_ID_IDENTIFY,
		ZB_ZCL_ARRAY_SIZE(relay_identify_server_attr_list, zb_zcl_attr_t),
		(relay_identify_server_attr_list),
		ZB_ZCL_CLUSTER_SERVER_ROLE,
		ZB_ZCL_MANUF_CODE_INVALID
	),
	ZB_ZCL_CLUSTER_DESC(
		ZB_ZCL_CLUSTER_ID_ON_OFF,
		ZB_ZCL_ARRAY_SIZE(relay_on_off_server_attr_list, zb_zcl_attr_t),
		(relay_on_off_server_attr_list),
		ZB_ZCL_CLUSTER_SERVER_ROLE,
		ZB_ZCL_MANUF_CODE_INVALID
	),
	ZB_ZCL_CLUSTER_DESC(
		ZB_ZCL_CLUSTER_ID_POWER_CONFIG,
		3,  /* 3 attributes: voltage, percentage, cluster revision */
		(relay_power_config_attr_list),
		ZB_ZCL_CLUSTER_SERVER_ROLE,
		ZB_ZCL_MANUF_CODE_INVALID
	)
};

/* Declare simple descriptor type for relay endpoint (4 server clusters, 0 client clusters) */
ZB_DECLARE_SIMPLE_DESC(4, 0);

ZB_AF_SIMPLE_DESC_TYPE(4, 0) simple_desc_relay_switch_ep = {
	RELAY_SWITCH_ENDPOINT,                /* Endpoint ID */
	ZB_AF_HA_PROFILE_ID,                  /* Application profile identifier */
	ZB_HA_ON_OFF_OUTPUT_DEVICE_ID,        /* Device ID - On/Off Output */
	0,                                     /* Device version */
	0,                                     /* Reserved */
	4,                                     /* Number of input (server) clusters */
	0,                                     /* Number of output (client) clusters */
	{
		ZB_ZCL_CLUSTER_ID_BASIC,           /* Server: Basic */
		ZB_ZCL_CLUSTER_ID_IDENTIFY,        /* Server: Identify */
		ZB_ZCL_CLUSTER_ID_ON_OFF,          /* Server: On/Off */
		ZB_ZCL_CLUSTER_ID_POWER_CONFIG,    /* Server: Power Configuration */
	}
};

/* Declare relay endpoint descriptor */
ZB_AF_DECLARE_ENDPOINT_DESC(
	relay_switch_ep,
	RELAY_SWITCH_ENDPOINT,
	ZB_AF_HA_PROFILE_ID,
	0,
	NULL,
	ZB_ZCL_ARRAY_SIZE(relay_switch_clusters, zb_zcl_cluster_desc_t),
	relay_switch_clusters,
	(zb_af_simple_desc_1_1_t *)&simple_desc_relay_switch_ep,
	0, NULL, /* No reporting ctx - battery reports sent manually */
	0, NULL  /* No CVC ctx */
);

/* Declare application's device context (list of registered endpoints) */
#ifndef CONFIG_ZIGBEE_FOTA
ZBOSS_DECLARE_DEVICE_CTX_1_EP(device_ctx, relay_switch_ep);
#else
  #if RELAY_SWITCH_ENDPOINT == CONFIG_ZIGBEE_FOTA_ENDPOINT
    #error "Relay switch and Zigbee OTA endpoints should be different."
  #endif

extern zb_af_endpoint_desc_t zigbee_fota_client_ep;
ZBOSS_DECLARE_DEVICE_CTX_2_EP(device_ctx,
			      zigbee_fota_client_ep,
			      relay_switch_ep);
#endif /* CONFIG_ZIGBEE_FOTA */

/**@brief Callback for handling ZCL On/Off commands. */
static zb_uint8_t zcl_on_off_handler(zb_bufid_t bufid)
{
	zb_zcl_device_callback_param_t *device_cb_param =
		ZB_BUF_GET_PARAM(bufid, zb_zcl_device_callback_param_t);

	LOG_DBG("ZCL callback ID: %d, endpoint: %d", device_cb_param->device_cb_id,
		device_cb_param->endpoint);

	if (device_cb_param->device_cb_id == ZB_ZCL_SET_ATTR_VALUE_CB_ID) {
		if (device_cb_param->cb_param.set_attr_value_param.cluster_id == ZB_ZCL_CLUSTER_ID_ON_OFF &&
		    device_cb_param->cb_param.set_attr_value_param.attr_id == ZB_ZCL_ATTR_ON_OFF_ON_OFF_ID) {
			zb_bool_t new_value = device_cb_param->cb_param.set_attr_value_param.values.data8;

			if (device_cb_param->endpoint == RELAY_SWITCH_ENDPOINT) {
				LOG_INF("Zigbee On/Off command for Relay: %s", new_value ? "ON" : "OFF");
				relay_ctx.relay_state = (new_value == ZB_TRUE);
				relay_control_set(relay_ctx.relay_state);
			} else {
				LOG_WRN("Unknown endpoint: %d", device_cb_param->endpoint);
				device_cb_param->status = RET_ERROR;
				return ZB_FALSE;
			}

			device_cb_param->status = RET_OK;
			return ZB_TRUE;
		}
	}

	return ZB_FALSE;
}

#ifdef CONFIG_ZIGBEE_FOTA
static void zcl_device_cb(zb_bufid_t bufid)
{
	zb_zcl_device_callback_param_t *device_cb_param =
		ZB_BUF_GET_PARAM(bufid, zb_zcl_device_callback_param_t);

	if (zcl_on_off_handler(bufid)) {
		return;
	}

	if (device_cb_param->device_cb_id == ZB_ZCL_OTA_UPGRADE_VALUE_CB_ID) {
		zigbee_fota_zcl_cb(bufid);
	} else {
		device_cb_param->status = RET_NOT_IMPLEMENTED;
	}
}
#else
static void zcl_device_cb(zb_bufid_t bufid)
{
	if (zcl_on_off_handler(bufid)) {
		return;
	}

	zb_zcl_device_callback_param_t *device_cb_param =
		ZB_BUF_GET_PARAM(bufid, zb_zcl_device_callback_param_t);
	device_cb_param->status = RET_NOT_IMPLEMENTED;
}
#endif /* CONFIG_ZIGBEE_FOTA */

void zigbee_device_init(void)
{
	/* Initialize relay state - start with relay OFF */
	relay_ctx.relay_state = false;
	relay_control_set(relay_ctx.relay_state);

	/* Basic cluster attributes data for relay endpoint */
	relay_dev_ctx.basic_attr.zcl_version = ZB_ZCL_VERSION;
	relay_dev_ctx.basic_attr.power_source = ZB_ZCL_BASIC_POWER_SOURCE_BATTERY;

	ZB_ZCL_SET_STRING_VAL(relay_dev_ctx.manufacturer_name,
			      (zb_uint8_t *)"FCApps",
			      ZB_ZCL_STRING_CONST_SIZE("FCApps"));

	ZB_ZCL_SET_STRING_VAL(relay_dev_ctx.model_id,
			      (zb_uint8_t *)"Smart Relay v1",
			      ZB_ZCL_STRING_CONST_SIZE("Smart Relay v1"));

	/* On/Off cluster attributes for relay - sync with relay state. */
	relay_dev_ctx.on_off_attr.on_off = relay_ctx.relay_state ? ZB_TRUE : ZB_FALSE;

	/* Identify cluster attributes data for relay. */
	relay_dev_ctx.identify_attr.identify_time = ZB_ZCL_IDENTIFY_IDENTIFY_TIME_DEFAULT_VALUE;

	/* Power Configuration cluster attributes - initial battery state unknown */
	battery_voltage = ZB_ZCL_POWER_CONFIG_BATTERY_VOLTAGE_INVALID;
	battery_percentage = ZB_ZCL_POWER_CONFIG_BATTERY_REMAINING_UNKNOWN;
	battery_voltage_last_reported = 0;

	LOG_INF("Power Configuration attributes initialized");
}

void zigbee_device_register(void)
{
	/* Register callback for handling ZCL commands */
	ZB_ZCL_REGISTER_DEVICE_CB(zcl_device_cb);

	/* Register device context (endpoints) */
	ZB_AF_REGISTER_DEVICE_CTX(&device_ctx);

	LOG_INF("Registered Zigbee endpoint: EP%d (Relay)", RELAY_SWITCH_ENDPOINT);

	/* Register handlers to identify notifications */
	ZB_AF_SET_IDENTIFY_NOTIFICATION_HANDLER(RELAY_SWITCH_ENDPOINT, identify_cb);
#ifdef CONFIG_ZIGBEE_FOTA
	ZB_AF_SET_IDENTIFY_NOTIFICATION_HANDLER(CONFIG_ZIGBEE_FOTA_ENDPOINT, identify_cb);
#endif
}

void zigbee_device_set_relay(bool on)
{
	relay_ctx.relay_state = on;
	relay_control_set(relay_ctx.relay_state);

	/* Update Zigbee On/Off attribute for relay endpoint */
	zb_uint8_t new_value = on ? ZB_TRUE : ZB_FALSE;
	zb_zcl_status_t status = zb_zcl_set_attr_val(
		RELAY_SWITCH_ENDPOINT,
		ZB_ZCL_CLUSTER_ID_ON_OFF,
		ZB_ZCL_CLUSTER_SERVER_ROLE,
		ZB_ZCL_ATTR_ON_OFF_ON_OFF_ID,
		&new_value,
		ZB_FALSE);

	if (status != ZB_ZCL_STATUS_SUCCESS) {
		LOG_ERR("Failed to update relay On/Off attribute: %d", status);
	}
}

bool zigbee_device_toggle_relay(void)
{
	zigbee_device_set_relay(!relay_ctx.relay_state);
	LOG_INF("Relay toggled to %s", relay_ctx.relay_state ? "ON" : "OFF");
	return relay_ctx.relay_state;
}

bool zigbee_device_get_relay_state(void)
{
	return relay_ctx.relay_state;
}

void zigbee_device_set_network_joined(bool joined)
{
	network_joined = joined;
	LOG_INF("Network joined status: %s", joined ? "true" : "false");
}

bool zigbee_device_is_network_joined(void)
{
	return network_joined;
}

/* Send battery attribute report to coordinator */
static void send_battery_report(zb_bufid_t bufid)
{
	zb_uint8_t *cmd_ptr;
	zb_uint16_t dst_addr = 0x0000;  /* Coordinator address */

	/* Start building the ZCL packet */
	cmd_ptr = ZB_ZCL_START_PACKET(bufid);

	/* Build frame control for Report Attributes command */
	ZB_ZCL_CONSTRUCT_GENERAL_COMMAND_REQ_FRAME_CONTROL_A(
		cmd_ptr,
		ZB_ZCL_FRAME_DIRECTION_TO_CLI,
		ZB_ZCL_NOT_MANUFACTURER_SPECIFIC,
		ZB_ZCL_DISABLE_DEFAULT_RESPONSE);

	/* Build command header with Report Attributes command ID (0x0A) */
	ZB_ZCL_CONSTRUCT_COMMAND_HEADER(
		cmd_ptr,
		ZB_ZCL_GET_SEQ_NUM(),
		ZB_ZCL_CMD_REPORT_ATTRIB);

	/* Add battery voltage attribute report (0x0020) - U16 in 10mV units */
	ZB_ZCL_PACKET_PUT_DATA16_VAL(cmd_ptr, ZB_ZCL_ATTR_POWER_CONFIG_BATTERY_VOLTAGE_ID);
	ZB_ZCL_PACKET_PUT_DATA8(cmd_ptr, ZB_ZCL_ATTR_TYPE_U16);
	ZB_ZCL_PACKET_PUT_DATA16_VAL(cmd_ptr, battery_voltage);

	/* Add battery percentage attribute report (0x0021) */
	ZB_ZCL_PACKET_PUT_DATA16_VAL(cmd_ptr, ZB_ZCL_ATTR_POWER_CONFIG_BATTERY_PERCENTAGE_REMAINING_ID);
	ZB_ZCL_PACKET_PUT_DATA8(cmd_ptr, ZB_ZCL_ATTR_TYPE_U8);
	ZB_ZCL_PACKET_PUT_DATA8(cmd_ptr, battery_percentage);

	/* Finish and send the packet */
	ZB_ZCL_FINISH_N_SEND_PACKET(
		bufid,
		cmd_ptr,
		dst_addr,
		ZB_APS_ADDR_MODE_16_ENDP_PRESENT,
		1,  /* Destination endpoint (coordinator) */
		RELAY_SWITCH_ENDPOINT,
		ZB_AF_HA_PROFILE_ID,
		ZB_ZCL_CLUSTER_ID_POWER_CONFIG,
		NULL);
}

static void battery_report_cb(zb_uint8_t param)
{
	zb_bufid_t bufid;

	if (param) {
		bufid = param;
	} else {
		bufid = zb_buf_get_out();
	}

	if (!bufid) {
		LOG_WRN("No buffer for battery report");
		return;
	}

	send_battery_report(bufid);
	LOG_INF("Battery report sent: %d.%02d V, %d%%",
		battery_voltage / 100, battery_voltage % 100, battery_percentage / 2);
}

void zigbee_device_update_battery(int32_t voltage_mv)
{
	/* Convert to 10mV units (e.g., 4060mV -> 406) */
	zb_uint16_t new_voltage = (zb_uint16_t)(voltage_mv / 10);

	/* Calculate percentage: Li-ion 3.0V-4.2V range */
	int32_t pct;
	if (voltage_mv <= BATTERY_MIN_MV) {
		pct = 0;
	} else if (voltage_mv >= BATTERY_MAX_MV) {
		pct = 100;
	} else {
		pct = ((voltage_mv - BATTERY_MIN_MV) * 100) / (BATTERY_MAX_MV - BATTERY_MIN_MV);
	}

	/* Convert to half-percent units (200 = 100%) */
	zb_uint8_t new_percentage = (zb_uint8_t)(pct * 2);

	/* Calculate voltage difference for threshold check */
	zb_int16_t diff = (zb_int16_t)(new_voltage - battery_voltage_last_reported);
	if (diff < 0) {
		diff = -diff;
	}

	/* Update attributes */
	battery_voltage = new_voltage;
	battery_percentage = new_percentage;

	LOG_DBG("Battery: %d.%02d V (%d units), %d%% (diff=%d)",
		voltage_mv / 1000, (voltage_mv % 1000) / 10,
		battery_voltage, pct, diff);

	/* Send report if change exceeds threshold and network is joined */
	if (diff >= BATTERY_REPORT_THRESHOLD) {
		battery_voltage_last_reported = battery_voltage;
		if (network_joined) {
			ZB_SCHEDULE_APP_CALLBACK(battery_report_cb, 0);
			LOG_INF("Battery changed: %d.%02d V, %d%%",
				voltage_mv / 1000, (voltage_mv % 1000) / 10, pct);
		} else {
			LOG_DBG("Battery change detected but network not joined, skipping report");
		}
	}
}
