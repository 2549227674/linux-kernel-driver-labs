// SPDX-License-Identifier: GPL-2.0
/*
 * custom_uart_dma.c - UART Driver for i.MX6ULL with SDMA Support
 *
 * Demonstrates:
 * - DMA engine API (dma_request_chan, dmaengine_slave_config,
 *                   dmaengine_prep_slave_single, dma_async_issue_pending)
 * - Streaming DMA mapping (dma_map_single, dma_unmap_single)
 * - Completion synchronization (init_completion, wait_for_completion)
 * - Graceful fallback: DMA init failure falls back to PIO mode
 * - EPROBE_DEFER handling for deferred DMA channel acquisition
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
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>

#define UCR1_TXDMAEN (1 << 3)
#define TX_DMA_BURST 16

#define UCR1_RRDYEN  (1 << 9)
#define SERIAL_BUFSIZE 16

/* i.MX6ULL UART register offsets */
#define UCR3        0x88
#define UCR3_RXDMUXSEL (1 << 2)
#define URXD        0x00
#define UTXD        0x40
#define UCR1        0x80
#define UCR2        0x84
#define UCR2_IRTS   (1 << 14)
#define UCR2_SRST   (1 << 0)
#define UCR2_RXEN   (1 << 1)
#define UCR2_TXEN   (1 << 2)
#define UCR2_WS     (1 << 5)
#define USR2        0x98
#define USR2_TXFE   (1 << 14)
#define UFCR        0x90
#define UBIR        0xA4
#define UBMR        0xA8

struct my_uart_dev {
    void __iomem *regs;
    struct clk *clk_ipg;
    struct clk *clk_per;
    struct miscdevice miscdev;
    int tx_count;
    int irq;
    char rx_buf[SERIAL_BUFSIZE];
    unsigned int buf_rd;
    unsigned int buf_wr;
    wait_queue_head_t wait;
    struct device *dev;

    /* DMA fields */
    char *tx_buf;
    struct dma_chan *tx_dma_chan;
    dma_addr_t fifo_dma_addr;
    struct completion tx_done;
    bool tx_ongoing;
    spinlock_t tx_lock;
};

static void my_uart_putc(struct my_uart_dev *dev, char c)
{
    int timeout = 1000000;
    while (!(readl(dev->regs + USR2) & USR2_TXFE) && (timeout > 0)) {
        cpu_relax();
        timeout--;
    }
    if (timeout > 0)
        writel(c, dev->regs + UTXD);
    else
        pr_err("my_uart: TX FIFO timeout!\n");
}

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
        if (c == '\n')
            my_uart_putc(dev, '\r');
        my_uart_putc(dev, c);
        dev->tx_count++;
    }

    kfree(kbuf);
    return count;
}

static irqreturn_t my_uart_isr(int irq, void *dev_id)
{
    struct my_uart_dev *dev = dev_id;
    char c;

    c = readl(dev->regs + URXD) & 0xFF;
    pr_info("my_uart_isr: Received '%c' (0x%02x)\n", c, c);

    dev->rx_buf[dev->buf_wr] = c;
    dev->buf_wr = (dev->buf_wr + 1) % SERIAL_BUFSIZE;
    wake_up_interruptible(&dev->wait);
    return IRQ_HANDLED;
}

static ssize_t my_uart_read(struct file *file, char __user *buf,
                              size_t count, loff_t *ppos)
{
    struct miscdevice *mdev = file->private_data;
    struct my_uart_dev *dev = container_of(mdev, struct my_uart_dev, miscdev);
    char c;
    int ret;

    if (count == 0) return 0;

    ret = wait_event_interruptible(dev->wait, dev->buf_rd != dev->buf_wr);
    if (ret) return ret;

    c = dev->rx_buf[dev->buf_rd];
    dev->buf_rd = (dev->buf_rd + 1) % SERIAL_BUFSIZE;

    if (put_user(c, buf)) return -EFAULT;
    return 1;
}

static int my_uart_init_dma(struct platform_device *pdev, struct my_uart_dev *dev)
{
    struct dma_slave_config txconf = { };
    struct resource *res;
    int ret;

    dev->tx_buf = devm_kzalloc(&pdev->dev, SERIAL_BUFSIZE, GFP_KERNEL);
    if (!dev->tx_buf) return -ENOMEM;

    init_completion(&dev->tx_done);
    spin_lock_init(&dev->tx_lock);

    /* Request TX DMA channel from NXP SDMA */
    dev->tx_dma_chan = dma_request_chan(&pdev->dev, "tx");
    if (IS_ERR(dev->tx_dma_chan)) {
        ret = PTR_ERR(dev->tx_dma_chan);
        if (ret == -EPROBE_DEFER)
            return ret;  /* SDMA not ready yet, retry later */
        dev_warn(&pdev->dev, "TX DMA channel unavailable, using PIO.\n");
        return -ENODEV;
    }

    res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
    dev->fifo_dma_addr = dma_map_resource(&pdev->dev, res->start + UTXD,
                                           1, DMA_TO_DEVICE, 0);
    if (dma_mapping_error(&pdev->dev, dev->fifo_dma_addr)) {
        dma_release_channel(dev->tx_dma_chan);
        return -EFAULT;
    }

    txconf.direction        = DMA_MEM_TO_DEV;
    txconf.dst_addr         = dev->fifo_dma_addr;
    txconf.dst_addr_width   = DMA_SLAVE_BUSWIDTH_1_BYTE;
    txconf.dst_maxburst     = TX_DMA_BURST;

    ret = dmaengine_slave_config(dev->tx_dma_chan, &txconf);
    if (ret) {
        dev_err(&pdev->dev, "Failed to config SDMA channel\n");
        dma_unmap_resource(&pdev->dev, dev->fifo_dma_addr, 1, DMA_TO_DEVICE, 0);
        dma_release_channel(dev->tx_dma_chan);
        return ret;
    }

    dev_info(&pdev->dev, "NXP SDMA TX channel configured successfully!\n");
    return 0;
}

static void my_uart_tx_dma_callback(void *data)
{
    struct my_uart_dev *dev = data;
    uint32_t reg_val;

    /* Disable TX DMA before signaling completion */
    reg_val = readl(dev->regs + UCR1);
    reg_val &= ~UCR1_TXDMAEN;
    writel(reg_val, dev->regs + UCR1);

    complete(&dev->tx_done);
}

static ssize_t my_uart_write_dma(struct file *file, const char __user *buf,
                                  size_t count, loff_t *ppos)
{
    struct miscdevice *mdev = file->private_data;
    struct my_uart_dev *dev = container_of(mdev, struct my_uart_dev, miscdev);
    struct dma_async_tx_descriptor *desc;
    dma_addr_t dma_addr;
    unsigned long flags;
    size_t len;
    uint32_t reg_val;

    if (count == 0) return 0;
    len = min_t(size_t, count, SERIAL_BUFSIZE);

    spin_lock_irqsave(&dev->tx_lock, flags);
    if (dev->tx_ongoing) {
        spin_unlock_irqrestore(&dev->tx_lock, flags);
        return -EBUSY;
    }
    dev->tx_ongoing = true;
    spin_unlock_irqrestore(&dev->tx_lock, flags);

    if (copy_from_user(dev->tx_buf, buf, len))
        goto err_out;

    dma_addr = dma_map_single(dev->dev, dev->tx_buf, len, DMA_TO_DEVICE);
    if (dma_mapping_error(dev->dev, dma_addr))
        goto err_out;

    desc = dmaengine_prep_slave_single(dev->tx_dma_chan, dma_addr, len,
                                        DMA_MEM_TO_DEV,
                                        DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
    if (!desc) {
        dma_unmap_single(dev->dev, dma_addr, len, DMA_TO_DEVICE);
        goto err_out;
    }

    reinit_completion(&dev->tx_done);
    desc->callback       = my_uart_tx_dma_callback;
    desc->callback_param = dev;

    dmaengine_submit(desc);
    dma_async_issue_pending(dev->tx_dma_chan);

    reg_val = readl(dev->regs + UCR1);
    reg_val |= UCR1_TXDMAEN;
    writel(reg_val, dev->regs + UCR1);

    wait_for_completion(&dev->tx_done);

    dma_unmap_single(dev->dev, dma_addr, len, DMA_TO_DEVICE);

    spin_lock_irqsave(&dev->tx_lock, flags);
    dev->tx_ongoing = false;
    spin_unlock_irqrestore(&dev->tx_lock, flags);

    dev->tx_count += len;
    return len;

err_out:
    spin_lock_irqsave(&dev->tx_lock, flags);
    dev->tx_ongoing = false;
    spin_unlock_irqrestore(&dev->tx_lock, flags);
    return -EFAULT;
}

static long my_uart_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    struct miscdevice *mdev = file->private_data;
    struct my_uart_dev *dev = container_of(mdev, struct my_uart_dev, miscdev);

    switch (cmd) {
    case 0:  /* SERIAL_RESET_COUNTER */
        dev->tx_count = 0;
        break;
    case 1:  /* SERIAL_GET_COUNTER */
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
    .read           = my_uart_read,
    .write          = my_uart_write,
    .unlocked_ioctl = my_uart_ioctl,
};

static const struct file_operations my_uart_fops_dma = {
    .owner          = THIS_MODULE,
    .read           = my_uart_read,
    .write          = my_uart_write_dma,
    .unlocked_ioctl = my_uart_ioctl,
};

static int my_uart_probe(struct platform_device *pdev)
{
    struct my_uart_dev *my_dev;
    struct resource *res;
    uint32_t reg_val;
    unsigned long per_rate;
    char *dev_name;
    int ret;

    dev_info(&pdev->dev, "Custom UART driver probing (DMA mode)...\n");

    my_dev = devm_kzalloc(&pdev->dev, sizeof(*my_dev), GFP_KERNEL);
    if (!my_dev) return -ENOMEM;

    res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
    my_dev->regs = devm_ioremap_resource(&pdev->dev, res);
    if (IS_ERR(my_dev->regs)) return PTR_ERR(my_dev->regs);

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

    reg_val = readl(my_dev->regs + UCR1);
    reg_val |= 0x01;
    writel(reg_val, my_dev->regs + UCR1);

    reg_val = readl(my_dev->regs + UCR2);
    reg_val |= (UCR2_SRST | UCR2_TXEN | UCR2_WS | UCR2_IRTS | UCR2_RXEN);
    writel(reg_val, my_dev->regs + UCR2);

    reg_val = readl(my_dev->regs + UCR3);
    reg_val |= UCR3_RXDMUXSEL;
    writel(reg_val, my_dev->regs + UCR3);

    per_rate = clk_get_rate(my_dev->clk_per);
    reg_val = readl(my_dev->regs + UFCR);
    reg_val &= ~(7 << 7);
    reg_val |= (5 << 7);
    writel(reg_val, my_dev->regs + UFCR);
    writel(15, my_dev->regs + UBIR);
    writel((per_rate / 115200) - 1, my_dev->regs + UBMR);

    init_waitqueue_head(&my_dev->wait);
    my_dev->buf_rd = 0;
    my_dev->buf_wr = 0;
    my_dev->tx_count = 0;

    my_dev->irq = platform_get_irq(pdev, 0);
    if (my_dev->irq < 0) return my_dev->irq;

    ret = devm_request_irq(&pdev->dev, my_dev->irq, my_uart_isr, 0,
                           dev_name(&pdev->dev), my_dev);
    if (ret) return ret;

    reg_val = readl(my_dev->regs + UCR1);
    reg_val |= UCR1_RRDYEN;
    writel(reg_val, my_dev->regs + UCR1);

    dev_name = devm_kasprintf(&pdev->dev, GFP_KERNEL, "serial-%x", res->start);
    if (!dev_name) return -ENOMEM;

    my_dev->miscdev.minor  = MISC_DYNAMIC_MINOR;
    my_dev->miscdev.name   = dev_name;
    my_dev->miscdev.parent = &pdev->dev;
    my_dev->dev = &pdev->dev;

    /* Try DMA init; fall back to PIO if DMA channel unavailable */
    ret = my_uart_init_dma(pdev, my_dev);
    if (ret == -EPROBE_DEFER)
        return ret;
    if (ret == 0)
        my_dev->miscdev.fops = &my_uart_fops_dma;  /* DMA mode */
    else
        my_dev->miscdev.fops = &my_uart_fops;      /* PIO fallback */

    ret = misc_register(&my_dev->miscdev);
    if (ret) return ret;

    platform_set_drvdata(pdev, my_dev);
    dev_info(&pdev->dev, "UART Misc Driver registered as /dev/%s\n", dev_name);
    return 0;
}

static int my_uart_remove(struct platform_device *pdev)
{
    struct my_uart_dev *my_dev = platform_get_drvdata(pdev);

    misc_deregister(&my_dev->miscdev);

    if (my_dev->tx_dma_chan) {
        dmaengine_terminate_sync(my_dev->tx_dma_chan);
        dma_unmap_resource(my_dev->dev, my_dev->fifo_dma_addr,
                           1, DMA_TO_DEVICE, 0);
        dma_release_channel(my_dev->tx_dma_chan);
    }

    clk_disable_unprepare(my_dev->clk_per);
    clk_disable_unprepare(my_dev->clk_ipg);
    return 0;
}

static const struct of_device_id my_uart_ids[] = {
    { .compatible = "my,custom-uart4" },
    { /* sentinel */ }
};

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
MODULE_DESCRIPTION("i.MX6ULL UART driver with NXP SDMA support");
