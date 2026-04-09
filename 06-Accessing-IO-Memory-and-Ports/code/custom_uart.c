// SPDX-License-Identifier: GPL-2.0
/*
 * custom_uart.c - Custom UART Driver for i.MX6ULL
 *
 * Demonstrates:
 * - Platform driver with memory-mapped I/O
 * - Clock framework and baud rate configuration
 * - TX loop with timeout protection (prevents kernel hang)
 *
 * This version sends 100 characters on probe() to verify hardware.
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

/* i.MX6ULL UART register offsets */
#define URXD  0x00
#define UTXD  0x40
#define UCR1  0x80
#define UCR2  0x84
#define UFCR  0x90
#define USR2  0x98
#define UBIR  0xA4
#define UBMR  0xA8

/* Key register bits */
#define UCR1_UARTEN  (1 << 0)
#define UCR2_SRST    (1 << 0)
#define UCR2_RXEN    (1 << 1)
#define UCR2_TXEN    (1 << 2)
#define UCR2_WS      (1 << 5)
#define UCR2_IRTS    (1 << 14)
#define USR2_TXFE    (1 << 14)

struct my_uart_dev {
    void __iomem *regs;
    struct clk *clk_ipg;
    struct clk *clk_per;
};

static int my_uart_probe(struct platform_device *pdev)
{
    struct my_uart_dev *my_dev;
    struct resource *res;
    uint32_t reg_val;
    int ret;
    int i, timeout;
    unsigned long per_rate;

    dev_info(&pdev->dev, "Custom UART driver probing...\n");

    my_dev = devm_kzalloc(&pdev->dev, sizeof(*my_dev), GFP_KERNEL);
    if (!my_dev) return -ENOMEM;

    res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
    my_dev->regs = devm_ioremap_resource(&pdev->dev, res);
    if (IS_ERR(my_dev->regs)) return PTR_ERR(my_dev->regs);

    my_dev->clk_ipg = devm_clk_get(&pdev->dev, "ipg");
    my_dev->clk_per = devm_clk_get(&pdev->dev, "per");
    if (IS_ERR(my_dev->clk_ipg) || IS_ERR(my_dev->clk_per)) {
        dev_err(&pdev->dev, "Failed to get clocks\n");
        return -ENODEV;
    }

    ret = clk_prepare_enable(my_dev->clk_ipg);
    if (ret) return ret;
    ret = clk_prepare_enable(my_dev->clk_per);
    if (ret) {
        clk_disable_unprepare(my_dev->clk_ipg);
        return ret;
    }
    dev_info(&pdev->dev, "Clocks enabled successfully!\n");

    /* Hardware init: UART enable */
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
    reg_val |= (5 << 7);
    writel(reg_val, my_dev->regs + UFCR);
    writel(15, my_dev->regs + UBIR);
    writel((per_rate / 115200) - 1, my_dev->regs + UBMR);

    /* Debug TX: send 100 characters with timeout protection */
    dev_info(&pdev->dev, "Starting transmission loop...\n");
    for (i = 0; i < 100; i++) {
        timeout = 1000000;
        while (!(readl(my_dev->regs + USR2) & USR2_TXFE) && (timeout > 0)) {
            cpu_relax();
            timeout--;
        }
        if (timeout <= 0) {
            dev_err(&pdev->dev, "TX FIFO hang at iteration %d!\n", i);
            break;
        }
        writel('H', my_dev->regs + UTXD);
        msleep(100);
    }

    platform_set_drvdata(pdev, my_dev);
    dev_info(&pdev->dev, "Probe finished!\n");
    return 0;
}

static int my_uart_remove(struct platform_device *pdev)
{
    struct my_uart_dev *my_dev = platform_get_drvdata(pdev);
    clk_disable_unprepare(my_dev->clk_per);
    clk_disable_unprepare(my_dev->clk_ipg);
    dev_info(&pdev->dev, "Custom UART driver removed.\n");
    return 0;
}

static const struct of_device_id my_uart_ids[] = {
    { .compatible = "my,custom-uart" },
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
MODULE_DESCRIPTION("Custom i.MX6ULL UART driver for training");
