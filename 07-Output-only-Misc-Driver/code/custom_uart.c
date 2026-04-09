// SPDX-License-Identifier: GPL-2.0
/*
 * custom_uart.c - Custom UART Driver for i.MX6ULL
 *
 * Demonstrates:
 * - Platform driver model (probe/remove)
 * - Memory-mapped I/O (ioremap, readl/writel)
 * - Clock framework (clk_prepare_enable, clk_get_rate)
 * - Misc device registration
 * - IOCTL for device control
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

/* i.MX6ULL UART register offsets */
#define URXD   0x00  /* Receive register */
#define UTXD  0x40  /* Transmit register */
#define UCR1  0x80  /* Control register 1 */
#define UCR2  0x84  /* Control register 2 */
#define UFCR  0x90  /* FIFO control register */
#define USR2  0x98  /* Status register 2 */
#define UBIR  0xA4  /* Baud rate integer register */
#define UBMR  0xA8  /* Baud rate modulator register */

/* Register bit definitions */
#define UCR1_UARTEN  (1 << 0)   /* UART enable */
#define UCR2_SRST    (1 << 0)   /* Software reset */
#define UCR2_RXEN    (1 << 1)   /* Receive enable */
#define UCR2_TXEN    (1 << 2)   /* Transmit enable */
#define UCR2_WS      (1 << 5)   /* 8-bit word length */
#define UCR2_IRTS    (1 << 14)  /* Ignore RTS pin */
#define USR2_TXFE    (1 << 14)  /* TX FIFO empty flag */

/* IOCTL command numbers */
#define SERIAL_RESET_COUNTER 0
#define SERIAL_GET_COUNTER   1

/* Device private data structure */
struct my_uart_dev {
    void __iomem *regs;        /* Virtual IO base address */
    struct clk *clk_ipg;        /* Interface clock */
    struct clk *clk_per;        /* Peripheral clock (baud rate generator) */
    struct miscdevice miscdev;  /* Embedded misc device */
    int tx_count;               /* Transmit character counter */
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

/* write callback: copy data from user, send via UART */
static ssize_t my_uart_write(struct file *file, const char __user *buf,
                              size_t count, loff_t *ppos)
{
    struct miscdevice *mdev = file->private_data;
    struct my_uart_dev *dev = container_of(mdev, struct my_uart_dev, miscdev);
    char *kbuf;
    int i;

    if (count == 0) return 0;

    kbuf = kzalloc(count, GFP_KERNEL);
    if (!kbuf) return -ENOMEM;

    if (copy_from_user(kbuf, buf, count)) {
        kfree(kbuf);
        return -EFAULT;
    }

    for (i = 0; i < count; i++) {
        char c = kbuf[i];
        /* Auto CR for LF */
        if (c == '\n')
            my_uart_putc(dev, '\r');
        my_uart_putc(dev, c);
        dev->tx_count++;
    }

    kfree(kbuf);
    return count;
}

/* ioctl callback: reset or query TX counter */
static long my_uart_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    struct miscdevice *mdev = file->private_data;
    struct my_uart_dev *dev = container_of(mdev, struct my_uart_dev, miscdev);

    switch (cmd) {
    case SERIAL_RESET_COUNTER:
        dev->tx_count = 0;
        break;
    case SERIAL_GET_COUNTER:
        if (put_user(dev->tx_count, (int __user *)arg))
            return -EFAULT;
        break;
    default:
        return -ENOTTY;
    }
    return 0;
}

static const struct file_operations my_uart_fops = {
    .owner          = THIS_MODULE,
    .write          = my_uart_write,
    .unlocked_ioctl = my_uart_ioctl,
};

/* Platform driver: probe */
static int my_uart_probe(struct platform_device *pdev)
{
    struct my_uart_dev *my_dev;
    struct resource *res;
    char *dev_name;
    uint32_t reg_val;
    unsigned long per_rate;
    int ret;

    dev_info(&pdev->dev, "Custom UART driver probing...\n");

    my_dev = devm_kzalloc(&pdev->dev, sizeof(*my_dev), GFP_KERNEL);
    if (!my_dev) return -ENOMEM;

    /* Map IO memory */
    res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
    my_dev->regs = devm_ioremap_resource(&pdev->dev, res);
    if (IS_ERR(my_dev->regs)) return PTR_ERR(my_dev->regs);

    /* Get and enable clocks */
    my_dev->clk_ipg = devm_clk_get(&pdev->dev, "ipg");
    my_dev->clk_per = devm_clk_get(&pdev->dev, "per");
    if (IS_ERR(my_dev->clk_ipg) || IS_ERR(my_dev->clk_per))
        return -ENODEV;

    ret = clk_prepare_enable(my_dev->clk_ipg);
    if (ret) return ret;
    ret = clk_prepare_enable(my_dev->clk_per);
    if (ret) {
        clk_disable_unprepare(my_dev->clk_ipg);
        return ret;
    }

    /* Hardware init: UART enable, software reset */
    reg_val = readl(my_dev->regs + UCR1);
    reg_val |= UCR1_UARTEN;
    writel(reg_val, my_dev->regs + UCR1);

    reg_val = readl(my_dev->regs + UCR2);
    reg_val |= (UCR2_SRST | UCR2_TXEN | UCR2_WS | UCR2_IRTS);
    writel(reg_val, my_dev->regs + UCR2);

    /* Baud rate: 115200 */
    per_rate = clk_get_rate(my_dev->clk_per);
    reg_val = readl(my_dev->regs + UFCR);
    reg_val &= ~(7 << 7);
    reg_val |= (5 << 7);   /* div by 1 */
    writel(reg_val, my_dev->regs + UFCR);
    writel(15, my_dev->regs + UBIR);
    writel((per_rate / 115200) - 1, my_dev->regs + UBMR);

    /* Register misc device */
    my_dev->tx_count = 0;
    dev_name = devm_kasprintf(&pdev->dev, GFP_KERNEL, "serial-%x", res->start);

    my_dev->miscdev.minor = MISC_DYNAMIC_MINOR;
    my_dev->miscdev.name  = dev_name;
    my_dev->miscdev.fops  = &my_uart_fops;
    my_dev->miscdev.parent = &pdev->dev;

    ret = misc_register(&my_dev->miscdev);
    if (ret) goto err_clk;

    platform_set_drvdata(pdev, my_dev);
    dev_info(&pdev->dev, "UART Misc Driver registered as /dev/%s\n", dev_name);
    return 0;

err_clk:
    clk_disable_unprepare(my_dev->clk_per);
    clk_disable_unprepare(my_dev->clk_ipg);
    return ret;
}

/* Platform driver: remove */
static int my_uart_remove(struct platform_device *pdev)
{
    struct my_uart_dev *my_dev = platform_get_drvdata(pdev);

    misc_deregister(&my_dev->miscdev);
    clk_disable_unprepare(my_dev->clk_per);
    clk_disable_unprepare(my_dev->clk_ipg);
    dev_info(&pdev->dev, "Custom UART driver removed.\n");
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
MODULE_DESCRIPTION("Custom i.MX6ULL UART driver (Bootlin Lab)");
