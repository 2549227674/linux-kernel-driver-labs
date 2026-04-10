// SPDX-License-Identifier: GPL-2.0
/*
 * custom_uart.c - UART Driver with Spinlock Protection
 *
 * Demonstrates:
 * - Spinlock protection for ISR + process context shared resources
 * - spin_lock in ISR context
 * - spin_lock_irqsave in process context (read/write/ioctl)
 * - Sandwich principle: memory allocation and copy_to/from_user outside lock
 * - CONFIG_DEBUG_ATOMIC_SLEEP: catches sleeping inside atomic context
 *
 * This is the standalone complete version for experiment 09 (Locking).
 * It is equivalent to the spinlock-enhanced version of the code
 * used in experiments 07 and 08.
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

/* IOCTL command definitions */
#define SERIAL_RESET_COUNTER _IO('s', 0)
#define SERIAL_GET_COUNTER   _IOR('s', 1, int)

/* Ring buffer size */
#define SERIAL_BUFSIZE 32

/* i.MX6ULL UART register offsets */
#define URXD  0x00  /* Receive register */
#define UTXD  0x40  /* Transmit register */
#define UCR1  0x80  /* Control register 1 */
#define UCR2  0x84  /* Control register 2 */
#define UCR3  0x88  /* Control register 3 */
#define UFCR  0x90  /* FIFO control register */
#define USR1  0x94  /* Status register 1 */
#define USR2  0x98  /* Status register 2 */
#define UBIR  0xA4  /* Baud rate integer register */
#define UBMR  0xA8  /* Baud rate modulator register */

/* Key register bit definitions */
#define UCR1_RRDYEN    (1 << 9)   /* Receive ready interrupt enable */
#define UCR1_UARTEN    (1 << 0)   /* UART enable */

#define UCR2_SRST      (1 << 0)   /* Software reset */
#define UCR2_RXEN      (1 << 1)   /* Receive enable */
#define UCR2_TXEN      (1 << 2)   /* Transmit enable */
#define UCR2_WS        (1 << 5)   /* 8-bit word length */
#define UCR2_IRTS      (1 << 14)  /* Ignore RTS pin */

#define UCR3_RXDMUXSEL (1 << 2)   /* Must set: route external RX pin to UART core */

#define USR2_TXFE      (1 << 14)  /* Transmit FIFO empty flag */

/* Device private data structure */
struct my_uart_dev {
    void __iomem *regs;
    struct clk *clk_ipg;
    struct clk *clk_per;
    struct miscdevice miscdev;

    /* Concurrency protection core */
    spinlock_t lock;              /* Protects buffer and registers */

    /* Shared resources */
    int tx_count;                 /* Transmit character counter */
    char rx_buf[SERIAL_BUFSIZE];  /* Ring receive buffer */
    unsigned int buf_rd;          /* Read pointer */
    unsigned int buf_wr;          /* Write pointer */

    int irq;                      /* IRQ number */
    wait_queue_head_t wait;       /* Wait queue head for blocking read */
};

/* Low-level hardware helper: send one character via polling */
static void my_uart_putc(struct my_uart_dev *dev, char c)
{
    int timeout = 1000000;

    /* Wait for TX FIFO to become empty */
    while (!(readl(dev->regs + USR2) & USR2_TXFE) && (timeout > 0)) {
        cpu_relax();
        timeout--;
    }

    if (timeout > 0) {
        writel(c, dev->regs + UTXD);
    } else {
        pr_err("my_uart: TX FIFO timeout!\n");
    }
}

/* ============================================
 * write callback: process context
 * Uses spin_lock_irqsave to protect tx_count
 * ============================================ */
static ssize_t my_uart_write(struct file *file, const char __user *buf,
                              size_t count, loff_t *ppos)
{
    struct miscdevice *mdev = file->private_data;
    struct my_uart_dev *dev = container_of(mdev, struct my_uart_dev, miscdev);
    char *kbuf;
    int i;
    unsigned long flags;

    if (count == 0) return 0;

    /* [LOCK OUTSIDE] Allocate kernel buffer - GFP_KERNEL may sleep */
    kbuf = kzalloc(count, GFP_KERNEL);
    if (!kbuf) return -ENOMEM;

    /* [LOCK OUTSIDE] Copy data from user space - may trigger page fault and sleep */
    if (copy_from_user(kbuf, buf, count)) {
        kfree(kbuf);
        return -EFAULT;
    }

    /* --- Enter critical section --- */
    spin_lock_irqsave(&dev->lock, flags);

    for (i = 0; i < count; i++) {
        char c = kbuf[i];
        if (c == '\n')
            my_uart_putc(dev, '\r');
        my_uart_putc(dev, c);
        dev->tx_count++;
    }

    spin_unlock_irqrestore(&dev->lock, flags);
    /* --- Exit critical section --- */

    kfree(kbuf);
    return count;
}

/* ============================================
 * read callback: blocking read via wait queue
 * ============================================ */
static ssize_t my_uart_read(struct file *file, char __user *buf,
                              size_t count, loff_t *ppos)
{
    struct miscdevice *mdev = file->private_data;
    struct my_uart_dev *dev = container_of(mdev, struct my_uart_dev, miscdev);
    char c;
    int ret;
    unsigned long flags;

    if (count == 0) return 0;

    /* [LOCK OUTSIDE] Wait queue sleep - this function itself is meant to sleep */
    ret = wait_event_interruptible(dev->wait, dev->buf_rd != dev->buf_wr);
    if (ret) return ret;

    /* --- Enter critical section --- */
    spin_lock_irqsave(&dev->lock, flags);

    c = dev->rx_buf[dev->buf_rd];
    dev->buf_rd = (dev->buf_rd + 1) % SERIAL_BUFSIZE;

    spin_unlock_irqrestore(&dev->lock, flags);
    /* --- Exit critical section --- */

    /* [LOCK OUTSIDE] put_user may cause page fault and sleep */
    if (put_user(c, buf)) return -EFAULT;
    return 1;
}

/* ============================================
 * ioctl callback: get/reset TX counter
 * ============================================ */
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

/* File operations structure */
static const struct file_operations my_uart_fops = {
    .owner          = THIS_MODULE,
    .read           = my_uart_read,
    .write          = my_uart_write,
    .unlocked_ioctl = my_uart_ioctl,
};

/* ============================================
 * ISR: hardware fires this on each received character
 * Uses spin_lock (not irqsave) because we're already in interrupt context
 * ============================================ */
static irqreturn_t my_uart_isr(int irq, void *dev_id)
{
    struct my_uart_dev *dev = dev_id;
    unsigned long flags;
    char c;

    /* --- Enter critical section --- */
    /* In ISR: interrupt context is already non-preemptible by same-priority interrupts.
       Use plain spin_lock (not irqsave) since we don't need to save/restore flags. */
    spin_lock_irqsave(&dev->lock, flags);

    /* Read receive register (also clears RRDY interrupt flag on i.MX6ULL) */
    c = readl(dev->regs + URXD) & 0xFF;

    /* Store to ring buffer */
    dev->rx_buf[dev->buf_wr] = c;
    dev->buf_wr = (dev->buf_wr + 1) % SERIAL_BUFSIZE;

    spin_unlock_irqrestore(&dev->lock, flags);
    /* --- Exit critical section --- */

    /* wake_up_interruptible: must be outside spinlock */
    wake_up_interruptible(&dev->wait);
    return IRQ_HANDLED;
}

/* ============================================
 * Probe: device initialization and registration
 * ============================================ */
static int my_uart_probe(struct platform_device *pdev)
{
    struct my_uart_dev *my_dev;
    struct resource *res;
    uint32_t reg_val;
    unsigned long per_rate;
    int ret;

    dev_info(&pdev->dev, "Custom UART driver probing with Locking...\n");

    /* 1. Allocate device structure memory */
    my_dev = devm_kzalloc(&pdev->dev, sizeof(*my_dev), GFP_KERNEL);
    if (!my_dev) return -ENOMEM;

    /* 2. [CORE] Initialize spinlock - must be done before any concurrency can happen */
    spin_lock_init(&my_dev->lock);

    /* 3. Map I/O registers */
    res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
    my_dev->regs = devm_ioremap_resource(&pdev->dev, res);
    if (IS_ERR(my_dev->regs)) return PTR_ERR(my_dev->regs);

    /* 4. Clock management */
    my_dev->clk_ipg = devm_clk_get(&pdev->dev, "ipg");
    my_dev->clk_per = devm_clk_get(&pdev->dev, "per");
    clk_prepare_enable(my_dev->clk_ipg);
    clk_prepare_enable(my_dev->clk_per);

    /* 5. Initialize wait queue and counters */
    init_waitqueue_head(&my_dev->wait);
    my_dev->buf_rd = 0;
    my_dev->buf_wr = 0;
    my_dev->tx_count = 0;

    /* 6. Hardware initialization sequence */
    writel(0, my_dev->regs + UCR1);

    reg_val = readl(my_dev->regs + UCR2);
    reg_val |= (UCR2_SRST | UCR2_TXEN | UCR2_RXEN | UCR2_WS | UCR2_IRTS);
    writel(reg_val, my_dev->regs + UCR2);

    /* Baud rate: 115200 */
    per_rate = clk_get_rate(my_dev->clk_per);
    writel(15, my_dev->regs + UBIR);
    writel((per_rate / 115200) - 1, my_dev->regs + UBMR);

    /* [NXP quirk] Must set RXDMUXSEL to route external RX pin to UART core */
    reg_val = readl(my_dev->regs + UCR3);
    reg_val |= UCR3_RXDMUXSEL;
    writel(reg_val, my_dev->regs + UCR3);

    /* 7. Register IRQ */
    my_dev->irq = platform_get_irq(pdev, 0);
    ret = devm_request_irq(&pdev->dev, my_dev->irq, my_uart_isr, 0,
                           dev_name(&pdev->dev), my_dev);
    if (ret) return ret;

    reg_val = readl(my_dev->regs + UCR1);
    reg_val |= (UCR1_UARTEN | UCR1_RRDYEN);
    writel(reg_val, my_dev->regs + UCR1);

    /* 8. Register misc device */
    my_dev->miscdev.minor = MISC_DYNAMIC_MINOR;
    my_dev->miscdev.name = devm_kasprintf(&pdev->dev, GFP_KERNEL, "serial-%x", res->start);
    my_dev->miscdev.fops = &my_uart_fops;
    my_dev->miscdev.parent = &pdev->dev;

    ret = misc_register(&my_dev->miscdev);
    if (ret) return ret;

    platform_set_drvdata(pdev, my_dev);
    return 0;
}

/* ============================================
 * Remove: cleanup on module unload
 * ============================================ */
static int my_uart_remove(struct platform_device *pdev)
{
    struct my_uart_dev *my_dev = platform_get_drvdata(pdev);

    misc_deregister(&my_dev->miscdev);

    /* Disable interrupts at hardware level */
    writel(0, my_dev->regs + UCR1);

    /* Disable clocks */
    clk_disable_unprepare(my_dev->clk_per);
    clk_disable_unprepare(my_dev->clk_ipg);

    dev_info(&pdev->dev, "Driver removed.\n");
    return 0;
}

static const struct of_device_id my_uart_ids[] = {
    { .compatible = "my,custom-uart4" },
    { /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, my_uart_ids);

static struct platform_driver my_uart_driver = {
    .driver = {
        .name = "my_custom_uart",
        .of_match_table = my_uart_ids,
    },
    .probe  = my_uart_probe,
    .remove = my_uart_remove,
};

module_platform_driver(my_uart_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Han");
MODULE_DESCRIPTION("i.MX6ULL UART driver with spinlock protection");
