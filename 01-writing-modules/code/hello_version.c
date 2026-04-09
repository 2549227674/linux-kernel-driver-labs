// SPDX-License-Identifier: GPL-2.0
/*
 * hello_version.c - Hello World Kernel Module
 *
 * A simple kernel module demonstrating:
 * - Module init/exit lifecycle
 * - Module parameters (sysfs)
 * - Kernel time tracking (ktime)
 * - Kernel version detection (utsname)
 *
 * Author: Han
 * Course: Bootlin Embedded Linux Kernel/Driver Development
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/utsname.h>     /* for kernel version detection */
#include <linux/moduleparam.h> /* for module parameters */
#include <linux/ktime.h>       /* for kernel time tracking */

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Han");
MODULE_DESCRIPTION("A hello world module with params and time tracking (Bootlin Lab)");

/* =========================================
 * Module Parameters
 * ========================================= */
static char *whom = "world";
static int howmany = 1;
static time64_t load_time; /* time64_t for Y2038 safety */

module_param(whom, charp, 0644);
MODULE_PARM_DESC(whom, "Recipient of the hello message");

module_param(howmany, int, 0644);
MODULE_PARM_DESC(howmany, "Number of greetings");

/* =========================================
 * Init Function (called on module load)
 * ========================================= */
static int __init hello_version_init(void)
{
    int i;

    load_time = ktime_get_seconds();

    pr_info("================================\n");
    pr_info("Hello Module Loaded! Kernel Version: %s\n", utsname()->release);

    for (i = 0; i < howmany; i++) {
        pr_info("(%d/%d) Hello, %s!\n", i + 1, howmany, whom);
    }
    pr_info("================================\n");

    return 0;
}

/* =========================================
 * Exit Function (called on module unload)
 * ========================================= */
static void __exit hello_version_exit(void)
{
    time64_t unload_time;
    time64_t elapsed_seconds;

    unload_time = ktime_get_seconds();
    elapsed_seconds = unload_time - load_time;

    pr_info("Goodbye, %s! The module is unloaded.\n", whom);
    pr_info("The module lived for %lld seconds.\n", (long long)elapsed_seconds);
}

module_init(hello_version_init);
module_exit(hello_version_exit);
