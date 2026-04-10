# Using the I2C Bus

## 实验目标

在 I2C 总线上驱动 MPU6500 六轴 IMU，实现裸 I2C 驱动，掌握 `i2c_driver` 框架、burst read 协议和 WHO_AM_I 硬件身份验证。

## 知识点

- `i2c_driver` / `i2c_client` / `i2c_adapter`：I2C 总线驱动模型
- `i2c_transfer` 两消息协议：先发寄存器地址，再读取数据（burst read）
- `i2c_smbus_read_byte_data` / `i2c_smbus_write_byte_data`：SMBus 便捷 API
- WHO_AM_I 寄存器（0x75）验证，期望值 0x70
- Big-Endian 数据组装：高字节在前

## 代码结构图解

### I2C 驱动模型

```mermaid
graph TD
    A["i2c_driver 驱动"] --> B["probe / remove"]
    A --> C["of_match_table / id_table<br>匹配设备"]
    B --> D["设备树 mpu6500@68<br>匹配成功"]
    D --> E["内核自动调用 probe"]
    E --> F["WHO_AM_I 验证 → 0x70"]
    F --> G["唤醒设备<br>读取加速度数据"]
    E --> H["remove: 设备休眠"]
```

### Burst Read 两消息协议

```mermaid
sequenceDiagram
    participant Host as 主机 (I2C 适配器)
    participant Bus as I2C 总线
    participant MPU as MPU6500

    Host->>Bus: S ADDR+W ACK
    Host->>MPU: 0x3B (ACCEL_XOUT_H 寄存器地址) NACK
    Bus->>MPU: 停止信号
    Host->>Bus: S ADDR+R ACK
    MPU-->>Bus: AXH → AXL → AYH → AYL → AZH → AZL (6 字节)
    Host-->>Bus: 停止信号 (NACK)
    Note over Host: 数据组装:<br>ax = (data[0]<<8) | data[1]
```

### WHO_AM_I 身份验证流程

```mermaid
graph TD
    A["probe 函数"] --> B["读 0x75 寄存器<br>WHO_AM_I"]
    B --> C{"返回值 == 0x70?"}
    C -->|是| D["身份正确，继续唤醒设备"]
    C -->|否| E["return -ENODEV<br>驱动不绑定"]
    D --> F["写 PWR_MGMT_1 = 0x00<br>唤醒传感器"]
    F --> G["burst read 读取 X/Y/Z 加速度"]
```

### Big-Endian 数据组装

```mermaid
graph LR
    A["寄存器 0x3B"] --> B["data[0]: X 高字节"]
    B --> C["data[1]: X 低字节"]
    A1["寄存器 0x3D"] --> D["data[2]: Y 高字节"]
    A1 --> E["data[3]: Y 低字节"]
    A2["寄存器 0x3F"] --> F["data[4]: Z 高字节"]
    A2 --> G["data[5]: Z 低字节"]
```

## 代码说明

| 文件 | 说明 |
|------|------|
| `code/invensense_mpu6500_i2c.c` | 裸 I2C 驱动（probe 中读取原始数据） |
| `code/Makefile` | Out-of-tree 构建脚本 |

## 验证

```bash
make
adb push invensense_mpu6500_i2c.ko /root/
adb shell insmod /root/invensense_mpu6500_i2c.ko
adb shell dmesg | grep MPU6500
```

## 关键设计

| 设计点 | 说明 |
|--------|------|
| `i2c_transfer` + `i2c_msg[2]` | 显式构造两消息 burst read（写地址 → 读数据） |
| WHO_AM_I 验证 | 读 0x75 寄存器，匹配 0x70 才继续初始化 |
| `module_i2c_driver` | 自动注册/注销驱动模块 |
| Big-Endian | MPU6500 高字节在前：`ax = (data[0] << 8) | data[1]` |
