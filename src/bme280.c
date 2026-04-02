#include "bme280.h"

#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <stdint.h>

LOG_MODULE_REGISTER(bme280, LOG_LEVEL_INF);

#define BME280_I2C_ADDR 0x77
#define BME280_CHIP_ID_REG 0xD0
#define BME280_CHIP_ID 0x58
#define BME280_RESET_REG 0xE0
#define BME280_RESET_CMD 0xB6
#define BME280_CTRL_HUM_REG 0xF2
#define BME280_STATUS_REG 0xF3
#define BME280_CTRL_MEAS_REG 0xF4
#define BME280_CONFIG_REG 0xF5
#define BME280_DATA_REG 0xF7

struct bme280_calib_data
{
    uint16_t dig_T1;
    int16_t dig_T2;
    int16_t dig_T3;

    uint16_t dig_P1;
    int16_t dig_P2;
    int16_t dig_P3;
    int16_t dig_P4;
    int16_t dig_P5;
    int16_t dig_P6;
    int16_t dig_P7;
    int16_t dig_P8;
    int16_t dig_P9;

    uint8_t dig_H1;
    int16_t dig_H2;
    uint8_t dig_H3;
    int16_t dig_H4;
    int16_t dig_H5;
    int8_t dig_H6;
};

static struct bme280_calib_data calib;
static int32_t t_fine;

static int bme280_read_regs(const struct device *i2c_dev, uint8_t reg, uint8_t *buf, size_t len)
{
    return i2c_write_read(i2c_dev, BME280_I2C_ADDR, &reg, 1, buf, len);
}

static int bme280_write_reg(const struct device *i2c_dev, uint8_t reg, uint8_t value)
{
    uint8_t buf[2] = {reg, value};
    return i2c_write(i2c_dev, buf, sizeof(buf), BME280_I2C_ADDR);
}

static int bme280_read_calibration(const struct device *i2c_dev)
{
    uint8_t buf1[26];
    uint8_t buf2[7];
    int ret;

    ret = bme280_read_regs(i2c_dev, 0x88, buf1, sizeof(buf1));
    if (ret)
    {
        return ret;
    }

    ret = bme280_read_regs(i2c_dev, 0xE1, buf2, sizeof(buf2));
    if (ret)
    {
        return ret;
    }

    calib.dig_T1 = (uint16_t)((buf1[1] << 8) | buf1[0]);
    calib.dig_T2 = (int16_t)((buf1[3] << 8) | buf1[2]);
    calib.dig_T3 = (int16_t)((buf1[5] << 8) | buf1[4]);

    calib.dig_P1 = (uint16_t)((buf1[7] << 8) | buf1[6]);
    calib.dig_P2 = (int16_t)((buf1[9] << 8) | buf1[8]);
    calib.dig_P3 = (int16_t)((buf1[11] << 8) | buf1[10]);
    calib.dig_P4 = (int16_t)((buf1[13] << 8) | buf1[12]);
    calib.dig_P5 = (int16_t)((buf1[15] << 8) | buf1[14]);
    calib.dig_P6 = (int16_t)((buf1[17] << 8) | buf1[16]);
    calib.dig_P7 = (int16_t)((buf1[19] << 8) | buf1[18]);
    calib.dig_P8 = (int16_t)((buf1[21] << 8) | buf1[20]);
    calib.dig_P9 = (int16_t)((buf1[23] << 8) | buf1[22]);

    calib.dig_H1 = buf1[25];
    calib.dig_H2 = (int16_t)((buf2[1] << 8) | buf2[0]);
    calib.dig_H3 = buf2[2];
    calib.dig_H4 = (int16_t)((buf2[3] << 4) | (buf2[4] & 0x0F));
    calib.dig_H5 = (int16_t)((buf2[5] << 4) | (buf2[4] >> 4));
    calib.dig_H6 = (int8_t)buf2[6];

    return 0;
}

static int32_t compensate_temperature(int32_t adc_T)
{
    int32_t var1, var2, T;

    var1 = ((((adc_T >> 3) - ((int32_t)calib.dig_T1 << 1))) * ((int32_t)calib.dig_T2)) >> 11;
    var2 = (((((adc_T >> 4) - ((int32_t)calib.dig_T1)) *
              ((adc_T >> 4) - ((int32_t)calib.dig_T1))) >>
             12) *
            ((int32_t)calib.dig_T3)) >>
           14;

    t_fine = var1 + var2;
    T = (t_fine * 5 + 128) >> 8;
    return T;
}

static uint32_t compensate_pressure(int32_t adc_P)
{
    int64_t var1, var2, p;

    var1 = ((int64_t)t_fine) - 128000;
    var2 = var1 * var1 * (int64_t)calib.dig_P6;
    var2 = var2 + ((var1 * (int64_t)calib.dig_P5) << 17);
    var2 = var2 + (((int64_t)calib.dig_P4) << 35);
    var1 = ((var1 * var1 * (int64_t)calib.dig_P3) >> 8) +
           ((var1 * (int64_t)calib.dig_P2) << 12);
    var1 = (((((int64_t)1) << 47) + var1) * ((int64_t)calib.dig_P1)) >> 33;

    if (var1 == 0)
    {
        return 0;
    }

    p = 1048576 - adc_P;
    p = (((p << 31) - var2) * 3125) / var1;
    var1 = (((int64_t)calib.dig_P9) * (p >> 13) * (p >> 13)) >> 25;
    var2 = (((int64_t)calib.dig_P8) * p) >> 19;

    p = ((p + var1 + var2) >> 8) + (((int64_t)calib.dig_P7) << 4);
    return (uint32_t)p;
}

static uint32_t compensate_humidity(int32_t adc_H)
{
    int32_t v_x1_u32r;

    v_x1_u32r = (t_fine - ((int32_t)76800));
    v_x1_u32r = (((((adc_H << 14) - (((int32_t)calib.dig_H4) << 20) -
                    (((int32_t)calib.dig_H5) * v_x1_u32r)) +
                   ((int32_t)16384)) >>
                  15) *
                 (((((((v_x1_u32r * ((int32_t)calib.dig_H6)) >> 10) *
                      (((v_x1_u32r * ((int32_t)calib.dig_H3)) >> 11) + ((int32_t)32768))) >>
                     10) +
                    ((int32_t)2097152)) *
                       ((int32_t)calib.dig_H2) +
                   8192) >>
                  14));

    v_x1_u32r = (v_x1_u32r -
                 (((((v_x1_u32r >> 15) * (v_x1_u32r >> 15)) >> 7) *
                   ((int32_t)calib.dig_H1)) >>
                  4));

    if (v_x1_u32r < 0)
    {
        v_x1_u32r = 0;
    }
    if (v_x1_u32r > 419430400)
    {
        v_x1_u32r = 419430400;
    }

    return (uint32_t)(v_x1_u32r >> 12);
}

int bme280_init(const struct device *i2c_dev)
{
    uint8_t chip_id = 0;
    int ret;

    ret = bme280_read_regs(i2c_dev, BME280_CHIP_ID_REG, &chip_id, 1);
    if (ret)
    {
        LOG_ERR("Failed to read chip ID");
        return ret;
    }

    if (chip_id != BME280_CHIP_ID)
    {
        LOG_ERR("Unexpected chip ID: 0x%02x", chip_id);
        return -EINVAL;
    }

    ret = bme280_write_reg(i2c_dev, BME280_RESET_REG, BME280_RESET_CMD);
    if (ret)
    {
        LOG_ERR("Failed to reset BME280");
        return ret;
    }

    k_msleep(10);

    ret = bme280_read_calibration(i2c_dev);
    if (ret)
    {
        LOG_ERR("Failed to read calibration");
        return ret;
    }

    ret = bme280_write_reg(i2c_dev, BME280_CTRL_HUM_REG, 0x01);
    if (ret)
    {
        return ret;
    }

    ret = bme280_write_reg(i2c_dev, BME280_CONFIG_REG, 0x00);
    if (ret)
    {
        return ret;
    }

    // ret = bme280_write_reg(i2c_dev, BME280_CTRL_MEAS_REG, 0x27);
    ret = bme280_write_reg(i2c_dev, BME280_CTRL_MEAS_REG, 0x24);
    if (ret)
    {
        return ret;
    }

    LOG_INF("BME280 initialized");
    return 0;
}

int bme280_read_data(const struct device *i2c_dev, float *temperature, float *pressure, float *humidity)
{
    uint8_t data[8];
    int ret;
//=======================
    ret = bme280_write_reg(i2c_dev, BME280_CTRL_HUM_REG, 0x01);
    if (ret)
    {
        LOG_ERR("Failed to set humidity oversampling");
        return ret;
    }

    ret = bme280_write_reg(i2c_dev, BME280_CTRL_MEAS_REG, 0x25);
    if (ret)
    {
        LOG_ERR("Failed to start forced measurement");
        return ret;
    }

    k_msleep(10);
//--------------------
    ret = bme280_read_regs(i2c_dev, BME280_DATA_REG, data, sizeof(data));
    if (ret)
    {
        LOG_ERR("Failed to read BME280 data");
        return ret;
    }

    int32_t adc_P = (int32_t)((((uint32_t)data[0]) << 12) |
                              (((uint32_t)data[1]) << 4) |
                              (((uint32_t)data[2]) >> 4));

    int32_t adc_T = (int32_t)((((uint32_t)data[3]) << 12) |
                              (((uint32_t)data[4]) << 4) |
                              (((uint32_t)data[5]) >> 4));

    int32_t adc_H = (int32_t)((((uint32_t)data[6]) << 8) |
                              ((uint32_t)data[7]));

    int32_t temp_x100 = compensate_temperature(adc_T);
    uint32_t press_q24_8 = compensate_pressure(adc_P);
    uint32_t hum_q22_10 = compensate_humidity(adc_H);

    if (temperature)
    {
        *temperature = temp_x100 / 100.0f;
    }

    if (pressure)
    {
        *pressure = (press_q24_8 / 256.0f) / 100.0f;
    }

    if (humidity)
    {
        *humidity = hum_q22_10 / 1024.0f;
    }

    return 0;
}