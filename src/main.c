#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>
#include <ram_pwrdn.h>
#include <dk_buttons_and_leds.h>
#include <zboss_api.h>
#include <zboss_api_addons.h>
#include <zb_zcl_reporting.h>
#include <zigbee/zigbee_app_utils.h>
#include <zigbee/zigbee_error_handler.h>
#include <zb_nrf_platform.h>

#include "zephyr_drivers.h"
#include "zb_zcl_struct.h"

void zboss_signal_handler(zb_bufid_t bufid);
static int configure_gpio(void);
static void button_handler(uint32_t button_state, uint32_t has_changed);
static void read_data_cb(struct k_timer *timer);
static void read_data_handler(struct k_work *work);
static void send_attribute_report(zb_bufid_t bufid, zb_uint16_t cmd_id);
static void enter_deep_sleep_work_handler(struct k_work *work);

K_WORK_DEFINE(read_data_work, read_data_handler);
K_WORK_DELAYABLE_DEFINE(deep_sleep_work, enter_deep_sleep_work_handler);
struct k_timer read_data_timer;

LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);

static void enter_deep_sleep_work_handler(struct k_work *work)
{
	LOG_INF("Configuration window closed. Setting long poll to 10 minutes.");
	zb_zdo_pim_set_long_poll_interval(LONG_POLL_INTERVAL);
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
			k_work_reschedule(&deep_sleep_work, K_SECONDS(30));
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

	if (bufid)
	{
		zb_buf_free(bufid);
	}

	bool thisJoin = ZB_JOINED();
	if ((lastJoin == false) && (thisJoin == true))
	{
		LOG_INF("joined network!");
		LED_BLINK_STOP();
		set_led_off(&led);
		k_timer_start(&read_data_timer, READ_DATA_INITIAL_DELAY, READ_DATA_TIMER_PERIOD);
	}
	else if ((lastJoin == true) && (thisJoin == false))
	{
		LOG_INF("left network!");
		LED_BLINK(K_NO_WAIT, K_NO_WAIT);
	}
	lastJoin = thisJoin;
}

static int configure_gpio(void)
{
	int ret;
	k_timer_init(&led_timer, led_timer_handler, NULL);
	LED_BLINK(K_NO_WAIT, K_NO_WAIT);
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

void button_handler(uint32_t button_state, uint32_t has_changed)
{
	LOG_INF("button_handler %d", button_state);

	user_input_indicate();

	check_factory_reset_button(button_state, has_changed);
	if (MY_BUTTON_MASK & has_changed & ~button_state)
	{
		if (!was_factory_reset_done())
		{
			//user_short_button_action();
		}
	}
}

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
	// Read your data here
	// read_your_data(&your_data);
	// zb_zcl_set_attr_val(
	// 	ENDPOINT_NUM,
	// 	ZB_ZCL_CLUSTER_ID_TEMP_MEASUREMENT,
	// 	ZB_ZCL_CLUSTER_SERVER_ROLE,
	// 	ZB_ZCL_ATTR_TEMP_MEASUREMENT_VALUE_ID,
	// 	(zb_uint8_t *)&your_data,
	// 	ZB_FALSE);

	zb_buf_get_out_delayed_ext(send_attribute_report, 0, 0);
}

int main(void)
{
	LOG_INF("Starting...");
	if (!configure_gpio())
	{
		LOG_INF("Init GPIO");
	}
	else
	{
		LOG_INF("ERROR init GPIO");
	}

	register_factory_reset_button(MY_BUTTON_MASK);
	zigbee_erase_persistent_storage(ERASE_PERSISTENT_CONFIG);
	zb_set_ed_timeout(ED_AGING_TIMEOUT_64MIN);
	zb_set_keepalive_timeout(ZB_MILLISECONDS_TO_BEACON_INTERVAL(3600 * 1000));

	// configure for lowest power
	zigbee_configure_sleepy_behavior(true);
	power_down_unused_ram();

	// initialize application clusters
	app_clusters_attr_init();

	// initialize read data  timer
	k_timer_init(&read_data_timer, read_data_cb, NULL);

	zigbee_enable();

	LOG_INF("Device started");

	while (1)
	{
		k_sleep(K_FOREVER);
	}
}
