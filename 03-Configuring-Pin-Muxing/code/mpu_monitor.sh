#!/bin/sh
#
# mpu_monitor.sh - MPU6500 IIO Data Monitor
#
# Demonstrates:
# - IIO (Industrial I/O) subsystem Sysfs interface
# - Auto device node detection
# - awk-based floating-point unit conversion
#
# Usage:
#   adb push mpu_monitor.sh /root/
#   adb shell chmod +x /root/mpu_monitor.sh
#   adb shell /root/mpu_monitor.sh
#
# Prerequisites:
#   - inv-mpu6050-i2c kernel module loaded
#   - MPU6500 detected at I2C address 0x68 (shows as UU in i2cdetect)

IIO_PATH=""

# Auto-detect IIO device path by name
for dev in /sys/bus/iio/devices/iio:device*; do
    if [ "$(cat $dev/name)" = "inv-mpu6050" ]; then
        IIO_PATH=$dev
        break
    fi
done

if [ -z "$IIO_PATH" ]; then
    echo "Error: MPU6500 IIO device not found. Check driver loading."
    exit 1
fi

echo "Found device: $(cat $IIO_PATH/name) at $IIO_PATH"

# IIO scale: raw_value * scale = m/s^2
SCALE=$(cat $IIO_PATH/in_accel_scale)
echo "Scale factor: $SCALE m/s^2 per LSB"
echo "------------------------------------------"
echo "  X-Accel    |    Y-Accel    |    Z-Accel  "
echo "------------------------------------------"

while true; do
    RAW_X=$(cat $IIO_PATH/in_accel_x_raw)
    RAW_Y=$(cat $IIO_PATH/in_accel_y_raw)
    RAW_Z=$(cat $IIO_PATH/in_accel_z_raw)

    awk -v x=$RAW_X -v y=$RAW_Y -v z=$RAW_Z -v s=$SCALE 'BEGIN {
        printf "\r %8.3f   |   %8.3f   |   %8.3f m/s^2", x*s, y*s, z*s
    }'

    sleep 0.2
done
