# Output-only Misc Driver

## 实验目标

在实验 3 的基础上，注册 Misc 字符设备，提供 `write` 和 `ioctl` 接口，替代 probe 中的硬编码发字符循环。

## 知识点

- Misc 设备驱动框架（`misc_register`）
- `struct file_operations` 与用户/内核空间数据交换
- `container_of` 宏：通过 `file->private_data` 找回私有设备结构体
- `copy_from_user` / `put_user` 跨空间安全拷贝
- `\n` → `\r\n` 转义（串口终端兼容性）
- 动态设备节点命名（`devm_kasprintf`）

## 代码结构图解

### container_of 反向追溯

```mermaid
graph TD
    A["用户 open /dev/serial-*"] --> B["内核 VFS 创建 file 结构体"]
    B --> C["file->private_data<br>指向 miscdevice 成员"]
    C --> D["container_of(file->private_data,<br>struct my_uart_dev, miscdev)"]
    D --> E["获取 my_uart_dev 首地址"]
    E --> F["访问 dev->regs / dev->tx_count<br>等所有成员"]
```

### Write 数据流

```mermaid
sequenceDiagram
    participant User as 用户态 echo "Hello"
    participant K as my_uart_write
    participant KM as kzalloc + copy_from_user
    participant HW as my_uart_putc
    participant UART as UART TX FIFO

    User->>K: write("Hello\n")
    K->>KM: 分配内核缓冲区
    KM->>K: copy_from_user
    K->>HW: 遍历字符: '\n' → '\r' + 字符
    HW->>UART: 轮询 USR2_TXFE 后写 UTXD
    HW-->>K: dev->tx_count++
    K-->>User: 返回写入字节数
```

### IOCTL 接口

```mermaid
graph TD
    A["用户 test_ioctl /dev/serial-*"] --> B["ioctl cmd = 0<br>SERIAL_RESET_COUNTER"]
    A --> C["ioctl cmd = 1<br>SERIAL_GET_COUNTER"]
    B --> D["dev->tx_count = 0<br>重置计数器"]
    C --> E["put_user count<br>写回用户空间"]
```

### Misc 设备注册

```mermaid
graph LR
    A["devm_kasprintf<br>生成 'serial-%x' 设备名"] --> B["miscdev.minor<br>= MISC_DYNAMIC_MINOR"]
    B --> C["miscdev.fops<br>= &my_uart_fops"]
    C --> D["misc_register<br>注册设备节点"]
    D --> E["/dev/serial-21f0000<br>用户态可访问"]
```

## 代码说明

| 文件 | 说明 |
|------|------|
| `code/custom_uart.c` | 驱动源码 |
| `code/Makefile` | Out-of-tree 构建脚本 |
| `code/test_ioctl.c` | 用户态测试程序（交叉编译） |

## 构建

```bash
# 编译驱动
make

# 交叉编译测试程序
arm-linux-gnueabihf-gcc test_ioctl.c -o test_ioctl -static
```

## 验证流程

```bash
# 加载驱动
adb shell insmod custom_uart.ko
adb shell dmesg | tail

# 查看设备节点
adb shell ls -l /dev/serial-*

# 通过 echo 发送数据
adb shell echo "Hello i.MX6ULL" > /dev/serial-21f0000

# 查看引用计数（Used=1 表示有进程持有）
adb shell lsmod | grep custom_uart

# 测试 IOCTL
adb push test_ioctl /root/
adb shell /root/test_ioctl /dev/serial-21f0000
```

## 关键设计

| 设计点 | 说明 |
|--------|------|
| `misc_register` | 自动分配次设备号，主设备号固定为 10 |
| `container_of` | 从 miscdev 指针反向追溯得到 my_uart_dev 首地址 |
| `\n` → `\r\n` | 串口终端需要回车符，打印换行前先发送 `\r` |
| `devm_kasprintf` | 根据物理基地址动态生成设备名，卸载时自动释放 |
