# Locking

## 实验目标

理解内核并发保护的核心机制：自旋锁（Spinlock）在中断上下文与进程上下文之间的使用规则，以及持锁期间禁止休眠的原则。

## 知识点

- Spinlock vs Mutex：适用场景与休眠行为差异
- `spin_lock` / `spin_unlock`：用于 ISR 上下文
- `spin_lock_irqsave` / `spin_unlock_irqrestore`：用于进程上下文
- 中断上下文**绝对不能休眠**：不能调用 `copy_to_user`、`kmalloc(GFP_KERNEL)` 等
- 三明治原则：持锁前完成所有内存分配和数据拷贝

## 代码结构图解

### Spinlock vs Mutex 对比

```mermaid
graph TD
    A["锁获取失败"] --> B{"选择什么锁?"}
    B -->|Spinlock| C["原地自旋<br>忙等待"]
    B -->|Mutex| D["进入睡眠<br>调度到其他进程"]
    C --> E["适用：中断上下文<br>不能休眠"]
    D --> F["适用：进程上下文<br>可以休眠"]
```

| 维度 | Spinlock | Mutex |
|------|----------|-------|
| 持锁时休眠 | **不允许** | 允许 |
| 适用上下文 | 中断 + 进程 | 仅进程 |
| 获取失败行为 | 自旋（忙等） | 睡眠（调度） |

### ISR vs 进程上下文用锁差异

```mermaid
graph TD
    A["持锁上下文"] --> B{"是中断上下文吗?"}
    B -->|是| C["spin_lock<br>无需保存中断标志"]
    B -->|否| D["spin_lock_irqsave<br>保存中断标志到 flags"]
    C --> E["访问共享数据"]
    D --> E
    E --> F["spin_unlock / spin_unlock_irqrestore"]
```

### 为什么进程必须用 irqsave

```mermaid
graph TD
    A["进程 A 获取锁 spin_lock"] --> B["中断触发 → ISR 抢占 A"]
    B --> C["ISR 尝试获取同一把锁"]
    C --> D{"ISR 用 spin_lock"}
    D -->|"ISR 不能休眠"| E["原地自旋等待"]
    E --> F["进程 A 被 ISR 抢占，无法释放锁"]
    F --> G["死锁！"]
    H["解决: spin_lock_irqsave"] --> I["持锁时禁止本地 CPU 中断"]
    I --> J["ISR 不会抢占，不会死锁"]
```

### 三明治原则（原子性保护）

```mermaid
graph LR
    A["第一步：分配内存<br>GFP_KERNEL 可能休眠"] --> B["第二步：拷贝数据<br>copy_from_user 可能休眠"]
    B --> C["第三步：持锁访问<br>spin_lock / unlock"]
    C --> D["第四步：拷贝结果<br>put_user 可能休眠"]
    D --> E["第五步：释放内存<br>kfree"]
    style C fill:#f96
```

### 持锁期间禁止的 API

```mermaid
graph TD
    A["持锁期间（原子上下文）"] --> B["kmalloc GFP_KERNEL<br>可能休眠"]
    A --> C["copy_to/from_user<br>可能休眠"]
    A --> D["mutex_lock<br>必然休眠"]
    A --> E["wait_event_interruptible<br>必然休眠"]
    A --> F["msleep<br>必然休眠"]
    B --> G["CONFIG_DEBUG_ATOMIC_SLEEP<br>触发内核 BUG"]
    C --> G
    D --> G
    E --> G
    F --> G
```

## 代码说明

| 文件 | 说明 |
|------|------|
| `code/custom_uart.c` | 完整驱动源码（含自旋锁、ISR + Ring Buffer + Wait Queue） |
| `code/Makefile` | Out-of-tree 构建脚本 |

## 自旋锁使用位置

| 区域 | 保护对象 | 锁类型 |
|------|----------|--------|
| `my_uart_isr` | `rx_buf`, `buf_wr` | `spin_lock` |
| `my_uart_read` | `rx_buf`, `buf_rd` | `spin_lock_irqsave` |
| `my_uart_write` | `tx_count` | `spin_lock_irqsave` |
| `my_uart_ioctl` | `tx_count` | `spin_lock_irqsave` |

## 关键原则

| 原则 | 说明 |
|------|------|
| 持锁期间不分配内存 | `GFP_KERNEL` 分配可能休眠，违反原子性 |
| 持锁期间不拷贝数据 | `copy_to_user` / `copy_from_user` 可能休眠 |
| 中断上下文用 `spin_lock` | 中断已禁止，无需再保存中断状态 |
| 进程上下文用 `spin_lock_irqsave` | 防止进程被中断打断后，中断又去竞争同一把锁（死锁） |
