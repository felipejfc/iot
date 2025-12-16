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

#include "zb_dimmer_switch.h"
#include "zigbee_device.h"
#include "zigbee_handlers.h"
#include "gpio_control.h"

#if CONFIG_ZIGBEE_FOTA
#include <zigbee/zigbee_fota.h>
#endif

LOG_MODULE_REGISTER(zigbee_device, LOG_LEVEL_INF);

/* LED context */
struct led_context {
	bool led_state;  /* Current LED state: true = ON, false = OFF */
};

/* Relay context */
struct relay_context {
	bool relay_state;  /* Current relay state: true = ON, false = OFF */
};

struct zb_device_ctx {
	zb_zcl_basic_attrs_t basic_attr;
	zb_zcl_identify_attrs_t identify_attr;
	zb_zcl_on_off_attrs_t on_off_attr;
	zb_char_t manufacturer_name[17];
	zb_char_t model_id[17];
};

/* Relay endpoint device context (simpler - just On/Off server) */
struct zb_relay_ctx {
	zb_zcl_basic_attrs_t basic_attr;
	zb_zcl_identify_attrs_t identify_attr;
	zb_zcl_on_off_attrs_t on_off_attr;
	zb_char_t manufacturer_name[17];
	zb_char_t model_id[17];
};

/* Voltage sensor endpoint device context */
struct zb_voltage_ctx {
	zb_zcl_basic_attrs_t basic_attr;
	zb_zcl_identify_attrs_t identify_attr;
	zb_char_t manufacturer_name[17];
	zb_char_t model_id[17];
};

/* Analog Input cluster attribute values - separate statics for compile-time initialization */
static zb_int16_t voltage_present_value;      /* Voltage in centivolts (100 = 1.00V) */
static zb_int16_t voltage_last_reported;      /* Last reported value for threshold check */
static zb_uint8_t voltage_status_flags;        /* Status flags */

/* Minimum voltage change (in centivolts) required to trigger a report */
#define VOLTAGE_REPORT_THRESHOLD_CV  5  /* 50mV = 5 centivolts */

static struct led_context led_ctx;
static struct relay_context relay_ctx;
static struct zb_device_ctx dev_ctx;
static struct zb_relay_ctx relay_dev_ctx;
static struct zb_voltage_ctx voltage_dev_ctx;

/* Forward declaration */
static void zcl_device_cb(zb_bufid_t bufid);
static zb_uint8_t zcl_on_off_handler(zb_bufid_t bufid);

/* Declare attribute list for Basic cluster (server) with manufacturer info. */
#define ZB_ZCL_BASIC_MANUF_SPECIFIC_ATTRIB_LIST                                 \
	ZB_ZCL_START_DECLARE_ATTRIB_LIST_CLUSTER_REVISION(basic_server_attr_list, ZB_ZCL_BASIC) \
	ZB_ZCL_SET_ATTR_DESC(ZB_ZCL_ATTR_BASIC_ZCL_VERSION_ID, (&dev_ctx.basic_attr.zcl_version)) \
	ZB_ZCL_SET_ATTR_DESC(ZB_ZCL_ATTR_BASIC_MANUFACTURER_NAME_ID, (dev_ctx.manufacturer_name)) \
	ZB_ZCL_SET_ATTR_DESC(ZB_ZCL_ATTR_BASIC_MODEL_IDENTIFIER_ID, (dev_ctx.model_id)) \
	ZB_ZCL_SET_ATTR_DESC(ZB_ZCL_ATTR_BASIC_POWER_SOURCE_ID, (&dev_ctx.basic_attr.power_source)) \
	ZB_ZCL_FINISH_DECLARE_ATTRIB_LIST

ZB_ZCL_BASIC_MANUF_SPECIFIC_ATTRIB_LIST;

/* Declare attribute list for Identify cluster (client). */
ZB_ZCL_DECLARE_IDENTIFY_CLIENT_ATTRIB_LIST(
	identify_client_attr_list);

/* Declare attribute list for Identify cluster (server). */
ZB_ZCL_DECLARE_IDENTIFY_SERVER_ATTRIB_LIST(
	identify_server_attr_list,
	&dev_ctx.identify_attr.identify_time);

/* Declare attribute list for Scenes cluster (client). */
ZB_ZCL_DECLARE_SCENES_CLIENT_ATTRIB_LIST(
	scenes_client_attr_list);

/* Declare attribute list for Groups cluster (client). */
ZB_ZCL_DECLARE_GROUPS_CLIENT_ATTRIB_LIST(
	groups_client_attr_list);

/* Declare attribute list for On/Off cluster (client). */
ZB_ZCL_DECLARE_ON_OFF_CLIENT_ATTRIB_LIST(
	on_off_client_attr_list);

/* Declare attribute list for On/Off cluster (server) - to receive commands */
ZB_ZCL_DECLARE_ON_OFF_ATTRIB_LIST(
	on_off_server_attr_list,
	&dev_ctx.on_off_attr.on_off);

/* Declare attribute list for Level control cluster (client). */
ZB_ZCL_DECLARE_LEVEL_CONTROL_CLIENT_ATTRIB_LIST(
	level_control_client_attr_list);

/* Declare cluster list for Dimmer Switch device with On/Off server. */
zb_zcl_cluster_desc_t dimmer_switch_clusters[] =
{
	ZB_ZCL_CLUSTER_DESC(
		ZB_ZCL_CLUSTER_ID_BASIC,
		ZB_ZCL_ARRAY_SIZE(basic_server_attr_list, zb_zcl_attr_t),
		(basic_server_attr_list),
		ZB_ZCL_CLUSTER_SERVER_ROLE,
		ZB_ZCL_MANUF_CODE_INVALID
	),
	ZB_ZCL_CLUSTER_DESC(
		ZB_ZCL_CLUSTER_ID_IDENTIFY,
		ZB_ZCL_ARRAY_SIZE(identify_server_attr_list, zb_zcl_attr_t),
		(identify_server_attr_list),
		ZB_ZCL_CLUSTER_SERVER_ROLE,
		ZB_ZCL_MANUF_CODE_INVALID
	),
	ZB_ZCL_CLUSTER_DESC(
		ZB_ZCL_CLUSTER_ID_IDENTIFY,
		ZB_ZCL_ARRAY_SIZE(identify_client_attr_list, zb_zcl_attr_t),
		(identify_client_attr_list),
		ZB_ZCL_CLUSTER_CLIENT_ROLE,
		ZB_ZCL_MANUF_CODE_INVALID
	),
	ZB_ZCL_CLUSTER_DESC(
		ZB_ZCL_CLUSTER_ID_ON_OFF,
		ZB_ZCL_ARRAY_SIZE(on_off_server_attr_list, zb_zcl_attr_t),
		(on_off_server_attr_list),
		ZB_ZCL_CLUSTER_SERVER_ROLE,
		ZB_ZCL_MANUF_CODE_INVALID
	),
	ZB_ZCL_CLUSTER_DESC(
		ZB_ZCL_CLUSTER_ID_SCENES,
		ZB_ZCL_ARRAY_SIZE(scenes_client_attr_list, zb_zcl_attr_t),
		(scenes_client_attr_list),
		ZB_ZCL_CLUSTER_CLIENT_ROLE,
		ZB_ZCL_MANUF_CODE_INVALID
	),
	ZB_ZCL_CLUSTER_DESC(
		ZB_ZCL_CLUSTER_ID_GROUPS,
		ZB_ZCL_ARRAY_SIZE(groups_client_attr_list, zb_zcl_attr_t),
		(groups_client_attr_list),
		ZB_ZCL_CLUSTER_CLIENT_ROLE,
		ZB_ZCL_MANUF_CODE_INVALID
	),
	ZB_ZCL_CLUSTER_DESC(
		ZB_ZCL_CLUSTER_ID_ON_OFF,
		ZB_ZCL_ARRAY_SIZE(on_off_client_attr_list, zb_zcl_attr_t),
		(on_off_client_attr_list),
		ZB_ZCL_CLUSTER_CLIENT_ROLE,
		ZB_ZCL_MANUF_CODE_INVALID
	),
	ZB_ZCL_CLUSTER_DESC(
		ZB_ZCL_CLUSTER_ID_LEVEL_CONTROL,
		ZB_ZCL_ARRAY_SIZE(level_control_client_attr_list, zb_zcl_attr_t),
		(level_control_client_attr_list),
		ZB_ZCL_CLUSTER_CLIENT_ROLE,
		ZB_ZCL_MANUF_CODE_INVALID
	)
};

/* Declare custom simple descriptor with 3 server clusters (Basic, Identify, On/Off)
 * and 5 client clusters (Identify, Scenes, Groups, On/Off, Level Control)
 */
ZB_DECLARE_SIMPLE_DESC(3, 5);

ZB_AF_SIMPLE_DESC_TYPE(3, 5) simple_desc_dimmer_switch_ep = {
	LIGHT_SWITCH_ENDPOINT,                /* Endpoint ID */
	ZB_AF_HA_PROFILE_ID,                  /* Application profile identifier */
	ZB_DIMMER_SWITCH_DEVICE_ID,           /* Device ID */
	ZB_DEVICE_VER_DIMMER_SWITCH,          /* Device version */
	0,                                     /* Reserved */
	3,                                     /* Number of input (server) clusters */
	5,                                     /* Number of output (client) clusters */
	{
		ZB_ZCL_CLUSTER_ID_BASIC,           /* Server: Basic */
		ZB_ZCL_CLUSTER_ID_IDENTIFY,        /* Server: Identify */
		ZB_ZCL_CLUSTER_ID_ON_OFF,          /* Server: On/Off */
		ZB_ZCL_CLUSTER_ID_IDENTIFY,        /* Client: Identify */
		ZB_ZCL_CLUSTER_ID_SCENES,          /* Client: Scenes */
		ZB_ZCL_CLUSTER_ID_GROUPS,          /* Client: Groups */
		ZB_ZCL_CLUSTER_ID_ON_OFF,          /* Client: On/Off */
		ZB_ZCL_CLUSTER_ID_LEVEL_CONTROL,   /* Client: Level Control */
	}
};

/* Declare endpoint descriptor */
ZB_AF_DECLARE_ENDPOINT_DESC(
	dimmer_switch_ep,
	LIGHT_SWITCH_ENDPOINT,
	ZB_AF_HA_PROFILE_ID,
	0,
	NULL,
	ZB_ZCL_ARRAY_SIZE(dimmer_switch_clusters, zb_zcl_cluster_desc_t),
	dimmer_switch_clusters,
	(zb_af_simple_desc_1_1_t *)&simple_desc_dimmer_switch_ep,
	0, NULL, /* No reporting ctx */
	0, NULL  /* No CVC ctx */
);

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
	)
};

/* Declare simple descriptor type for relay endpoint (3 server clusters, 0 client clusters) */
ZB_DECLARE_SIMPLE_DESC(3, 0);

ZB_AF_SIMPLE_DESC_TYPE(3, 0) simple_desc_relay_switch_ep = {
	RELAY_SWITCH_ENDPOINT,                /* Endpoint ID */
	ZB_AF_HA_PROFILE_ID,                  /* Application profile identifier */
	ZB_HA_ON_OFF_OUTPUT_DEVICE_ID,        /* Device ID - On/Off Output */
	0,                                     /* Device version */
	0,                                     /* Reserved */
	3,                                     /* Number of input (server) clusters */
	0,                                     /* Number of output (client) clusters */
	{
		ZB_ZCL_CLUSTER_ID_BASIC,           /* Server: Basic */
		ZB_ZCL_CLUSTER_ID_IDENTIFY,        /* Server: Identify */
		ZB_ZCL_CLUSTER_ID_ON_OFF,          /* Server: On/Off */
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
	0, NULL, /* No reporting ctx */
	0, NULL  /* No CVC ctx */
);

/* =============================================================================
 * VOLTAGE SENSOR ENDPOINT (EP 3) - Analog Input for voltage measurement
 * =============================================================================
 * Uses Analog Input cluster (0x000C) for zigbee2mqtt compatibility.
 * Attribute reads are handled dynamically via ZCL callback.
 */

/* Analog Input cluster ID and attributes */
#define ZB_ZCL_CLUSTER_ID_ANALOG_INPUT              0x000C
#define ZB_ZCL_ATTR_ANALOG_INPUT_PRESENT_VALUE_ID   0x0055
#define ZB_ZCL_ATTR_ANALOG_INPUT_OUT_OF_SERVICE_ID  0x0051
#define ZB_ZCL_ATTR_ANALOG_INPUT_STATUS_FLAGS_ID    0x006F

/* Declare attribute list for Basic cluster (server) for voltage endpoint */
#define ZB_ZCL_BASIC_VOLTAGE_ATTRIB_LIST                                         \
	ZB_ZCL_START_DECLARE_ATTRIB_LIST_CLUSTER_REVISION(voltage_basic_attr_list, ZB_ZCL_BASIC) \
	ZB_ZCL_SET_ATTR_DESC(ZB_ZCL_ATTR_BASIC_ZCL_VERSION_ID, (&voltage_dev_ctx.basic_attr.zcl_version)) \
	ZB_ZCL_SET_ATTR_DESC(ZB_ZCL_ATTR_BASIC_MANUFACTURER_NAME_ID, (voltage_dev_ctx.manufacturer_name)) \
	ZB_ZCL_SET_ATTR_DESC(ZB_ZCL_ATTR_BASIC_MODEL_IDENTIFIER_ID, (voltage_dev_ctx.model_id)) \
	ZB_ZCL_SET_ATTR_DESC(ZB_ZCL_ATTR_BASIC_POWER_SOURCE_ID, (&voltage_dev_ctx.basic_attr.power_source)) \
	ZB_ZCL_FINISH_DECLARE_ATTRIB_LIST

ZB_ZCL_BASIC_VOLTAGE_ATTRIB_LIST;

/* Declare attribute list for Identify cluster (server) for voltage endpoint */
ZB_ZCL_DECLARE_IDENTIFY_SERVER_ATTRIB_LIST(
	voltage_identify_server_attr_list,
	&voltage_dev_ctx.identify_attr.identify_time);

/* Analog Input attribute list - pointers initialized at runtime
 * Note: ZBOSS attribute lists need the data_p field set at runtime
 * due to position-independent code requirements
 */
static zb_zcl_attr_t voltage_analog_input_attr_list[3];  /* presentValue, clusterRevision, terminator */

/* Number of actual attributes (not counting terminator) */
#define VOLTAGE_ANALOG_INPUT_ATTR_COUNT 2

/* Cluster revision for analog input */
static zb_uint16_t voltage_analog_input_cluster_revision = 1;

/* Declare cluster list for Voltage sensor endpoint */
zb_zcl_cluster_desc_t voltage_sensor_clusters[] =
{
	ZB_ZCL_CLUSTER_DESC(
		ZB_ZCL_CLUSTER_ID_BASIC,
		ZB_ZCL_ARRAY_SIZE(voltage_basic_attr_list, zb_zcl_attr_t),
		(voltage_basic_attr_list),
		ZB_ZCL_CLUSTER_SERVER_ROLE,
		ZB_ZCL_MANUF_CODE_INVALID
	),
	ZB_ZCL_CLUSTER_DESC(
		ZB_ZCL_CLUSTER_ID_IDENTIFY,
		ZB_ZCL_ARRAY_SIZE(voltage_identify_server_attr_list, zb_zcl_attr_t),
		(voltage_identify_server_attr_list),
		ZB_ZCL_CLUSTER_SERVER_ROLE,
		ZB_ZCL_MANUF_CODE_INVALID
	),
	/* Analog Input cluster with runtime-initialized attributes */
	{
		ZB_ZCL_CLUSTER_ID_ANALOG_INPUT,
		VOLTAGE_ANALOG_INPUT_ATTR_COUNT,
		voltage_analog_input_attr_list,
		ZB_ZCL_CLUSTER_SERVER_ROLE,
		ZB_ZCL_MANUF_CODE_INVALID,
		NULL
	}
};

/* Simple descriptor for voltage endpoint (3 server clusters, 0 client clusters) */
/* Note: ZB_DECLARE_SIMPLE_DESC(3, 0) already declared for relay endpoint */
ZB_AF_SIMPLE_DESC_TYPE(3, 0) simple_desc_voltage_sensor_ep = {
	VOLTAGE_SENSOR_ENDPOINT,              /* Endpoint ID */
	ZB_AF_HA_PROFILE_ID,                  /* Application profile identifier */
	ZB_HA_SIMPLE_SENSOR_DEVICE_ID,        /* Device ID - Simple Sensor */
	0,                                     /* Device version */
	0,                                     /* Reserved */
	3,                                     /* Number of input (server) clusters */
	0,                                     /* Number of output (client) clusters */
	{
		ZB_ZCL_CLUSTER_ID_BASIC,           /* Server: Basic */
		ZB_ZCL_CLUSTER_ID_IDENTIFY,        /* Server: Identify */
		ZB_ZCL_CLUSTER_ID_ANALOG_INPUT,    /* Server: Analog Input (0x000C) */
	}
};

/* Declare voltage sensor endpoint descriptor */
ZB_AF_DECLARE_ENDPOINT_DESC(
	voltage_sensor_ep,
	VOLTAGE_SENSOR_ENDPOINT,
	ZB_AF_HA_PROFILE_ID,
	0,
	NULL,
	ZB_ZCL_ARRAY_SIZE(voltage_sensor_clusters, zb_zcl_cluster_desc_t),
	voltage_sensor_clusters,
	(zb_af_simple_desc_1_1_t *)&simple_desc_voltage_sensor_ep,
	0, NULL, /* No reporting ctx */
	0, NULL  /* No CVC ctx */
);

/* Declare application's device context (list of registered endpoints) */
#ifndef CONFIG_ZIGBEE_FOTA
ZBOSS_DECLARE_DEVICE_CTX_3_EP(dimmer_switch_ctx, dimmer_switch_ep, relay_switch_ep, voltage_sensor_ep);
#else
  #if LIGHT_SWITCH_ENDPOINT == CONFIG_ZIGBEE_FOTA_ENDPOINT
    #error "Light switch and Zigbee OTA endpoints should be different."
  #endif
  #if RELAY_SWITCH_ENDPOINT == CONFIG_ZIGBEE_FOTA_ENDPOINT
    #error "Relay switch and Zigbee OTA endpoints should be different."
  #endif
  #if VOLTAGE_SENSOR_ENDPOINT == CONFIG_ZIGBEE_FOTA_ENDPOINT
    #error "Voltage sensor and Zigbee OTA endpoints should be different."
  #endif

extern zb_af_endpoint_desc_t zigbee_fota_client_ep;
ZBOSS_DECLARE_DEVICE_CTX_4_EP(dimmer_switch_ctx,
			      zigbee_fota_client_ep,
			      dimmer_switch_ep,
			      relay_switch_ep,
			      voltage_sensor_ep);
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

			if (device_cb_param->endpoint == LIGHT_SWITCH_ENDPOINT) {
				LOG_INF("Zigbee On/Off command for LED: %s", new_value ? "ON" : "OFF");
				led_ctx.led_state = (new_value == ZB_TRUE);
				led_control_set(led_ctx.led_state);
			} else if (device_cb_param->endpoint == RELAY_SWITCH_ENDPOINT) {
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
	/* Initialize LED state - start with LED OFF */
	led_ctx.led_state = false;
	led_control_set(led_ctx.led_state);

	/* Initialize relay state - start with relay OFF */
	relay_ctx.relay_state = false;
	relay_control_set(relay_ctx.relay_state);

	/* Basic cluster attributes data for LED endpoint */
	dev_ctx.basic_attr.zcl_version = ZB_ZCL_VERSION;
	dev_ctx.basic_attr.power_source = ZB_ZCL_BASIC_POWER_SOURCE_UNKNOWN;

	/* Set manufacturer name and model identifier with Zigbee string format (length + data). */
	ZB_ZCL_SET_STRING_VAL(dev_ctx.manufacturer_name,
			      (zb_uint8_t *)"Nordic",
			      ZB_ZCL_STRING_CONST_SIZE("Nordic"));

	ZB_ZCL_SET_STRING_VAL(dev_ctx.model_id,
			      (zb_uint8_t *)"Cava test dev",
			      ZB_ZCL_STRING_CONST_SIZE("Cava test dev"));

	/* On/Off cluster attributes - sync with LED state. */
	dev_ctx.on_off_attr.on_off = led_ctx.led_state ? ZB_TRUE : ZB_FALSE;

	/* Identify cluster attributes data. */
	dev_ctx.identify_attr.identify_time = ZB_ZCL_IDENTIFY_IDENTIFY_TIME_DEFAULT_VALUE;

	/* Basic cluster attributes data for relay endpoint */
	relay_dev_ctx.basic_attr.zcl_version = ZB_ZCL_VERSION;
	relay_dev_ctx.basic_attr.power_source = ZB_ZCL_BASIC_POWER_SOURCE_UNKNOWN;

	ZB_ZCL_SET_STRING_VAL(relay_dev_ctx.manufacturer_name,
			      (zb_uint8_t *)"Nordic",
			      ZB_ZCL_STRING_CONST_SIZE("Nordic"));

	ZB_ZCL_SET_STRING_VAL(relay_dev_ctx.model_id,
			      (zb_uint8_t *)"Relay Switch",
			      ZB_ZCL_STRING_CONST_SIZE("Relay Switch"));

	/* On/Off cluster attributes for relay - sync with relay state. */
	relay_dev_ctx.on_off_attr.on_off = relay_ctx.relay_state ? ZB_TRUE : ZB_FALSE;

	/* Identify cluster attributes data for relay. */
	relay_dev_ctx.identify_attr.identify_time = ZB_ZCL_IDENTIFY_IDENTIFY_TIME_DEFAULT_VALUE;

	/* Basic cluster attributes data for voltage sensor endpoint */
	voltage_dev_ctx.basic_attr.zcl_version = ZB_ZCL_VERSION;
	voltage_dev_ctx.basic_attr.power_source = ZB_ZCL_BASIC_POWER_SOURCE_UNKNOWN;

	ZB_ZCL_SET_STRING_VAL(voltage_dev_ctx.manufacturer_name,
			      (zb_uint8_t *)"Nordic",
			      ZB_ZCL_STRING_CONST_SIZE("Nordic"));

	ZB_ZCL_SET_STRING_VAL(voltage_dev_ctx.model_id,
			      (zb_uint8_t *)"Voltage Sensor",
			      ZB_ZCL_STRING_CONST_SIZE("Voltage Sensor"));

	/* Analog Input cluster attributes - initial voltage 0 */
	voltage_present_value = 0;
	voltage_last_reported = 0;  /* First reading will trigger report if >= 50mV */
	voltage_status_flags = 0;

	/* Runtime initialization of Analog Input attribute list */
	/* Attribute 0: presentValue (0x0055) */
	voltage_analog_input_attr_list[0].id = ZB_ZCL_ATTR_ANALOG_INPUT_PRESENT_VALUE_ID;
	voltage_analog_input_attr_list[0].type = ZB_ZCL_ATTR_TYPE_S16;
	voltage_analog_input_attr_list[0].access = ZB_ZCL_ATTR_ACCESS_READ_ONLY;
	voltage_analog_input_attr_list[0].manuf_code = ZB_ZCL_NON_MANUFACTURER_SPECIFIC;
	voltage_analog_input_attr_list[0].data_p = &voltage_present_value;

	/* Attribute 1: clusterRevision (0xFFFD) */
	voltage_analog_input_attr_list[1].id = ZB_ZCL_ATTR_GLOBAL_CLUSTER_REVISION_ID;
	voltage_analog_input_attr_list[1].type = ZB_ZCL_ATTR_TYPE_U16;
	voltage_analog_input_attr_list[1].access = ZB_ZCL_ATTR_ACCESS_READ_ONLY;
	voltage_analog_input_attr_list[1].manuf_code = ZB_ZCL_NON_MANUFACTURER_SPECIFIC;
	voltage_analog_input_attr_list[1].data_p = &voltage_analog_input_cluster_revision;

	/* Terminator */
	voltage_analog_input_attr_list[2].id = ZB_ZCL_NULL_ID;
	voltage_analog_input_attr_list[2].type = 0;
	voltage_analog_input_attr_list[2].access = 0;
	voltage_analog_input_attr_list[2].manuf_code = ZB_ZCL_NON_MANUFACTURER_SPECIFIC;
	voltage_analog_input_attr_list[2].data_p = NULL;

	LOG_INF("Analog Input attributes initialized");

	/* Identify cluster attributes data for voltage sensor. */
	voltage_dev_ctx.identify_attr.identify_time = ZB_ZCL_IDENTIFY_IDENTIFY_TIME_DEFAULT_VALUE;
}

void zigbee_device_register(void)
{
	/* Register callback for handling ZCL commands */
	ZB_ZCL_REGISTER_DEVICE_CB(zcl_device_cb);

	/* Register device context (endpoints) */
	ZB_AF_REGISTER_DEVICE_CTX(&dimmer_switch_ctx);

	LOG_INF("Registered Zigbee endpoints: EP%d (LED), EP%d (Relay), EP%d (Voltage)",
		LIGHT_SWITCH_ENDPOINT, RELAY_SWITCH_ENDPOINT, VOLTAGE_SENSOR_ENDPOINT);

	/* Register handlers to identify notifications */
	ZB_AF_SET_IDENTIFY_NOTIFICATION_HANDLER(LIGHT_SWITCH_ENDPOINT, identify_cb);
	ZB_AF_SET_IDENTIFY_NOTIFICATION_HANDLER(RELAY_SWITCH_ENDPOINT, identify_cb);
	ZB_AF_SET_IDENTIFY_NOTIFICATION_HANDLER(VOLTAGE_SENSOR_ENDPOINT, identify_cb);
#ifdef CONFIG_ZIGBEE_FOTA
	ZB_AF_SET_IDENTIFY_NOTIFICATION_HANDLER(CONFIG_ZIGBEE_FOTA_ENDPOINT, identify_cb);
#endif
}

void zigbee_device_set_led(bool on)
{
	led_ctx.led_state = on;
	led_control_set(led_ctx.led_state);

	/* Update Zigbee On/Off attribute */
	zb_uint8_t new_value = on ? ZB_TRUE : ZB_FALSE;
	zb_zcl_status_t status = zb_zcl_set_attr_val(
		LIGHT_SWITCH_ENDPOINT,
		ZB_ZCL_CLUSTER_ID_ON_OFF,
		ZB_ZCL_CLUSTER_SERVER_ROLE,
		ZB_ZCL_ATTR_ON_OFF_ON_OFF_ID,
		&new_value,
		ZB_FALSE);

	if (status != ZB_ZCL_STATUS_SUCCESS) {
		LOG_ERR("Failed to update On/Off attribute: %d", status);
	}
}

bool zigbee_device_toggle_led(void)
{
	zigbee_device_set_led(!led_ctx.led_state);
	LOG_INF("LED toggled to %s", led_ctx.led_state ? "ON" : "OFF");
	return led_ctx.led_state;
}

bool zigbee_device_get_led_state(void)
{
	return led_ctx.led_state;
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

/* Send voltage attribute report to coordinator (address 0x0000) */
static void send_voltage_report(zb_bufid_t bufid)
{
	zb_uint8_t *cmd_ptr;
	zb_uint16_t dst_addr = 0x0000;  /* Coordinator address */
	zb_int16_t value = voltage_present_value;

	/* Start building the ZCL packet */
	cmd_ptr = ZB_ZCL_START_PACKET(bufid);

	/* Build frame control for Report Attributes command:
	 * - Global command (not cluster-specific)
	 * - Server to client direction
	 * - Disable default response
	 */
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

	/* Add attribute report record:
	 * - Attribute ID (2 bytes LE)
	 * - Attribute type (1 byte)
	 * - Attribute value (size depends on type)
	 */
	ZB_ZCL_PACKET_PUT_DATA16_VAL(cmd_ptr, ZB_ZCL_ATTR_ANALOG_INPUT_PRESENT_VALUE_ID);
	ZB_ZCL_PACKET_PUT_DATA8(cmd_ptr, ZB_ZCL_ATTR_TYPE_S16);
	ZB_ZCL_PACKET_PUT_DATA16_VAL(cmd_ptr, value);

	/* Finish and send the packet */
	ZB_ZCL_FINISH_N_SEND_PACKET(
		bufid,
		cmd_ptr,
		dst_addr,
		ZB_APS_ADDR_MODE_16_ENDP_PRESENT,
		1,  /* Destination endpoint (coordinator) */
		VOLTAGE_SENSOR_ENDPOINT,
		ZB_AF_HA_PROFILE_ID,
		ZB_ZCL_CLUSTER_ID_ANALOG_INPUT,
		NULL);
}

static void voltage_report_cb(zb_uint8_t param)
{
	zb_bufid_t bufid;

	if (param) {
		bufid = param;
	} else {
		bufid = zb_buf_get_out();
	}

	if (!bufid) {
		LOG_WRN("No buffer for voltage report");
		return;
	}

	send_voltage_report(bufid);
	LOG_INF("Voltage report sent: %d cV", voltage_present_value);
}

void zigbee_device_update_voltage(int32_t voltage_mv)
{
	/* Store voltage in centivolts (e.g., 330 = 3.30V) for better precision */
	zb_int16_t new_value = (zb_int16_t)(voltage_mv / 10);
	zb_int16_t diff = new_value - voltage_last_reported;

	/* Calculate absolute difference */
	if (diff < 0) {
		diff = -diff;
	}

	voltage_present_value = new_value;

	LOG_DBG("Voltage: %d.%02d V (%d cV, diff=%d cV)",
		voltage_mv / 1000, (voltage_mv % 1000) / 10, voltage_present_value, diff);

	/* Only send report if change exceeds threshold (50mV = 5 cV) */
	if (diff >= VOLTAGE_REPORT_THRESHOLD_CV) {
		voltage_last_reported = voltage_present_value;
		ZB_SCHEDULE_APP_CALLBACK(voltage_report_cb, 0);
		LOG_INF("Voltage changed: %d.%02d V (%d cV)",
			voltage_mv / 1000, (voltage_mv % 1000) / 10, voltage_present_value);
	}
}

int16_t zigbee_device_get_voltage_centivolts(void)
{
	return voltage_present_value;
}
