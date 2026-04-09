# Interrupt Handler and Sleep / Wait Queue

## 实验目标

在实验2的基础上，添加中断驱动的接收（ISR + Ring Buffer + Wait Queue）和自旋锁保护（多核/中断上下文并发保护）。

## 知识点

- 硬件中断注册：`devm_request_irq`，`irqreturn_t`
- 环形缓冲区（Ring Buffer）：生产者（ISR）/消费者（read）模型
- `wait_event_interruptible`：阻塞读取，ISR 通过 `wake_up_interruptible` 唤醒
- 自旋锁：`spin_lock_irqsave` / `spin_unlock_irqrestore`
  - 保护 ISR 和进程上下文共享的 `tx_count`
  - 保护环形缓冲区的 `buf_wr`
- NXP i.MX6ULL 硬件特性：`UCR3_RXDMUXSEL` 位必须置 1
- 中断休眠的三明治原则（锁外分配内存/拷贝数据）

## 代码说明

| 文件 | 说明 |
|------|------|
| `code/custom_uart.c` | 完整驱动（含 ISR、Wait Queue、Spinlock） |
| `code/Makefile` | Out-of-tree 构建脚本 |

## 数据流

```
接收: UART硬件ISR --写--> rx_buf[buf_wr] --读--> my_uart_read(用户)
发送: my_uart_write(用户) --> 轮询TX --> UCR3_RXDMUXSEL必须置1
```

## 自旋锁使用位置

| 区域 | 保护对象 | 锁类型 |
|------|----------|--------|
| `my_uart_isr` | `rx_buf`, `buf_wr` | `spin_lock` |
| `my_uart_read` | `rx_buf`, `buf_rd` | `spin_lock_irqsave` |
| `my_uart_write` | `tx_count` | `spin_lock_irqsave` |
| `my_uart_ioctl` | `tx_count` | `spin_lock_irqsave` |

## 验证

```bash
# 加载驱动
adb shell insmod /root/custom_uart.ko

# 后台监听（进程会休眠在 wait queue）
adb shell "cat /dev/serial-21f0000 &"

# 从另一个终端发送数据（触发 ISR，唤醒等待进程）
adb shell echo "Test" > /dev/serial-21f0000

# 检查中断计数
adb shell cat /proc/interrupts | grep serial
```
