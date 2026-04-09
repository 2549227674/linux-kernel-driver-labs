# I2C Driver and Input Subsystem (MPU6500)

## 实验目标

在 I2C 总线上驱动 MPU6500 六轴惯性测量单元（加速度计 + 陀螺仪），分别实现裸 I2C 驱动和桥接到 Linux input 子系统的轮询驱动。

## 知识点

- `i2c_driver` / `i2c_client` / `i2c_adapter`：I2C 总线驱动模型
- `i2c_transfer` 两消息协议：先发寄存器地址，再读取数据（burst read）
- `i2c_smbus_read_byte_data` / `i2c_smbus_write_byte_data`：SMBus 便捷 API
- WHO_AM_I 寄存器（0x75）硬件身份验证，期望值 0x70
- Big-Endian 数据组装：大端序高字节在前
- `input_polled_dev` 框架（Linux 4.9 专用）：轮询 input 设备注册
- `EV_ABS` / `ABS_X/Y/Z`：绝对坐标事件类型
- `input_report_abs` / `input_sync`：向 input 子系统报告数据
- `devm_kzalloc` / `devm_input_allocate_polled_device`：devm 资源管理

## 代码说明

| 文件 | 说明 |
|------|------|
| `code/invensense_mpu6500_i2c.c` | 裸 I2C 驱动（probe 中读取原始数据） |
| `code/invensense_mpu6500_input.c` | Polled Input 驱动（桥接到 Linux input 子系统） |
| `code/Makefile` | Out-of-tree 构建脚本 |

## 数据流

```
inv_mpu6500_input:
  mpu6500_poll (定时回调) --> mpu6500_read_accel (I2C burst read)
  --> input_report_abs --> input_sync --> /dev/input/eventX (evtest 可读)

inv_mpu6500_i2c:
  probe --> i2c_smbus WHO_AM_I 验证 --> i2c_smbus 唤醒设备
  --> mpu6500_read_accel (burst read) --> dmesg 打印原始数据
```

## I2C Burst Read 协议

```
Host --[reg_addr:0x3B]--> MPU6500  (write, 1 byte)
Host <--[6 bytes: AXH/AXL/AYH/AYL/AZH/AZL]--  (read)
MPU6500 为 Big-Endian：高字节在前
```

## 验证

```bash
# 编译两个驱动
make

# 加载 I2C 驱动（仅读取验证）
adb shell insmod /root/invensense_mpu6500_i2c.ko
adb shell dmesg | grep MPU6500

# 加载 Polled Input 驱动
adb shell insmod /root/invensense_mpu6500_input.ko
adb shell dmesg | grep MPU6500

# 查看 input 设备
adb shell ls -l /sys/class/input/

# 用 evtest 读取加速度数据（需要 evtest 二进制）
adb shell evtest /dev/input/eventX
```

## 关键设计

- `i2c_transfer` + `struct i2c_msg[2]`：显式构造两消息 burst read，不依赖 SMBus block read
- `input_polled_dev`：Linux 4.9 专用框架，poll_interval=50ms（20Hz），内核工作队列负责调度
- `set_bit(EV_ABS, input->evbit)` + `input_set_abs_params`：必须设置 EV_ABS 才能用 evtest 读取
- `devm_input_allocate_polled_device`：设备卸载时自动释放，无需 remove 回调手动注销
