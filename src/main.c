#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/gpio.h>
#include <ram_pwrdn.h>
#include <dk_buttons_and_leds.h>
#include <drivers/include/nrfx_saadc.h>
#include <zboss_api.h>
#include <zboss_api_addons.h>
#include <zb_zcl_reporting.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/drivers/led.h>
#include <zigbee/zigbee_app_utils.h>
#include <zigbee/zigbee_error_handler.h>
#include <zb_nrf_platform.h>
#include "zb_range_extender.h"
#include <zboss_api_zcl.h>

#include "bme280.h"
#include "vcc.h"

#define I2C_DEV_NODE DT_NODELABEL(i2c1)
const struct device *i2c_dev = DEVICE_DT_GET(I2C_DEV_NODE);

#define SW0_NODE DT_ALIAS(sw0)
#define MY_BUTTON_MASK BIT(0)
static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(SW0_NODE, gpios);

#define LED0_NODE DT_ALIAS(led0)
#define MY_LED_MASK BIT(0)
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);
const struct device *leds = DEVICE_DT_GET(DT_NODELABEL(leds));
static struct k_timer led_timer;

#define NORDIC_DIY_INIT_BASIC_APP_VERSION 01
#define NORDIC_DIY_INIT_BASIC_STACK_VERSION 10
#define NORDIC_DIY_INIT_BASIC_HW_VERSION 11
#define NORDIC_DIY_INIT_BASIC_SW_VERSION "build-1"
#define NORDIC_DIY_INIT_BASIC_MANUF_NAME "Nordic"
#define NORDIC_DIY_INIT_BASIC_MODEL_ID "nordic DIY PM"
#define NORDIC_DIY_INIT_BASIC_DATE_CODE "033026"
#define NORDIC_DIY_INIT_BASIC_POWER_SOURCE ZB_ZCL_BASIC_POWER_SOURCE_BATTERY
#define NORDIC_DIY_INIT_BASIC_LOCATION_DESC ""
#define NORDIC_DIY_INIT_BASIC_PH_ENV ZB_ZCL_BASIC_ENV_UNSPECIFIED

#define ATTR_TEMP_MIN (-4000)
#define ATTR_TEMP_MAX (8500)
#define ATTR_TEMP_TOLERANCE (100)

#define ATTR_HUM_MIN (0)
#define ATTR_HUM_MAX (10000)

#define ATTR_PRESSURE_MIN (3000)
#define ATTR_PRESSURE_MAX (11000)
#define ATTR_PRESSURE_TOLERANCE (1)

#define ENDPOINT_NUM 1
#define ERASE_PERSISTENT_CONFIG ZB_FALSE

#define READ_DATA_INITIAL_DELAY K_SECONDS(30)
#define READ_DATA_TIMER_PERIOD K_SECONDS(60)

struct zb_zcl_power_attrs
{
	zb_uint8_t voltage;
	zb_uint8_t percent_remaining;
};

typedef struct zb_zcl_power_attrs zb_zcl_power_attrs_t;

typedef struct
{
	zb_int16_t measure_value;
	zb_int16_t min_measure_value;
	zb_int16_t max_measure_value;
} zb_zcl_humidity_measurement_attrs_t;

typedef struct
{
	zb_int16_t measure_value;
	zb_int16_t min_measure_value;
	zb_int16_t max_measure_value;
	zb_int16_t tolerance_value;
} zb_zcl_pressure_measurement_attrs_t;

typedef struct
{
	zb_uint32_t checkin_interval;
	zb_uint32_t long_poll_interval;
	zb_uint16_t short_poll_interval;
	zb_uint16_t fast_poll_timeout;
	zb_uint32_t checkin_interval_min;
	zb_uint32_t long_poll_interval_min;
	zb_uint16_t fast_poll_timeout_max;
} zb_zcl_poll_control_attrs_t;

struct zb_device_ctx
{
	zb_zcl_basic_attrs_ext_t basic_attr;
	zb_zcl_identify_attrs_t identify_attr;
	zb_zcl_temp_measurement_attrs_t temp_measure_attrs;
	zb_zcl_humidity_measurement_attrs_t humidity_measure_attrs;
	zb_zcl_pressure_measurement_attrs_t pressure_measure_attrs;
	zb_zcl_power_attrs_t power_attr;
	zb_zcl_poll_control_attrs_t poll_control_attrs;
};

typedef struct
{
	uint16_t voltage;
	uint8_t capacity;
} voltage_capacity;

void zboss_signal_handler(zb_bufid_t bufid);
static int configure_gpio(void);
static void set_led_off(const struct gpio_dt_spec *led);
static void set_led_on(const struct gpio_dt_spec *led);
static void button_handler(uint32_t button_state, uint32_t has_changed);
static void app_clusters_attr_init(void);
static void read_data_cb(struct k_timer *timer);
static void read_data_handler(struct k_work *work);
static uint8_t cr2032_CalculateLevel(uint16_t voltage);
static void send_attribute_report(zb_bufid_t bufid, zb_uint16_t cmd_id);
static void enter_deep_sleep_work_handler(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(deep_sleep_work, enter_deep_sleep_work_handler);
static void reboot_work_handler(struct k_work *work);
static struct k_work_delayable reboot_work;
static void led_timer_handler(struct k_timer *timer);

LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);

// attribute storage for our device
static struct zb_device_ctx dev_ctx;

ZB_ZCL_DECLARE_BASIC_ATTRIB_LIST_EXT(
	basic_attr_list,
	&dev_ctx.basic_attr.zcl_version,
	&dev_ctx.basic_attr.app_version,
	&dev_ctx.basic_attr.stack_version,
	&dev_ctx.basic_attr.hw_version,
	dev_ctx.basic_attr.mf_name,
	dev_ctx.basic_attr.model_id,
	dev_ctx.basic_attr.date_code,
	&dev_ctx.basic_attr.power_source,
	dev_ctx.basic_attr.location_id,
	&dev_ctx.basic_attr.ph_env,
	dev_ctx.basic_attr.sw_ver);

ZB_ZCL_DECLARE_IDENTIFY_CLIENT_ATTRIB_LIST(
	identify_client_attr_list);

ZB_ZCL_DECLARE_IDENTIFY_SERVER_ATTRIB_LIST(
	identify_server_attr_list,
	&dev_ctx.identify_attr.identify_time);

#define ZB_ZCL_DECLARE_POWER_CONFIG_VOLTAGE_PERCENT_ATTRIB_LIST(attr_list, voltage, remaining)        \
	ZB_ZCL_START_DECLARE_ATTRIB_LIST_CLUSTER_REVISION(attr_list, ZB_ZCL_POWER_CONFIG)                 \
	ZB_SET_ATTR_DESCR_WITH_ZB_ZCL_ATTR_POWER_CONFIG_BATTERY_VOLTAGE_ID(voltage, ),                    \
		ZB_SET_ATTR_DESCR_WITH_ZB_ZCL_ATTR_POWER_CONFIG_BATTERY_PERCENTAGE_REMAINING_ID(remaining, ), \
		ZB_ZCL_FINISH_DECLARE_ATTRIB_LIST;

ZB_ZCL_DECLARE_POWER_CONFIG_VOLTAGE_PERCENT_ATTRIB_LIST(
	power_config_server_attr_list,
	&dev_ctx.power_attr.voltage,
	&dev_ctx.power_attr.percent_remaining)

ZB_ZCL_DECLARE_POLL_CONTROL_ATTRIB_LIST(
	poll_control_attrib_list,
	&dev_ctx.poll_control_attrs.checkin_interval,
	&dev_ctx.poll_control_attrs.long_poll_interval,
	&dev_ctx.poll_control_attrs.short_poll_interval,
	&dev_ctx.poll_control_attrs.fast_poll_timeout,
	&dev_ctx.poll_control_attrs.checkin_interval_min,
	&dev_ctx.poll_control_attrs.long_poll_interval_min,
	&dev_ctx.poll_control_attrs.fast_poll_timeout_max);

ZB_ZCL_DECLARE_TEMP_MEASUREMENT_ATTRIB_LIST(
	temp_measurement_attr_list,
	&dev_ctx.temp_measure_attrs.measure_value,
	&dev_ctx.temp_measure_attrs.min_measure_value,
	&dev_ctx.temp_measure_attrs.max_measure_value,
	&dev_ctx.temp_measure_attrs.tolerance);

ZB_ZCL_DECLARE_REL_HUMIDITY_MEASUREMENT_ATTRIB_LIST(
	humidity_measurement_attr_list,
	&dev_ctx.humidity_measure_attrs.measure_value,
	&dev_ctx.humidity_measure_attrs.min_measure_value,
	&dev_ctx.humidity_measure_attrs.max_measure_value);

ZB_ZCL_DECLARE_PRESSURE_MEASUREMENT_ATTRIB_LIST(
	pressure_measurement_attr_list,
	&dev_ctx.pressure_measure_attrs.measure_value,
	&dev_ctx.pressure_measure_attrs.min_measure_value,
	&dev_ctx.pressure_measure_attrs.max_measure_value,
	&dev_ctx.pressure_measure_attrs.tolerance_value);

ZB_DECLARE_NORDIC_DIY_CLUSTER_LIST(
	NORDIC_DIY_clusters,
	basic_attr_list,
	identify_server_attr_list,
	power_config_server_attr_list,
	temp_measurement_attr_list,
	humidity_measurement_attr_list,
	pressure_measurement_attr_list,
	identify_client_attr_list,
	poll_control_attrib_list);

ZB_DECLARE_NORDIC_DIY_EP(
	NORDIC_DIY_ep,
	ENDPOINT_NUM,
	NORDIC_DIY_clusters);

ZBOSS_DECLARE_DEVICE_CTX_1_EP(
	NORDIC_DIY_ctx,
	NORDIC_DIY_ep);

struct k_timer read_data_timer;
K_WORK_DEFINE(read_data_work, read_data_handler);

static voltage_capacity volt_cap[] =
	{{3000, 200}, {2900, 160}, {2800, 120}, {2700, 80}, {2600, 60}, {2500, 40}, {2400, 20}, {2000, 0}};

static void enter_deep_sleep_work_handler(struct k_work *work)
{
	LOG_INF("Configuration window closed. Setting long poll to 10 minutes.");
	zb_zdo_pim_set_long_poll_interval(600000);
}

void zboss_signal_handler(zb_bufid_t bufid)
{
	static bool lastJoin = false;

	zb_zdo_app_signal_hdr_t *sig_hndler = NULL;
	zb_zdo_app_signal_type_t sig = zb_get_app_signal(bufid, &sig_hndler);
	zb_ret_t status = ZB_GET_APP_SIGNAL_STATUS(bufid);

	if (sig == ZB_BDB_SIGNAL_STEERING)
	{
		LOG_INF("Signal STEERING");

		if (status == RET_OK)
		{
			LOG_INF("Device joined the network. Configuring reporting...");
			k_work_reschedule(&deep_sleep_work, K_SECONDS(45));
		}
	}

	if (sig == ZB_BDB_SIGNAL_DEVICE_REBOOT)
	{
		LOG_INF("Signal REBOOT");

		if (status == RET_OK)
		{
			LOG_INF("Device rebooting the network");
			k_work_reschedule(&deep_sleep_work, K_SECONDS(30));
		}
	}

	ZB_ERROR_CHECK(zigbee_default_signal_handler(bufid));

	{
		zb_buf_free(bufid);
	}

	bool thisJoin = ZB_JOINED();
	if ((lastJoin == false) && (thisJoin == true))
	{
		LOG_INF("joined network!");
		k_timer_stop(&led_timer);
		set_led_off(&led);
		k_timer_start(&read_data_timer, READ_DATA_INITIAL_DELAY, READ_DATA_TIMER_PERIOD);
	}
	else if ((lastJoin == true) && (thisJoin == false))
	{
		LOG_INF("left network!");
		// set_led_on(&led);
		k_timer_start(&led_timer, K_NO_WAIT, K_NO_WAIT);
	}
	lastJoin = thisJoin;
}

static int configure_gpio(void)
{
	int ret;
	k_timer_init(&led_timer, led_timer_handler, NULL);
	if (!gpio_is_ready_dt(&led))
	{
		LOG_ERR("GPIO device for led not ready");
		return -ENODEV;
	}

	if (!gpio_is_ready_dt(&button))
	{
		LOG_ERR("GPIO device for button not ready");
		return -ENODEV;
	}

	ret = gpio_pin_configure_dt(&button, GPIO_INPUT);
	if (ret < 0)
	{
		LOG_ERR("Failed to configure button pin: %d", ret);
		return ret;
	}

	dk_buttons_init(button_handler);
	return 0;
}

static void set_led_off(const struct gpio_dt_spec *led)
{

	gpio_pin_set_dt(led, 0);
}

static void set_led_on(const struct gpio_dt_spec *led)
{

	gpio_pin_set_dt(led, 1);
}

static void led_timer_handler(struct k_timer *timer)
{
	ARG_UNUSED(timer);

	static int led_on_state;
	gpio_pin_set_dt(&led, !((++led_on_state) % 100));
	k_timer_start(&led_timer, K_MSEC(10), K_NO_WAIT);
}

static void app_clusters_attr_init(void)
{
	// Basic cluster attributes data.
	dev_ctx.basic_attr.zcl_version = ZB_ZCL_VERSION;
	dev_ctx.basic_attr.power_source = NORDIC_DIY_INIT_BASIC_POWER_SOURCE;
	dev_ctx.basic_attr.stack_version = NORDIC_DIY_INIT_BASIC_STACK_VERSION;
	dev_ctx.basic_attr.hw_version = NORDIC_DIY_INIT_BASIC_HW_VERSION;

	ZB_ZCL_SET_STRING_VAL(dev_ctx.basic_attr.mf_name,
						  NORDIC_DIY_INIT_BASIC_MANUF_NAME,
						  ZB_ZCL_STRING_CONST_SIZE(NORDIC_DIY_INIT_BASIC_MANUF_NAME));

	ZB_ZCL_SET_STRING_VAL(dev_ctx.basic_attr.model_id,
						  NORDIC_DIY_INIT_BASIC_MODEL_ID,
						  ZB_ZCL_STRING_CONST_SIZE(NORDIC_DIY_INIT_BASIC_MODEL_ID));

	ZB_ZCL_SET_STRING_VAL(dev_ctx.basic_attr.date_code,
						  NORDIC_DIY_INIT_BASIC_DATE_CODE,
						  ZB_ZCL_STRING_CONST_SIZE(NORDIC_DIY_INIT_BASIC_DATE_CODE));

	ZB_ZCL_SET_STRING_VAL(dev_ctx.basic_attr.location_id,
						  NORDIC_DIY_INIT_BASIC_LOCATION_DESC,
						  ZB_ZCL_STRING_CONST_SIZE(NORDIC_DIY_INIT_BASIC_LOCATION_DESC));

	dev_ctx.basic_attr.ph_env = NORDIC_DIY_INIT_BASIC_PH_ENV;
	dev_ctx.basic_attr.app_version = NORDIC_DIY_INIT_BASIC_APP_VERSION;
	ZB_ZCL_SET_STRING_VAL(dev_ctx.basic_attr.sw_ver,
					  NORDIC_DIY_INIT_BASIC_SW_VERSION,
					  ZB_ZCL_STRING_CONST_SIZE(NORDIC_DIY_INIT_BASIC_SW_VERSION));

	dev_ctx.identify_attr.identify_time = ZB_ZCL_IDENTIFY_IDENTIFY_TIME_DEFAULT_VALUE;

	dev_ctx.power_attr.voltage = ZB_ZCL_POWER_CONFIG_BATTERY_VOLTAGE_INVALID;
	dev_ctx.power_attr.percent_remaining = ZB_ZCL_POWER_CONFIG_BATTERY_REMAINING_UNKNOWN;

	dev_ctx.temp_measure_attrs.measure_value = ZB_ZCL_ATTR_TEMP_MEASUREMENT_VALUE_UNKNOWN;
	dev_ctx.temp_measure_attrs.min_measure_value = ATTR_TEMP_MIN;
	dev_ctx.temp_measure_attrs.max_measure_value = ATTR_TEMP_MAX;
	dev_ctx.temp_measure_attrs.tolerance = ATTR_TEMP_TOLERANCE;

	dev_ctx.humidity_measure_attrs.measure_value = ZB_ZCL_ATTR_REL_HUMIDITY_MEASUREMENT_VALUE_UNKNOWN;
	dev_ctx.humidity_measure_attrs.min_measure_value = ATTR_HUM_MIN;
	dev_ctx.humidity_measure_attrs.max_measure_value = ATTR_HUM_MAX;

	dev_ctx.pressure_measure_attrs.measure_value = ZB_ZCL_ATTR_PRESSURE_MEASUREMENT_VALUE_UNKNOWN;
	dev_ctx.pressure_measure_attrs.min_measure_value = ATTR_PRESSURE_MIN;
	dev_ctx.pressure_measure_attrs.max_measure_value = ATTR_PRESSURE_MAX;
	dev_ctx.pressure_measure_attrs.tolerance_value = ATTR_PRESSURE_TOLERANCE;

	dev_ctx.poll_control_attrs.checkin_interval =
		ZB_ZCL_POLL_CONTROL_CHECKIN_INTERVAL_DEFAULT_VALUE;
	dev_ctx.poll_control_attrs.long_poll_interval =
		ZB_ZCL_POLL_CONTROL_LONG_POLL_INTERVAL_DEFAULT_VALUE;
	dev_ctx.poll_control_attrs.short_poll_interval =
		ZB_ZCL_POLL_CONTROL_SHORT_POLL_INTERVAL_DEFAULT_VALUE;
	dev_ctx.poll_control_attrs.fast_poll_timeout =
		ZB_ZCL_POLL_CONTROL_FAST_POLL_TIMEOUT_DEFAULT_VALUE;
	dev_ctx.poll_control_attrs.checkin_interval_min =
		ZB_ZCL_POLL_CONTROL_CHECKIN_MIN_INTERVAL_DEFAULT_VALUE;
	dev_ctx.poll_control_attrs.long_poll_interval_min =
		ZB_ZCL_POLL_CONTROL_LONG_POLL_MIN_INTERVAL_DEFAULT_VALUE;
	dev_ctx.poll_control_attrs.fast_poll_timeout_max =
		ZB_ZCL_POLL_CONTROL_FAST_POLL_MAX_TIMEOUT_DEFAULT_VALUE;
}

static void reboot_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);
	LOG_INF("apply settings: reboot");
	k_msleep(100);
	sys_reboot(SYS_REBOOT_WARM);
}

void button_handler(uint32_t button_state, uint32_t has_changed)
{
	LOG_INF("button_handler %d", button_state);

	user_input_indicate();

	check_factory_reset_button(button_state, has_changed);
	if (MY_BUTTON_MASK & has_changed & ~button_state)
	{
		if (!was_factory_reset_done())
		{
			k_work_reschedule(&reboot_work, K_MSEC(50));
		}
	}
}

#define NRFX_SAADC_CONFIG_IRQ_PRIORITY 6

static void read_data_cb(struct k_timer *timer)
{
	ARG_UNUSED(timer);
	k_work_submit(&read_data_work);
}

static void send_attribute_report(zb_bufid_t bufid, zb_uint16_t cmd_id)
{
	ARG_UNUSED(cmd_id);
	LOG_INF("force zboss scheduler to wake and send attribute report");
	zb_buf_free(bufid);
}

static void read_data_handler(struct k_work *work)
{
	ARG_UNUSED(work);
	float temp_bme, press_bme, hum_bme;
	if (bme280_read_data(i2c_dev, &temp_bme, &press_bme, &hum_bme) != 0)
	{
		LOG_ERR("error read bme280");
		return;
	}
	int16_t temp = 2500;
	int16_t hum = 10000;
	zb_int16_t press = 0;

	temp = (int16_t)(temp_bme * 100);
	hum = (int16_t)(hum_bme * 100);
	press = (int16_t)(press_bme * 10.0f);

	zb_zcl_set_attr_val(
		ENDPOINT_NUM,
		ZB_ZCL_CLUSTER_ID_TEMP_MEASUREMENT,
		ZB_ZCL_CLUSTER_SERVER_ROLE,
		ZB_ZCL_ATTR_TEMP_MEASUREMENT_VALUE_ID,
		(zb_uint8_t *)&temp,
		ZB_FALSE);

	zb_zcl_set_attr_val(
		ENDPOINT_NUM,
		ZB_ZCL_CLUSTER_ID_REL_HUMIDITY_MEASUREMENT,
		ZB_ZCL_CLUSTER_SERVER_ROLE,
		ZB_ZCL_ATTR_REL_HUMIDITY_MEASUREMENT_VALUE_ID,
		(zb_uint8_t *)&hum,
		ZB_FALSE);

	zb_zcl_set_attr_val(
		ENDPOINT_NUM,
		ZB_ZCL_CLUSTER_ID_PRESSURE_MEASUREMENT,
		ZB_ZCL_CLUSTER_SERVER_ROLE,
		ZB_ZCL_ATTR_PRESSURE_MEASUREMENT_VALUE_ID,
		(zb_uint8_t *)&press,
		ZB_FALSE);

	float volt_cr = 0.0f;
	zb_uint8_t battery_level = ZB_ZCL_POWER_CONFIG_BATTERY_REMAINING_UNKNOWN;
	if (read_vdd(&volt_cr))
	{
		LOG_INF("error read voltage.");
	}
	else
	{
		battery_level = cr2032_CalculateLevel((int)(volt_cr * 1000.0));
		// LOG_INF("volt level = %d", battery_level);
	}

	zb_zcl_set_attr_val(
		ENDPOINT_NUM,
		ZB_ZCL_CLUSTER_ID_POWER_CONFIG,
		ZB_ZCL_CLUSTER_SERVER_ROLE,
		ZB_ZCL_ATTR_POWER_CONFIG_BATTERY_PERCENTAGE_REMAINING_ID,
		&battery_level,
		ZB_FALSE);

	LOG_INF("Data t = %.1fC, h = %.0f%%, p = %.1fhPa, V = %.1fV", temp_bme, hum_bme, press_bme, volt_cr);
	zb_buf_get_out_delayed_ext(send_attribute_report, 0, 0);
}

uint8_t cr2032_CalculateLevel(uint16_t voltage)
{
	uint32_t res = 0;
	uint8_t i;

	for (i = 0; i < (sizeof(volt_cap) / sizeof(voltage_capacity)); i++)
	{
		if (voltage > volt_cap[i].voltage)
		{
			if (i == 0)
			{
				return volt_cap[0].capacity;
			}
			else
			{
				res = (voltage - volt_cap[i].voltage) * (volt_cap[i - 1].capacity - volt_cap[i].capacity) / (volt_cap[i - 1].voltage - volt_cap[i].voltage);
				res += volt_cap[i].capacity;
				return (uint8_t)res;
			}
		}
	}
	// Below the minimum voltage in the table.
	return volt_cap[sizeof(volt_cap) / sizeof(voltage_capacity) - 1].capacity;
}

int main(void)
{
	LOG_INF("Starting Device");

	k_work_init_delayable(&reboot_work, reboot_work_handler);
	if (!configure_gpio())
	{
	};

	k_timer_start(&led_timer, K_NO_WAIT, K_NO_WAIT);
	register_factory_reset_button(MY_BUTTON_MASK);
	zigbee_erase_persistent_storage(ERASE_PERSISTENT_CONFIG);
	zb_set_ed_timeout(ED_AGING_TIMEOUT_64MIN);
	zb_set_keepalive_timeout(ZB_MILLISECONDS_TO_BEACON_INTERVAL(3600 * 1000));

	// configure for lowest power
	zigbee_configure_sleepy_behavior(true);
	power_down_unused_ram();

	// register switch device context (endpoints)
	ZB_AF_REGISTER_DEVICE_CTX(&NORDIC_DIY_ctx);

	// initialize application clusters
	app_clusters_attr_init();

	if (bme280_init(i2c_dev) != 0)
	{
		LOG_ERR("error init bme280");
	}
	if (saadc_init() != 0)
	{
		LOG_ERR("error init saadc");
	}

	// initialize read battery voltage timer
	k_timer_init(&read_data_timer, read_data_cb, NULL);

	// start Zigbee default thread
	zigbee_enable();

	LOG_INF("Device started");

	//  suspend main thread
	while (1)
	{
		k_sleep(K_FOREVER);
	}
}
