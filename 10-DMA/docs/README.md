# DMA

## 实验目标

在实验 4 的 UART 驱动基础上，将 TX 轮询发送替换为 NXP SDMA 引擎 DMA 发送，实现零拷贝、高效率的 UART 数据传输，同时保留 ISR + Ring Buffer + Wait Queue 的 RX 路径。

## 知识点

- DMA 引擎 API：`dma_request_chan` / `dmaengine_slave_config` / `dmaengine_prep_slave_single` / `dma_async_issue_pending`
- 物理地址获取：`dma_map_resource`（UART TX FIFO 寄存器物理地址）
- 流式 DMA 映射：`dma_map_single` / `dma_unmap_single`
- Completion 同步：`init_completion` / `wait_for_completion` / `complete`
- `EPROBE_DEFER`：SDMA 控制器未就绪时延迟探测
- `dmaengine_terminate_sync` / `dma_release_channel`：DMA 通道释放
- PIO 回退机制：DMA 通道不可用时自动降级为轮询发送
- `tx_ongoing` 标志 + 自旋锁：防止 DMA 和 PIO 并发写入

## 代码说明

| 文件 | 说明 |
|------|------|
| `code/custom_uart_dma.c` | 完整驱动（含 SDMA TX + ISR RX + PIO 回退） |
| `code/Makefile` | Out-of-tree 构建脚本 |

## 数据流

```
TX (DMA 模式):
  my_uart_write_dma --> copy_from_user --> dma_map_single
  --> dmaengine_prep_slave_single --> dma_async_issue_pending
  --> UCR1_TXDMAEN 启用 --> DMA 硬件自主传输 --> TX 完成中断
  --> my_uart_tx_dma_callback --> complete(&tx_done)
  --> wait_for_completion 返回

TX (PIO 回退):
  my_uart_write --> 轮询 USR2_TXFE --> 逐字节写入 UTXD

RX:
  UART 硬件 ISR --> rx_buf[buf_wr] --> my_uart_read
  --> wait_event_interruptible 阻塞 --> wake_up_interruptible 唤醒
```

## DMA 配置详解

```c
struct dma_slave_config txconf = { };
txconf.direction      = DMA_MEM_TO_DEV;       /* 内存→设备 */
txconf.dst_addr       = fifo_dma_addr;        /* UART TX FIFO 物理地址 */
txconf.dst_addr_width = DMA_SLAVE_BUSWIDTH_1_BYTE;
txconf.dst_maxburst   = 16;                   /* 每次 burst 16 字节 */
```

## 自旋锁使用位置

| 区域 | 保护对象 |
|------|----------|
| `my_uart_write_dma` | `tx_ongoing` 标志（防止 PIO/DMA 并发） |
| `my_uart_isr` | `rx_buf`, `buf_wr` |

## 验证

```bash
# 编译
make

# 加载驱动
adb shell insmod /root/custom_uart_dma.ko
adb shell dmesg | grep -E "SDMA|UART"

# 预期输出之一（DMA 模式）：
# NXP SDMA TX channel configured successfully!
# UART Misc Driver registered as /dev/serial-...

# 预期输出之一（PIO 回退）：
# TX DMA channel unavailable, using PIO.

# 测试 RX（后台监听）
adb shell "cat /dev/serial-21f0000 &"

# 测试 TX
adb shell echo "DMA test" > /dev/serial-21f0000

# 检查中断计数
adb shell cat /proc/interrupts | grep serial
```

## 关键设计

- `dma_map_resource`：将 UART TX FIFO 的物理地址（res->start + UTXD）映射为 DMA 可访问的地址
- `EPROBE_DEFER`：SDMA 控制器在内核中可能比 UART 设备后探测，返回此错误使内核稍后重新探测
- `tx_ongoing` + `spin_lock_irqsave`：防止应用层同时调用 DMA 和 PIO write 导致竞争
- `reinit_completion`：每次 DMA 传输前重置 completion 状态，确保 wait_for_completion 正确等待
- `UCR1_TXDMAEN`：写入后再等待 completion，避免 TX 完成中断在设置回调之前到达
