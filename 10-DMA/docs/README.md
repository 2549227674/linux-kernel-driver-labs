# DMA

## 实验目标

将 UART TX 轮询发送替换为 NXP SDMA 引擎 DMA 发送，实现零拷贝、高效率的 UART 数据传输，同时保留 ISR + Ring Buffer + Wait Queue 的 RX 路径。

## 知识点

- DMA 引擎 API：`dma_request_chan` / `dmaengine_slave_config` / `dmaengine_prep_slave_single` / `dma_async_issue_pending`
- 物理地址获取：`dma_map_resource`（UART TX FIFO 寄存器物理地址）
- 流式 DMA 映射：`dma_map_single` / `dma_unmap_single`
- Completion 同步：`init_completion` / `wait_for_completion` / `complete`
- `EPROBE_DEFER`：SDMA 控制器未就绪时延迟探测
- PIO 回退机制：DMA 通道不可用时自动降级为轮询发送

## 代码结构图解

### DMA vs PIO 数据流对比

```mermaid
graph TB
    subgraph "PIO 轮询模式"
        P1["copy_from_user"] --> P2["for 循环逐字节"]
        P2 --> P3["读 USR2_TXFE"]
        P3 --> P4["写 UTXD"]
        P4 --> P5["CPU 逐字节干预"]
    end
    subgraph "DMA 模式"
        D1["copy_from_user"] --> D2["dma_map_single<br>Cache Clean"]
        D2 --> D3["dmaengine_submit + issue"]
        D3 --> D4["SDMA 硬件突发搬运 16 字节"]
        D4 --> D5["TX 完成中断<br>回调唤醒进程"]
    end
```

### DMA 引擎 API 完整链路

```mermaid
sequenceDiagram
    participant App as 应用层 write()
    participant DMA as DMA Engine API
    participant SDMA as i.MX6ULL SDMA
    participant UART as UART TX FIFO
    participant CB as DMA 回调

    App->>DMA: 1. dma_request_chan("tx")
    Note over DMA: 向 SDMA 控制器申请通道
    App->>DMA: 2. dmaengine_slave_config(txconf)
    Note over DMA: 方向/地址/burst 配置
    App->>DMA: 3. dma_map_single(buf)
    Note over DMA: Cache Clean，返回总线地址
    App->>DMA: 4. prep_slave_single + submit
    App->>DMA: 5. issue_pending（启动传输）
    App->>DMA: 6. 置位 UCR1_TXDMAEN
    App->>DMA: 7. wait_for_completion（休眠）
    SDMA->>UART: Burst 突发搬运数据...
    SDMA-->>CB: TX 完成中断
    CB-->>DMA: complete()（唤醒进程）
    App->>DMA: 8. dma_unmap_single
```

### Cache 一致性问题

```mermaid
graph TD
    A["copy_from_user 数据"] --> B["写入 CPU L1/L2 Cache"]
    B --> C{"未写回 DDR"}
    C -->|是| D["SDMA 从 DDR 读数据"]
    D --> E["读到旧数据 → 乱码"]
    C -->|否| F["SDMA 读到正确数据"]
    G["解决: dma_map_single"] --> H["强制 Cache Clean 刷入 DDR"]
    H --> F
```

### tx_ongoing 防竞争

```mermaid
graph TD
    A["write_dma 进程"] --> B["spin_lock_irqsave"]
    B --> C{"tx_ongoing == true?"}
    C -->|是| D["spin_unlock<br>return -EBUSY"]
    C -->|否| E["tx_ongoing = true"]
    E --> F["DMA 传输"]
    F --> G["完成中断"]
    G --> H["tx_ongoing = false"]
    H --> I["spin_unlock_irqrestore"]
```

### PIO Fallback 鲁棒性

```mermaid
graph TD
    A["my_uart_init_dma"] --> B{"返回错误?"}
    B -->|EPROBE_DEFER| C["return ret<br>内核稍后重试 probe"]
    B -->|-ENODEV| D["dev_warn<br>初始化 PIO 模式"]
    B -->|0 成功| E["miscdev.fops = DMA 版本"]
    D --> F["miscdev.fops = PIO 版本"]
```

### TXTL 水位线与 dst_maxburst 关系

| TXTL | FIFO 空位 | 触发时机 | 推荐 dst_maxburst |
|------|-----------|----------|------------------|
| 2 | 30 | 快空时才申请 | ≤ 30 |
| **16** | **16** | **空一半时申请** | **≤ 16（最优）** |
| 31 | 1 | 有空间即申请 | 只能 1 |

## 代码说明

| 文件 | 说明 |
|------|------|
| `code/custom_uart_dma.c` | 完整驱动（含 SDMA TX + ISR RX + PIO 回退） |
| `code/Makefile` | Out-of-tree 构建脚本 |

## 验证

```bash
make
adb shell insmod /root/custom_uart_dma.ko
adb shell dmesg | grep -E "SDMA|UART"
# 预期之一: NXP SDMA TX channel configured successfully!
# 预期之一: TX DMA channel unavailable, using PIO.

adb shell "cat /dev/serial-21f0000 &"
adb shell echo "DMA test" > /dev/serial-21f0000
adb shell cat /proc/interrupts | grep serial
```

## 关键设计

| 设计点 | 说明 |
|--------|------|
| `dma_map_resource` | UART TX FIFO 物理地址 → DMA 可访问的总线地址 |
| `EPROBE_DEFER` | SDMA 未就绪时返回此错误，内核稍后重新探测 |
| `tx_ongoing + spin_lock` | 防止应用层同时调用 DMA 和 PIO write 竞争 |
| `reinit_completion` | 每次 DMA 前重置 completion，确保 wait_for_completion 正确等待 |
| `UCR1_TXDMAEN = (1 << 3)` | 写入后再等 completion，避免 TX 中断在回调前到达 |
| `TXTL=16 + burst=16` | SDMA 和 UART FIFO 完美匹配，避免饥饿或溢出 |
