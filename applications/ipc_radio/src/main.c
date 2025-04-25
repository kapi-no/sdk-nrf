/*
 * Copyright (c) 2023 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <errno.h>
#include <zephyr/logging/log.h>

#include "ipc_bt.h"

LOG_MODULE_REGISTER(ipc_radio, CONFIG_IPC_RADIO_LOG_LEVEL);

#if !(CONFIG_IPC_RADIO_802154 || CONFIG_IPC_RADIO_BT)
#error "No radio serialization selected."
#endif

#include <zephyr/init.h>

#define GPIO0_BASE     0x418C0500UL
#define GPIO0_DIRSET   (*(volatile uint32_t *)(GPIO0_BASE + 0x018))
#define GPIO0_OUTSET   (*(volatile uint32_t *)(GPIO0_BASE + 0x008))
#define GPIO0_OUTCLR   (*(volatile uint32_t *)(GPIO0_BASE + 0x00C))

#define PIN_LED2       29

static void delay_ms_200(void) {
	volatile int count = 2500000;  // Adjust if needed
	while (count--);
}

static int led_init(void)
{
	GPIO0_DIRSET = (1 << PIN_LED2);

	GPIO0_OUTSET = (1 << PIN_LED2);
	delay_ms_200();
	GPIO0_OUTCLR = (1 << PIN_LED2);
	delay_ms_200();
	GPIO0_OUTSET = (1 << PIN_LED2);

	return 0;
}

SYS_INIT(led_init, EARLY, 0);

int main(void)
{
	int err;

	err = ipc_bt_init();
	if ((err) && (err != -ENOSYS)) {
		LOG_ERR("Error initializing ipc radio %d", err);
		return err;
	}

	for (;;) {
		err = ipc_bt_process();

		if (err == -ENOSYS) {
			/* Particular implementation does not need the process function */
			return 0;
		} else if (err) {
			LOG_ERR("Error processing ipc radio %d", err);
			return err;
		}
	}
}
