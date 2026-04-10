# Accessing I/O Memory and Ports

## 实验目标

通过直接操作 i.MX6ULL UART 寄存器，实现内存映射 I/O、时钟配置、波特率计算，以及 TX FIFO 超时保护轮询发送。

## 知识点

- `ioremap` / `devm_ioremap_resource` 物理地址映射
- `clk_get_rate` / `clk_prepare_enable` 时钟框架
- i.MX6ULL UART 寄存器映射（UCR1/UCR2/UFCR/UBIR/UBMR/USR2）
- 波特率公式：`BaudRate = RefFreq / (16 × (UBMR+1)/(UBIR+1))`
- `cpu_relax` + 超时保护：避免 TX FIFO 死锁

## 代码结构图解

### 内存映射 I/O 流程

```mermaid
graph TD
    A["设备树 &uart4<br>资源地址 0x021F0000"] --> B["platform_get_resource<br>获取 IORESOURCE_MEM"]
    B --> C["devm_ioremap_resource<br>request_mem_region + ioremap"]
    C --> D["虚拟地址 my_dev->regs"]
    D --> E["readl / writel<br>寄存器访问"]
    E --> F["URXD / UTXD / UCR1<br>UFCR / USR2 / UBIR"]
```

### 时钟框架初始化序列

```mermaid
graph LR
    A["devm_clk_get ipg"] --> B["devm_clk_get per"]
    B --> C["clk_prepare_enable<br>接口时钟"]
    C --> D["clk_prepare_enable<br>外设时钟"]
    D --> E["clk_get_rate per<br>获取波特率时钟"]
    E --> F["UBMR = rate/115200 - 1<br>配置波特率"]
```

### 波特率计算公式

```mermaid
graph TD
    A["公式: BaudRate = RefFreq / (16 × (UBMR+1)/(UBIR+1))"] --> B["UBIR = 15<br>(16 与公式中抵消)"]
    A --> C["UFCR[9:7] = 101b<br>(div-by-1, 不分频)"]
    A --> D["UBMR = RefFreq / 115200 - 1<br>直接由时钟频率计算"]
    B --> E["简化公式:<br>BaudRate = RefFreq / (UBMR+1)"]
```

### TX FIFO 超时保护轮询

```mermaid
graph TD
    A["for i in 0..99 循环"] --> B["timeout = 1000000"]
    B --> C["while !(USR2 & TXFE)<br>且 timeout > 0"]
    C --> D["cpu_relax<br>忙等待"]
    D --> E["timeout--"]
    E --> F{"timeout > 0?"}
    F -->|是| G["writel 写 UTXD"]
    F -->|否| H["TX FIFO 挂起, break<br>防止内核永久挂起"]
```

## 代码说明

| 文件 | 说明 |
|------|------|
| `code/custom_uart.c` | 驱动源码（probe 中发送 100 字符） |
| `code/Makefile` | Out-of-tree 构建脚本 |
| `code/imx6ull-100ask-custom.dts` | 设备树片段 |

## 波特率配置

```
UBIR = 15 (与公式中 16 抵消)
UBMR = (per_rate / 115200) - 1
UFCR[9:7] = 101b (div-by-1, 不分频)
```

## 验证

```bash
adb push custom_uart.ko /root/
adb shell insmod /root/custom_uart.ko
adb shell dmesg | tail
# 预期：看到 UCR1 initial value 和 "Starting transmission loop"
```

## 关键设计

| 设计点 | 说明 |
|--------|------|
| `devm_ioremap_resource` | 合并 request_mem_region + ioremap，失败返回错误码，无需手动 unmap |
| `cpu_relax()` | 提示编译器这是忙等待，可优化寄存器读取 |
| 超时保护 | 无超时保护时，TX FIFO 硬件卡死将导致内核永久挂起 |
| `compatible = "my,custom-uart"` | 劫持设备树节点，阻止内核默认驱动绑定 |
