// SPDX-License-Identifier: GPL-2.0
/*
 * custom_uart.c - UART Driver for i.MX6ULL with ISR, Ring Buffer and Spinlock
 *
 * Demonstrates:
 * - Interrupt-driven RX with ring buffer
 * - Blocking read via wait queue (sleep/wakeup)
 * - Spinlock protection for shared resources (ISR + process context)
 * - UCR3_RXDMUXSEL bit (NXP i.MX6ULL hardware quirk)
 *
 * Author: Han
 * Course: Bootlin Embedded Linux Kernel/Driver Development
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/of.h>
#include <linux/delay.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/wait.h>
#include <linux/spinlock.h>

/* i.MX6ULL UART register offsets */
#define URXD  0x00
#define UTXD  0x40
#define UCR1  0x80
#define UCR2  0x84
#define UCR3  0x88
#define UFCR  0x90
#define USR2  0x98
#define UBIR  0xA4
#define UBMR  0xA8

/* Register bit definitions */
#define UCR1_UARTEN  (1 << 0)
#define UCR1_RRDYEN  (1 << 9)
#define UCR2_SRST    (1 << 0)
#define UCR2_RXEN    (1 << 1)
#define UCR2_TXEN    (1 << 2)
#define UCR2_WS      (1 << 5)
#define UCR2_IRTS    (1 << 14)
#define UCR3_RXDMUXSEL (1 << 2)  /* Must set for external RX to work */
#define USR2_TXFE    (1 << 14)

/* Ring buffer size */
#define SERIAL_BUFSIZE 32

/* IOCTL commands */
#define SERIAL_RESET_COUNTER _IO('s', 0)
#define SERIAL_GET_COUNTER   _IOR('s', 1, int)

/* Device private data */
struct my_uart_dev {
    void __iomem *regs;
    struct clk *clk_ipg;
    struct clk *clk_per;
    struct miscdevice miscdev;

    /* Concurrency protection */
    spinlock_t lock;

    /* Shared resources */
    int tx_count;
    char rx_buf[SERIAL_BUFSIZE];
    unsigned int buf_rd;
    unsigned int buf_wr;

    int irq;
    wait_queue_head_t wait;
};

/* Low-level TX: send one char with polling (caller holds lock) */
static void my_uart_putc_locked(struct my_uart_dev *dev, char c)
{
    int timeout = 1000000;
    while (!(readl(dev->regs + USR2) & USR2_TXFE) && (timeout > 0))
        cpu_relax(), timeout--;
    if (timeout > 0)
        writel(c, dev->regs + UTXD);
}

/* write callback: process context, protects tx_count with spinlock */
static ssize_t my_uart_write(struct file *file, const char __user *buf,
                              size_t count, loff_t *ppos)
{
    struct miscdevice *mdev = file->private_data;
    struct my_uart_dev *dev = container_of(mdev, struct my_uart_dev, miscdev);
    char *kbuf;
    int i;
    unsigned long flags;

    if (count == 0) return 0;

    kbuf = kzalloc(count, GFP_KERNEL);
    if (!kbuf) return -ENOMEM;

    if (copy_from_user(kbuf, buf, count)) {
        kfree(kbuf);
        return -EFAULT;
    }

    spin_lock_irqsave(&dev->lock, flags);
    for (i = 0; i < count; i++) {
        char c = kbuf[i];
        if (c == '\n')
            my_uart_putc_locked(dev, '\r');
        my_uart_putc_locked(dev, c);
        dev->tx_count++;
    }
    spin_unlock_irqrestore(&dev->lock, flags);

    kfree(kbuf);
    return count;
}

/* read callback: blocking read via wait queue, ISR fills ring buffer */
static ssize_t my_uart_read(struct file *file, char __user *buf,
                              size_t count, loff_t *ppos)
{
    struct miscdevice *mdev = file->private_data;
    struct my_uart_dev *dev = container_of(mdev, struct my_uart_dev, miscdev);
    char c;
    int ret;
    unsigned long flags;

    if (count == 0) return 0;

    ret = wait_event_interruptible(dev->wait, dev->buf_rd != dev->buf_wr);
    if (ret) return ret;

    spin_lock_irqsave(&dev->lock, flags);
    c = dev->rx_buf[dev->buf_rd];
    dev->buf_rd = (dev->buf_rd + 1) % SERIAL_BUFSIZE;
    spin_unlock_irqrestore(&dev->lock, flags);

    if (put_user(c, buf)) return -EFAULT;
    return 1;
}

/* ioctl callback: get/reset TX counter with spinlock protection */
static long my_uart_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    struct miscdevice *mdev = file->private_data;
    struct my_uart_dev *dev = container_of(mdev, struct my_uart_dev, miscdev);
    unsigned long flags;
    int current_tx;

    switch (cmd) {
    case SERIAL_RESET_COUNTER:
        spin_lock_irqsave(&dev->lock, flags);
        dev->tx_count = 0;
        spin_unlock_irqrestore(&dev->lock, flags);
        break;
    case SERIAL_GET_COUNTER:
        spin_lock_irqsave(&dev->lock, flags);
        current_tx = dev->tx_count;
        spin_unlock_irqrestore(&dev->lock, flags);
        if (put_user(current_tx, (int __user *)arg))
            return -EFAULT;
        break;
    default:
        return -ENOTTY;
    }
    return 0;
}

static const struct file_operations my_uart_fops = {
    .owner          = THIS_MODULE,
    .read           = my_uart_read,
    .write          = my_uart_write,
    .unlocked_ioctl = my_uart_ioctl,
};

/* ISR: hardware fires this on each received character */
static irqreturn_t my_uart_isr(int irq, void *dev_id)
{
    struct my_uart_dev *dev = dev_id;
    unsigned long flags;
    char c;

    c = readl(dev->regs + URXD) & 0xFF;

    spin_lock_irqsave(&dev->lock, flags);
    dev->rx_buf[dev->buf_wr] = c;
    dev->buf_wr = (dev->buf_wr + 1) % SERIAL_BUFSIZE;
    spin_unlock_irqrestore(&dev->lock, flags);

    wake_up_interruptible(&dev->wait);
    return IRQ_HANDLED;
}

static int my_uart_probe(struct platform_device *pdev)
{
    struct my_uart_dev *my_dev;
    struct resource *res;
    uint32_t reg_val;
    unsigned long per_rate;
    int ret;

    dev_info(&pdev->dev, "Custom UART driver probing (ISR + Spinlock)...\n");

    my_dev = devm_kzalloc(&pdev->dev, sizeof(*my_dev), GFP_KERNEL);
    if (!my_dev) return -ENOMEM;

    spin_lock_init(&my_dev->lock);

    res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
    my_dev->regs = devm_ioremap_resource(&pdev->dev, res);
    if (IS_ERR(my_dev->regs)) return PTR_ERR(my_dev->regs);

    my_dev->clk_ipg = devm_clk_get(&pdev->dev, "ipg");
    my_dev->clk_per = devm_clk_get(&pdev->dev, "per");
    clk_prepare_enable(my_dev->clk_ipg);
    clk_prepare_enable(my_dev->clk_per);

    init_waitqueue_head(&my_dev->wait);
    my_dev->buf_rd = 0;
    my_dev->buf_wr = 0;
    my_dev->tx_count = 0;

    /* Hardware init */
    writel(0, my_dev->regs + UCR1);

    reg_val = readl(my_dev->regs + UCR2);
    reg_val |= (UCR2_SRST | UCR2_TXEN | UCR2_RXEN | UCR2_WS | UCR2_IRTS);
    writel(reg_val, my_dev->regs + UCR2);

    per_rate = clk_get_rate(my_dev->clk_per);
    writel(15, my_dev->regs + UBIR);
    writel((per_rate / 115200) - 1, my_dev->regs + UBMR);

    /* NXP quirk: must set RXDMUXSEL to route external pin to RX core */
    reg_val = readl(my_dev->regs + UCR3);
    reg_val |= UCR3_RXDMUXSEL;
    writel(reg_val, my_dev->regs + UCR3);

    /* Register IRQ */
    my_dev->irq = platform_get_irq(pdev, 0);
    ret = devm_request_irq(&pdev->dev, my_dev->irq, my_uart_isr, 0,
                           dev_name(&pdev->dev), my_dev);
    if (ret) return ret;

    reg_val = readl(my_dev->regs + UCR1);
    reg_val |= (UCR1_UARTEN | UCR1_RRDYEN);
    writel(reg_val, my_dev->regs + UCR1);

    my_dev->miscdev.minor = MISC_DYNAMIC_MINOR;
    my_dev->miscdev.name  = devm_kasprintf(&pdev->dev, GFP_KERNEL, "serial-%x", res->start);
    my_dev->miscdev.fops  = &my_uart_fops;
    my_dev->miscdev.parent = &pdev->dev;

    ret = misc_register(&my_dev->miscdev);
    if (ret) return ret;

    platform_set_drvdata(pdev, my_dev);
    return 0;
}

static int my_uart_remove(struct platform_device *pdev)
{
    struct my_uart_dev *my_dev = platform_get_drvdata(pdev);

    misc_deregister(&my_dev->miscdev);
    writel(0, my_dev->regs + UCR1);
    clk_disable_unprepare(my_dev->clk_per);
    clk_disable_unprepare(my_dev->clk_ipg);
    return 0;
}

static const struct of_device_id my_uart_ids[] = {
    { .compatible = "my,custom-uart4" },
    { /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, my_uart_ids);

static struct platform_driver my_uart_driver = {
    .driver = {
        .name           = "my_custom_uart",
        .of_match_table = my_uart_ids,
    },
    .probe  = my_uart_probe,
    .remove = my_uart_remove,
};

module_platform_driver(my_uart_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Han");
MODULE_DESCRIPTION("i.MX6ULL UART driver with ISR, ring buffer and spinlock");
