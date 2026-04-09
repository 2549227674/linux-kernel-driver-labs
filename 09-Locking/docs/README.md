# Locking

## 实验目标

理解内核并发保护的核心机制：自旋锁（Spinlock）在中断上下文与进程上下文之间的使用规则，以及持锁期间禁止休眠的原则。

## 知识点

- Spinlock vs Mutex：适用场景与休眠行为差异
- `spin_lock` / `spin_unlock`：用于 ISR 上下文（中断已禁止抢占）
- `spin_lock_irqsave` / `spin_unlock_irqrestore`：用于进程上下文（保存/恢复中断状态）
- 中断上下文**绝对不能休眠**：不能调用 `copy_to_user`、`kmalloc(GFP_KERNEL)` 等
- 三明治原则：持锁前完成所有内存分配和数据拷贝
- `CONFIG_DEBUG_ATOMIC_SLEEP`：持锁期间休眠会触发内核 BUG

## 自旋锁与 Mutex 对比

| 维度 | Spinlock | Mutex |
|------|----------|-------|
| 持有锁时休眠 | **不允许** | 允许 |
| 适用场景 | **中断上下文** | 进程上下文 |
| 获取失败行为 | 自旋（忙等） | 睡眠（调度） |
| 死锁风险 | 低（配合 irqsave） | 中 |

## 正确加锁模式

**ISR 中：使用 `spin_lock`（中断上下文本身已禁止抢占）**
```c
static irqreturn_t my_uart_isr(int irq, void *dev_id) {
    spin_lock(&dev->lock);
    // 访问共享 Buffer 和寄存器
    spin_unlock(&dev->lock);
    return IRQ_HANDLED;
}
```

**进程上下文中：使用 `spin_lock_irqsave`（禁止本地 CPU 中断，防止死锁）**
```c
ssize_t my_uart_read(struct file *file, char __user *buf, ...) {
    unsigned long flags;
    spin_lock_irqsave(&dev->lock, flags);
    // 从 Buffer 取数据到局部变量
    spin_unlock_irqrestore(&dev->lock, flags);
    // copy_to_user 必须放在锁外面！
    copy_to_user(buf, &tmp, 1);
    return 1;
}
```

## 真实实现参考

| 保护对象 | 实验 | 文件 |
|---------|------|------|
| ISR 与 read/write 之间的 rx_buf / buf_wr / buf_rd | 08 | `custom_uart.c` |
| DMA 与 PIO write 之间的 tx_ongoing 标志 | 10 | `custom_uart_dma.c` |
| ioctl 中的 tx_count | 08 | `custom_uart.c` |

## 关键原则

- **持锁期间不分配内存**：`GFP_KERNEL` 分配可能休眠，违反原子性
- **持锁期间不拷贝数据**：`copy_to_user` / `copy_from_user` 可能休眠
- **中断上下文用 `spin_lock`（非 irqsave）**：中断已禁止，无需再保存中断状态
- **进程上下文用 `spin_lock_irqsave`**：防止该进程被中断打断后，中断又去竞争同一把锁（死锁）
