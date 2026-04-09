# Configuring Pin Multiplexing

## 实验目标

在实验 6 的基础上，通过 pinctrl 子系统将 I2C1 复用至 CSI 摄像头引脚（而非默认引脚），解决 CSI 引脚与 UART6 的引脚冲突，并验证自定义引脚复用的设备树写法。

## 知识点

- NXP i.MX6ULL pinctrl 两级节点结构：`container_node` → `actual_pin_group`
- `MX6UL_PAD_CSI_PIXCLK__I2C1_SCL` / `MX6UL_PAD_CSI_MCLK__I2C1_SDA`：引脚宏定义
- Pad 配置值 `0x4001b8b1`：启用开漏 + 内部上拉（I2C 安全配置）
- `/delete-property/`：移除从父节点继承的 pinctrl 属性，避免配置冲突
- `status = "disabled"`：禁用占用引脚的 UART6 节点
- IIO Sysfs 接口：`/sys/bus/iio/devices/iio:device*/in_accel_*_raw`
- `awk` 浮点单位转换：`raw × scale = m/s²`

## 代码说明

| 文件 | 说明 |
|------|------|
| `code/imx6ull-100ask-custom.dts` | 完整设备树（两级 pinctrl + CSI I2C1 + MPU6500） |
| `code/mpu_monitor.sh` | IIO Sysfs 自动检测 + 加速度数据监控脚本 |

## 设备树关键修改

```dts
/* NXP 两级 pinctrl 结构（驱动要求） */
&iomuxc {
    my_board_grp {
        pinctrl_my_i2c1: my_i2c1grp {
            fsl,pins = <
                /* CSI_PIXCLK 复用为 I2C1_SCL，pad=0x4001b8b1 */
                MX6UL_PAD_CSI_PIXCLK__I2C1_SCL   0x4001b8b1
                /* CSI_MCLK 复用为 I2C1_SDA */
                MX6UL_PAD_CSI_MCLK__I2C1_SDA     0x4001b8b1
            >;
        };
    };
};

/* 释放 CSI 引脚（与 UART6 冲突） */
&uart6 {
    status = "disabled";
};

/* I2C1 使用自定义 CSI 引脚 */
&i2c1 {
    /delete-property/ pinctrl-names;  /* 删除继承的 pinctrl */
    /delete-property/ pinctrl-0;
    pinctrl-names = "default";
    pinctrl-0 = <&pinctrl_my_i2c1>;   /* 应用自定义引脚组 */
    clock-frequency = <100000>;       /* 100kHz I2C 标准模式 */
    status = "okay";
};
```

## Pad 配置 0x4001b8b1 详解

```
bit [31:16] = 0x4001  (SION + 操作码)
bit [15:0]  = 0xb8b1  (HYS=1, PUE=1, PKE=1, ODE=1, ... → 开漏+上拉)
SION: Software Input On (强制作为输入)
ODE: Open Drain Enable (开漏，I2C 必须)
PUE/PKE: Pull enable / Keeper (上拉)
```

## 验证

```bash
# 编译设备树
make dtbs

# 部署
adb push arch/arm/boot/dts/imx6ull-100ask-custom.dtb /boot/
adb shell reboot

# 验证 I2C1 在 CSI 引脚上工作
adb shell i2cdetect -y 1
# 预期：0x68 处显示 "UU"（内核 mpu6050 驱动已占用）

# 推送并运行 IIO 监控脚本
adb push mpu_monitor.sh /root/
adb shell chmod +x /root/mpu_monitor.sh
adb shell /root/mpu_monitor.sh
# 输出：X/Y/Z 加速度值（m/s²），每 0.2 秒刷新
```

## 关键设计

- 两级节点结构：`my_board_grp` 容器节点 → `my_i2c1grp` 引脚组（无容器则 NXP pinctrl 驱动返回 `-EINVAL`）
- `ODE=1`（开漏）：I2C 要求总线可被任意设备拉低，开漏 + 上拉电阻实现线与逻辑
- `/delete-property/` 放在 `&i2c1` 内部：在覆盖节点之前先删除原有属性，顺序很重要
