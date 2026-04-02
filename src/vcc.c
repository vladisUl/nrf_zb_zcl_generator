#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/logging/log.h>
#include <stdint.h>
#include "vcc.h"

#define ADC_RESOLUTION 10
#define ADC_GAIN ADC_GAIN_1_6
#define ADC_REFERENCE ADC_REF_INTERNAL
#define ADC_CHANNEL_ID 0
#define ADC_BUFFER_SIZE 1

LOG_MODULE_REGISTER(vcc, LOG_LEVEL_INF);

static const struct device *adc_dev;
static int16_t sample_buffer[ADC_BUFFER_SIZE];

static const struct adc_channel_cfg adc_cfg = {
    .gain = ADC_GAIN,
    .reference = ADC_REFERENCE,
    .acquisition_time = ADC_ACQ_TIME(ADC_ACQ_TIME_MICROSECONDS, 10),
    .channel_id = ADC_CHANNEL_ID,
    .input_positive = SAADC_CH_PSELP_PSELP_VDD, // measure VDD
};

int saadc_init(void)
{
    adc_dev = DEVICE_DT_GET(DT_NODELABEL(adc));
    if (!device_is_ready(adc_dev))
    {
        LOG_WRN("ADC device not ready\n");
        return -1;
    }

    int err = adc_channel_setup(adc_dev, &adc_cfg);
    if (err < 0)
    {
        LOG_ERR("ADC channel setup failed: %d\n", err);
        return err;
    }

    LOG_INF("SAADC initialized successfully\n");
    return 0;
}

int read_vdd(float *voltage)
{
    if (!adc_dev)
    {
        LOG_WRN("ADC device not initialized\n");
        return -1;
    }

    struct adc_sequence sequence = {
        .options = NULL,
        .channels = BIT(ADC_CHANNEL_ID),
        .buffer = sample_buffer,
        .buffer_size = sizeof(sample_buffer),
        .resolution = ADC_RESOLUTION,
        .oversampling = 0,
        .calibrate = false,
    };

    int err = adc_read(adc_dev, &sequence);
    if (err < 0)
    {
        LOG_ERR("ADC read error: %d\n", err);
        return err;
    }

    int32_t raw_value = sample_buffer[0];
    *voltage = (raw_value * 3.6) / (1 << ADC_RESOLUTION); // Рассчитываем напряжение

    return 0;
}
