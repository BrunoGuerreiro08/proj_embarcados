/*
 * Copyright (c) 2012-2014 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

int main(void)
{
	printf("Hello World! %s\n", CONFIG_BOARD_TARGET);
    while (1) {
        printk("Tick\n");
        k_sleep(K_SECONDS(1));
    }
	return 0;
}
