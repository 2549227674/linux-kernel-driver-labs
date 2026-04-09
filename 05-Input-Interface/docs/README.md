# Input Interface

## 实验目标

将 MPU6500 加速度计桥接到 Linux input 子系统，通过 `input_polled_dev` 框架实现定时轮询上报 X/Y/Z 加速度数据，使标准 input 工具（evtest）可直接读取硬件数据。

## 知识点

- `input_polled_dev` 框架（Linux 4.9 专用）：轮询 input 设备注册
- `EV_ABS` / `ABS_X/Y/Z`：绝对坐标事件类型
- `input_report_abs` / `input_sync`：向 input 子系统报告数据
- Bridge 模式：I2C 物理层 + input 逻辑层
- `input_set_abs_params`：设置事件值的范围和分辨率
- `devm_kzalloc` / `devm_input_allocate_polled_device`：devm 资源管理

## 代码说明

| 文件 | 说明 |
|------|------|
| `code/invensense_mpu6500_input.c` | Polled Input 驱动（桥接到 Linux input 子系统） |
| `code/Makefile` | Out-of-tree 构建脚本 |

## 数据流

```
mpu6500_poll (定时回调, 50ms) --> mpu6500_read_accel (I2C burst read)
  --> input_report_abs(ABS_X, ax) --> input_report_abs(ABS_Y, ay)
  --> input_report_abs(ABS_Z, az) --> input_sync()
  --> /dev/input/eventX (evtest 可读)
```

## 验证

```bash
make
adb push invensense_mpu6500_input.ko /root/
adb shell insmod /root/invensense_mpu6500_input.ko
adb shell dmesg | grep MPU6500

# 查看 input 设备
adb shell ls -l /sys/class/input/

# 用 evtest 读取加速度数据
adb shell evtest /dev/input/eventX
```

## 关键设计

- `input_polled_dev`：内核工作队列负责定时调度 poll 回调，驱动无需自己管理定时器
- `set_bit(EV_ABS, input->evbit)` + `input_set_abs_params`：必须设置 EV_ABS 才能用 evtest 读取
- `devm_input_allocate_polled_device`：设备卸载时自动释放，无需 remove 回调手动注销
- Big-Endian 数据组装：MPU6500 高字节在前 `ax = (data[0] << 8) | data[1]`
