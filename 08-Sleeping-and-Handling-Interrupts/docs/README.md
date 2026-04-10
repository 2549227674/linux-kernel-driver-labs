# Sleeping and Handling Interrupts

## 实验目标

添加中断驱动的接收（ISR + Ring Buffer + Wait Queue）和自旋锁保护，实现阻塞读，使串口可被 `cat` 命令实时监听。

## 知识点

- 硬件中断注册：`devm_request_irq`，`irqreturn_t`
- 环形缓冲区（Ring Buffer）：生产者（ISR）/消费者（read）模型
- `wait_event_interruptible`：阻塞读取，ISR 通过 `wake_up_interruptible` 唤醒
- 自旋锁：`spin_lock_irqsave` / `spin_unlock_irqrestore`
- NXP i.MX6ULL 硬件特性：`UCR3_RXDMUXSEL` 位必须置 1

## 代码结构图解

### 生产者/消费者模型

```mermaid
graph TB
    subgraph "ISR（生产者，中断上下文）"
        ISR1["读 URXD 寄存器"]
        ISR2["写 rx_buf[buf_wr]"]
        ISR3["buf_wr = (buf_wr+1) % SIZE"]
        ISR4["wake_up_interruptible"]
    end
    subgraph "read（消费者，进程上下文）"
        READ1["wait_event_interruptible<br>缓冲区空则休眠"]
        READ2["读 rx_buf[buf_rd]"]
        READ3["buf_rd = (buf_rd+1) % SIZE"]
        READ4["put_user 返回用户"]
    end
    ISR2 -->|共享 rx_buf| READ2
    ISR4 -->|唤醒等待队列| READ1
```

### 环形缓冲区原理

```mermaid
graph LR
    A["SERIAL_BUFSIZE = 32"] --> B["写指针 buf_wr<br>ISR 更新"]
    A --> C["读指针 buf_rd<br>read 更新"]
    B --> D{"空条件:<br>buf_rd == buf_wr"}
    C --> E{"满条件:<br>(buf_wr+1) % SIZE == buf_rd"}
```

### 自旋锁使用位置

```mermaid
graph TD
    A["ISR: my_uart_isr"] --> B["spin_lock<br>中断已禁止调度"]
    B --> C["保护 rx_buf, buf_wr"]
    D["read: my_uart_read"] --> E["spin_lock_irqsave<br>保存中断标志"]
    E --> F["保护 rx_buf, buf_rd"]
    G["write/ioctl"] --> H["spin_lock_irqsave"]
    H --> I["保护 tx_count"]
```

### 三明治原则

```mermaid
graph LR
    A["第一步：分配内存<br>GFP_KERNEL"] --> B["第二步：拷贝数据<br>copy_from_user"]
    B --> C["第三步：持锁访问<br>spin_lock / unlock"]
    C --> D["第四步：拷贝结果<br>put_user"]
    D --> E["第五步：释放内存<br>kfree"]
    style C fill:#f96
```

### UCR3_RXDMUXSEL 硬件 quirk

```mermaid
graph TD
    A["UCR3 寄存器 bit 2"] --> B{"RXDMUXSEL = ?"}
    B -->|0| C["RX 信号来自内部测试<br>始终读回 0x00"]
    B -->|1| D["RX 信号路由至外部<br>正常接收数据"]
```

## 代码说明

| 文件 | 说明 |
|------|------|
| `code/custom_uart.c` | 完整驱动（含 ISR、Wait Queue、Spinlock） |
| `code/Makefile` | Out-of-tree 构建脚本 |

## 验证

```bash
adb shell insmod /root/custom_uart.ko
adb shell "cat /dev/serial-21f0000 &"
adb shell echo "Test" > /dev/serial-21f0000
adb shell cat /proc/interrupts | grep serial
```

## 关键设计

| 设计点 | 说明 |
|--------|------|
| `spin_lock` in ISR | 中断已禁止调度，无需 irqsave |
| `spin_lock_irqsave` in read | 保存/恢复中断标志，防止死锁 |
| `wait_event_interruptible` | 缓冲区空则休眠，ISR 唤醒后继续 |
| `UCR3_RXDMUXSEL = 1` | 不置 1 外部 RX 信号进不来，始终读 0x00 |
| 三明治原则 | `copy_to/from_user` 和 `GFP_KERNEL` 必须在锁外 |
