# Input Interface

## 实验目标

将 MPU6500 加速度计桥接到 Linux input 子系统，通过 `input_polled_dev` 框架实现定时轮询上报 X/Y/Z 加速度数据，使标准 input 工具（evtest）可直接读取硬件数据。

## 知识点

- `input_polled_dev` 框架（Linux 4.9 专用）：轮询 input 设备注册
- `EV_ABS` / `ABS_X/Y/Z`：绝对坐标事件类型
- `input_report_abs` / `input_sync`：向 input 子系统报告数据
- Bridge 模式：I2C 物理层 + input 逻辑层
- `input_set_abs_params`：设置事件值的范围和分辨率

## 代码结构图解

### 四层抽象架构

```mermaid
graph TB
    A["硬件层<br>MPU6500 加速度计"] -->|I2C 总线读取| B["驱动层<br>mpu6500_read_accel<br>(burst read 6 字节)"]
    B --> C["input_polled_dev<br>poll 回调 50ms"]
    C --> D["Input 子系统<br>EV_ABS 事件队列"]
    D --> E["用户空间<br>evtest /dev/input/eventX"]
```

### 数据报告时序

```mermaid
sequenceDiagram
    participant WQ as 内核工作队列
    participant Poll as mpu6500_poll
    participant I2C as I2C 总线
    participant Input as Input 子系统
    participant User as evtest

    WQ->>Poll: 每 50ms 调用一次 poll()
    Poll->>I2C: 读 6 字节加速度数据
    I2C-->>Poll: ax, ay, az (Big-Endian)
    Poll->>Input: input_report_abs(ABS_X, ax)
    Poll->>Input: input_report_abs(ABS_Y, ay)
    Poll->>Input: input_report_abs(ABS_Z, az)
    Poll->>Input: input_sync() 帧结束
    Input-->>User: /dev/input/eventX 可读取
```

### EV_ABS 事件报告序列

```mermaid
graph LR
    A["input_report_abs<br>ABS_X"] --> B["input_report_abs<br>ABS_Y"]
    B --> C["input_report_abs<br>ABS_Z"]
    C --> D["input_sync<br>一帧结束标志"]
```

### 设备注册与注销

```mermaid
graph TD
    A["devm_kzalloc<br>分配私有结构"] --> B["devm_input_allocate_polled_device<br>分配轮询 input 设备"]
    B --> C["设置 poll 回调函数<br>+ poll_interval = 50ms"]
    C --> D["set_bit EV_ABS<br>+ set_abs_params"]
    D --> E["input_register_polled_device<br>注册成功"]
    E --> F["设备自动移除<br>devm_ 自动释放"]
```

## 代码说明

| 文件 | 说明 |
|------|------|
| `code/invensense_mpu6500_input.c` | Polled Input 驱动（桥接到 Linux input 子系统） |
| `code/Makefile` | Out-of-tree 构建脚本 |

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

| 设计点 | 说明 |
|--------|------|
| `input_polled_dev` | 内核工作队列定期调度 poll 回调，无需自己管理定时器 |
| `poll_interval = 50` | 50ms 间隔 = 20Hz 采样率 |
| `EV_ABS` + `input_set_abs_params` | 声明绝对坐标事件类型和轴范围 (-32768~32767) |
| `input_sync` | 每帧事件结束标志，通知用户空间数据就绪 |
| `devm_input_allocate_polled_device` | 设备卸载时自动释放，remove 只需休眠硬件 |
