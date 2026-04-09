// SPDX-License-Identifier: GPL-2.0
/*
 * invensense_mpu6500_i2c.c - I2C Driver for MPU6500 Accelerometer
 *
 * Demonstrates:
 * - I2C driver model (i2c_driver, probe/remove)
 * - I2C burst read (two-message protocol: write register addr, read data)
 * - Big-Endian data assembly
 * - WHO_AM_I hardware identity check
 *
 * Author: Han
 * Course: Bootlin Embedded Linux Kernel/Driver Development
 */

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/mod_devicetable.h>
#include <linux/delay.h>

/* MPU6500 register addresses */
#define MPU6500_REG_WHO_AM_I    0x75
#define MPU6500_REG_PWR_MGMT_1  0x6B
#define MPU6500_ACCEL_XOUT_H    0x3B

#define MPU6500_WHOAMI_VALUE    0x70
#define MPU6500_WAKEUP_VALUE    0x00
#define MPU6500_SLEEP_VALUE     0x40

/* Burst read: send register address, then read 6 bytes (X/Y/Z axes) */
static int mpu6500_read_accel(struct i2c_client *client, short *x, short *y, short *z)
{
    u8 reg_addr = MPU6500_ACCEL_XOUT_H;
    u8 data[6];
    struct i2c_msg msgs[2];
    int ret;

    msgs[0].addr  = client->addr;
    msgs[0].flags = 0;         /* write */
    msgs[0].len   = 1;
    msgs[0].buf   = &reg_addr;

    msgs[1].addr  = client->addr;
    msgs[1].flags = I2C_M_RD; /* read */
    msgs[1].len   = 6;
    msgs[1].buf   = data;

    ret = i2c_transfer(client->adapter, msgs, 2);
    if (ret != 2) {
        pr_err("MPU6500: I2C transfer failed (ret=%d)\n", ret);
        return -EIO;
    }

    /* MPU6500 is Big-Endian: high byte first */
    *x = (data[0] << 8) | data[1];
    *y = (data[2] << 8) | data[3];
    *z = (data[4] << 8) | data[5];

    return 0;
}

static int mpu6500_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
    short ax, ay, az;
    int who_am_i;
    int ret;

    pr_info("MPU6500: Probe called for address 0x%02x\n", client->addr);

    who_am_i = i2c_smbus_read_byte_data(client, MPU6500_REG_WHO_AM_I);
    if (who_am_i < 0 || who_am_i != MPU6500_WHOAMI_VALUE) {
        pr_err("MPU6500: WHO_AM_I check failed! got=0x%02x\n", who_am_i);
        return -ENODEV;
    }
    pr_info("MPU6500: Device ID verified: 0x%02x\n", who_am_i);

    ret = i2c_smbus_write_byte_data(client, MPU6500_REG_PWR_MGMT_1, MPU6500_WAKEUP_VALUE);
    if (ret < 0) return ret;
    pr_info("MPU6500: Device woken up.\n");

    if (mpu6500_read_accel(client, &ax, &ay, &az) == 0)
        pr_info("MPU6500: Raw Data -> X:%d, Y:%d, Z:%d\n", ax, ay, az);

    return 0;
}

static int mpu6500_remove(struct i2c_client *client)
{
    i2c_smbus_write_byte_data(client, MPU6500_REG_PWR_MGMT_1, MPU6500_SLEEP_VALUE);
    pr_info("MPU6500: Device is now sleeping. Removed.\n");
    return 0;
}

static const struct of_device_id mpu6500_of_match[] = {
    { .compatible = "my,custom-mpu6500", },
    { }
};
MODULE_DEVICE_TABLE(of, mpu6500_of_match);

static const struct i2c_device_id mpu6500_id[] = {
    { "mpu6500", 0 },
    { }
};
MODULE_DEVICE_TABLE(i2c, mpu6500_id);

static struct i2c_driver mpu6500_driver = {
    .driver = {
        .name           = "invensense_mpu6500",
        .of_match_table = mpu6500_of_match,
    },
    .probe    = mpu6500_probe,
    .remove   = mpu6500_remove,
    .id_table = mpu6500_id,
};

module_i2c_driver(mpu6500_driver);

MODULE_AUTHOR("Han");
MODULE_DESCRIPTION("MPU6500 I2C Driver for i.MX6ULL");
MODULE_LICENSE("GPL");
