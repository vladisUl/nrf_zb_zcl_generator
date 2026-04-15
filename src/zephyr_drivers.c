#include "zephyr_drivers.h"
#include <zephyr/logging/log.h>
#include <zephyr/pm/device.h>

const struct device *leds = DEVICE_DT_GET(DT_NODELABEL(leds));

LOG_MODULE_REGISTER(zephyr_drivers, LOG_LEVEL_INF);



//RTIO_DEFINE(ctx, 1, 1);

void set_led_off(const struct gpio_dt_spec *led)
{
    gpio_pin_set_dt(led, 0);
}

void set_led_on(const struct gpio_dt_spec *led)
{
    gpio_pin_set_dt(led, 1);
}

void led_timer_handler(struct k_timer *timer)
{
    ARG_UNUSED(timer);

    static int led_on_state;
    gpio_pin_set_dt(&led, !((++led_on_state) % 100));
    k_timer_start(&led_timer, K_MSEC(10), K_NO_WAIT);
}
