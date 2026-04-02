#ifndef BME280_H
#define BME280_H

#include <zephyr/device.h>

int bme280_init(const struct device *i2c_dev);
int bme280_read_data(const struct device *i2c_dev, float *temperature, float *pressure, float *humidity);

#endif /* BME280_H */