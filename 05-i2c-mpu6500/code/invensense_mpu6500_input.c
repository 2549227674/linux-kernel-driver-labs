// SPDX-License-Identifier: GPL-2.0
/*
 * invensense_mpu6500_input.c - MPU6500 Polled Input Driver
 *
 * Demonstrates:
 * - input_polled_dev framework (Linux 4.9 polling input subsystem)
 * - Bridge between I2C hardware layer and Linux input subsystem
 * - EV_ABS absolute coordinate reporting
 * - devm_ resource management
 *
 * Author: Han
 * Course: Bootlin Embedded Linux Kernel/Driver Development
 */

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/mod_devicetable.h>
#include <linux/delay.h>
#include <linux/input.h>
#include <linux/input-polldev.h>

/* MPU6500 register addresses */
#define MPU6500_REG_WHO_AM_I    0x75
#define MPU6500_REG_PWR_MGMT_1  0x6B
#define MPU6500_ACCEL_XOUT_H    0x3B

#define MPU6500_WHOAMI_VALUE    0x70
#define MPU6500_WAKEUP_VALUE    0x00
#define MPU6500_SLEEP_VALUE     0x40

/* Device private data: bridges I2C client and polling input device */
struct mpu6500_dev {
    struct i2c_client *client;
    struct input_polled_dev *poll_dev;
};

/* Burst read: write register address, then read 6 bytes (X/Y/Z axes) */
static int mpu6500_read_accel(struct i2c_client *client, short *x, short *y, short *z)
{
    u8 reg_addr = MPU6500_ACCEL_XOUT_H;
    u8 data[6];
    struct i2c_msg msgs[2];
    int ret;

    msgs[0].addr  = client->addr;
    msgs[0].flags = 0;
    msgs[0].len   = 1;
    msgs[0].buf   = &reg_addr;

    msgs[1].addr  = client->addr;
    msgs[1].flags = I2C_M_RD;
    msgs[1].len   = 6;
    msgs[1].buf   = data;

    ret = i2c_transfer(client->adapter, msgs, 2);
    if (ret != 2)
        return -EIO;

    /* Big-Endian assembly */
    *x = (data[0] << 8) | data[1];
    *y = (data[2] << 8) | data[3];
    *z = (data[4] << 8) | data[5];

    return 0;
}

/* Poll callback: called periodically by kernel workqueue */
static void mpu6500_poll(struct input_polled_dev *poll_dev)
{
    struct mpu6500_dev *mpu = poll_dev->private;
    short ax, ay, az;

    if (mpu6500_read_accel(mpu->client, &ax, &ay, &az) != 0)
        return;

    /* Report absolute X/Y/Z to input subsystem (evtest can read these) */
    input_report_abs(poll_dev->input, ABS_X, ax);
    input_report_abs(poll_dev->input, ABS_Y, ay);
    input_report_abs(poll_dev->input, ABS_Z, az);
    input_sync(poll_dev->input);
}

static int mpu6500_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
    struct mpu6500_dev *mpu;
    struct input_polled_dev *poll_dev;
    struct input_dev *input;
    int ret;
    int who_am_i;

    pr_info("MPU6500: Probe started at addr 0x%02x\n", client->addr);

    who_am_i = i2c_smbus_read_byte_data(client, MPU6500_REG_WHO_AM_I);
    if (who_am_i != MPU6500_WHOAMI_VALUE) {
        pr_err("MPU6500: Device ID mismatch! expected=0x70 got=0x%02x\n", who_am_i);
        return -ENODEV;
    }

    ret = i2c_smbus_write_byte_data(client, MPU6500_REG_PWR_MGMT_1, MPU6500_WAKEUP_VALUE);
    if (ret < 0) return ret;

    mpu = devm_kzalloc(&client->dev, sizeof(*mpu), GFP_KERNEL);
    if (!mpu) return -ENOMEM;

    poll_dev = devm_input_allocate_polled_device(&client->dev);
    if (!poll_dev) return -ENOMEM;

    mpu->client    = client;
    mpu->poll_dev  = poll_dev;
    poll_dev->private = mpu;
    i2c_set_clientdata(client, mpu);

    poll_dev->poll        = mpu6500_poll;
    poll_dev->poll_interval = 50;  /* 50ms interval = 20Hz */

    input = poll_dev->input;
    input->name = "MPU6500 Accelerometer";
    input->id.bustype = BUS_I2C;

    set_bit(EV_ABS, input->evbit);
    input_set_abs_params(input, ABS_X, -32768, 32767, 8, 0);
    input_set_abs_params(input, ABS_Y, -32768, 32767, 8, 0);
    input_set_abs_params(input, ABS_Z, -32768, 32767, 8, 0);

    ret = input_register_polled_device(poll_dev);
    if (ret) {
        pr_err("MPU6500: Failed to register polled device\n");
        return ret;
    }

    pr_info("MPU6500: Input device registered successfully!\n");
    return 0;
}

static int mpu6500_remove(struct i2c_client *client)
{
    i2c_smbus_write_byte_data(client, MPU6500_REG_PWR_MGMT_1, MPU6500_SLEEP_VALUE);
    pr_info("MPU6500: Device put to sleep. Driver removed.\n");
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
MODULE_DESCRIPTION("MPU6500 Polled Input Driver for i.MX6ULL");
MODULE_LICENSE("GPL");
