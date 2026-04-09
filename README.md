# linux-kernel-driver-labs

i.MX6ULL 嵌入式 Linux 内核驱动实验，包含 8 个由浅入深的实验，覆盖字符设备、内存映射 I/O、中断处理、I2C 驱动、输入子系统、设备树引脚复用和 DMA 引擎。

**硬件平台**：NXP i.MX6ULL (ARM Cortex-A7) + 100ask 开发板
**内核版本**：Linux 4.9.88
**工具链**：arm-linux-gnueabihf-gcc

---

## 实验索引

| # | 实验名称 | 核心知识点 |
|---|----------|------------|
| 01 | [Writing Kernel Modules](01-writing-modules/) | 模块参数、module_init/exit、GPL 符号导出、ktime 时间统计 |
| 02 | [Output-only Misc Driver](02-output-misc-driver/) | Misc 设备框架、file_operations、container_of、copy_from_user、ioctl |
| 03 | [Accessing I/O Registers](03-access-io-registers/) | ioremap、时钟框架、UART 寄存器、波特率公式、probe 自动发送 |
| 04 | [Interrupt Handler & Wait Queue](04-interrupt-handler/) | devm_request_irq、ISR、Ring Buffer、wait_event_interruptible、spinlock |
| 05 | [I2C Driver & Input Subsystem](05-i2c-mpu6500/) | i2c_driver、i2c_transfer burst read、input_polled_dev、EV_ABS |
| 06 | [Describing Hardware (DTS)](06-describing-hardware/) | &label 节点覆盖、GPIO LED、interrupt-parent、IRQ_TYPE_EDGE_RISING |
| 07 | [Pin Multiplexing (Pinctrl)](07-pin-muxing/) | 两级 pinctrl 结构、开漏 pad 配置、/delete-property/、IIO Sysfs |
| 08 | [DMA Engine (SDMA)](08-dma/) | dmaengine API、dma_map_resource、Completion 同步、EPROBE_DEFER、PIO 回退 |

---

## 目录结构

```
linux-kernel-driver-labs/
├── 01-writing-modules/
│   ├── code/          # 驱动源码 + Makefile
│   ├── docs/          # 实验文档
│   └── assets/        # 电路图/截图
├── 02-output-misc-driver/
├── 03-access-io-registers/
├── 04-interrupt-handler/
├── 05-i2c-mpu6500/
├── 06-describing-hardware/
├── 07-pin-muxing/
├── 08-dma/
├── Bootlin-实验总结.md   # 实验总结报告
├── LICENSE            # GPL-2.0
└── .gitignore
```

---

## 构建方法

```bash
# 安装交叉编译工具链（Ubuntu/Debian）
sudo apt install gcc-arm-linux-gnueabihf

# 设置内核源码路径（根据实际路径修改）
export HOME=/home/your_user
# 或在每个实验的 code/ 目录直接执行：
make

# 产物为 *.ko 内核模块和 *.dtb 设备树（实验 03/06/07）
```

---

## 实验依赖关系

```
01-writing-modules          （基础：模块框架）
       ↓
02-output-misc-driver       （进阶：字符设备 + ioctl）
       ↓
03-access-io-registers     （进阶：内存映射 I/O + 时钟）
       ↓
04-interrupt-handler        （进阶：中断 + Ring Buffer + Wait Queue）
       ↓
08-dma                      （进阶：DMA 引擎替换 TX）
       ↑
       │
05-i2c-mpu6500  ←→  06-describing-hardware  ←→  07-pin-muxing
  (I2C 驱动)        (设备树基础)               (引脚复用)
```

---

## 硬件连接

| 资源 | 说明 |
|------|------|
| UART4 | 实验 02/03/04/08 的串口设备节点 `/dev/serial-21f0000` |
| I2C1 (CSI pins) | 实验 05/06/07 的 MPU6500 加速度计总线 |
| GPIO2_00 | MPU6500 中断信号（实验 05-07） |
| CSI pins (UART6 conflict) | 实验 07 将 I2C1 复用至此 |

---

## License

All kernel modules are licensed under **GPL-2.0**. See [LICENSE](LICENSE) for details.
