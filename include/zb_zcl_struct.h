#ifndef ZB_ZCL_STRUCT_H
#define ZB_ZCL_STRUCT_H

#include <zboss_api.h>
#include <zboss_api_addons.h>
#include <zcl/zb_zcl_basic.h>
#include <zcl/zb_zcl_identify.h>
#include <zcl/zb_zcl_power_config.h>
#include <zcl/zb_zcl_rel_humidity_measurement.h>

#define NORDIC_DIY_TEMPLATE_INIT_BASIC_APP_VERSION 1
#define NORDIC_DIY_TEMPLATE_INIT_BASIC_STACK_VERSION 10
#define NORDIC_DIY_TEMPLATE_INIT_BASIC_HW_VERSION 11
#define NORDIC_DIY_TEMPLATE_INIT_BASIC_SW_VERSION "build-1"
#define NORDIC_DIY_TEMPLATE_INIT_BASIC_MANUF_NAME "Nordic"
#define NORDIC_DIY_TEMPLATE_INIT_BASIC_MODEL_ID "nordic_diy_template"
#define NORDIC_DIY_TEMPLATE_INIT_BASIC_DATE_CODE "033026"
#define NORDIC_DIY_TEMPLATE_INIT_BASIC_POWER_SOURCE ZB_ZCL_BASIC_POWER_SOURCE_BATTERY
#define NORDIC_DIY_TEMPLATE_INIT_BASIC_LOCATION_DESC ""
#define NORDIC_DIY_TEMPLATE_INIT_BASIC_PH_ENV ZB_ZCL_BASIC_ENV_UNSPECIFIED

#define ATTR_HUM_MIN (0)
#define ATTR_HUM_MAX (10000)

#define ENDPOINT_NUM 1
#define ERASE_PERSISTENT_CONFIG ZB_FALSE

#define READ_DATA_INITIAL_DELAY K_SECONDS(30)
#define READ_DATA_TIMER_PERIOD K_SECONDS(60)
#define LONG_POLL_INTERVAL 600000

typedef struct zb_zcl_power_attrs zb_zcl_power_attrs_t;

struct zb_zcl_power_attrs
{
    zb_uint8_t voltage;
    zb_uint8_t percent_remaining;
};

typedef struct
{
    zb_int16_t measure_value;
    zb_int16_t min_measure_value;
    zb_int16_t max_measure_value;
} zb_zcl_humidity_measurement_attrs_t;

struct zb_device_ctx
{
    zb_zcl_basic_attrs_ext_t basic_attr;
    zb_zcl_identify_attrs_t identify_attr;
    zb_zcl_humidity_measurement_attrs_t humidity_measure_attrs;
    zb_zcl_power_attrs_t power_attr;
};

extern struct zb_device_ctx dev_ctx;

extern zb_af_device_ctx_t NORDIC_DIY_TEMPLATE_ctx;
extern zb_af_endpoint_desc_t NORDIC_DIY_TEMPLATE_ep;

void app_clusters_attr_init(void);

#endif /* ZB_ZCL_STRUCT_H */
