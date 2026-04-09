/*
 * test_ioctl.c - Userspace test program for custom_uart driver
 *
 * Tests:
 * - Opening the misc device
 * - Writing data (triggers TX)
 * - IOCTL: get/reset TX character counter
 *
 * Compile: arm-linux-gnueabihf-gcc test_ioctl.c -o test_ioctl -static
 * Usage:  ./test_ioctl /dev/serial-21f0000
 */

#include <stdio.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#define SERIAL_RESET_COUNTER 0
#define SERIAL_GET_COUNTER   1

int main(int argc, char **argv)
{
    int fd, count;
    char *msg = "test";

    if (argc < 2) {
        printf("Usage: %s <device_path>\n", argv[0]);
        return -1;
    }

    fd = open(argv[1], O_RDWR);
    if (fd < 0) {
        perror("Open device failed");
        return -1;
    }

    ioctl(fd, SERIAL_GET_COUNTER, &count);
    printf("Initial count: %d\n", count);

    printf("Writing '%s' to device...\n", msg);
    write(fd, msg, strlen(msg));

    ioctl(fd, SERIAL_GET_COUNTER, &count);
    printf("After writing, count: %d\n", count);

    printf("Resetting counter...\n");
    ioctl(fd, SERIAL_RESET_COUNTER);

    ioctl(fd, SERIAL_GET_COUNTER, &count);
    printf("Final count after reset: %d\n", count);

    close(fd);
    return 0;
}
