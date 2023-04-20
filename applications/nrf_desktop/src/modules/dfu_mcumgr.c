/*
 * Copyright (c) 2023 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/dfu/mcuboot.h>
#include <zephyr/mgmt/mcumgr/mgmt/callbacks.h>

#include <app_event_manager.h>
#include "dfu_lock.h"

#define MODULE dfu_mcumgr
#include <caf/events/module_state_event.h>
#include <caf/events/ble_smp_event.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(MODULE, CONFIG_DESKTOP_DFU_MCUMGR_LOG_LEVEL);

#define DFU_TIMEOUT	K_SECONDS(5)

static struct k_work_delayable dfu_timeout;

static atomic_t mcumgr_event_active = ATOMIC_INIT(false);

/* Declare the function here as it is not available in the MCUmgr API. */
void img_mgmt_reset_upload(void);

static void dfu_lock_owner_changed(const struct dfu_lock_owner *new_owner)
{
	LOG_DBG("MCUmgr progress reset due to different DFU process operations");

	img_mgmt_reset_upload();
}

const static struct dfu_lock_owner mcumgr_owner = {
	.name = "MCUmgr",
	.owner_changed = dfu_lock_owner_changed,
};

static void dfu_timeout_handler(struct k_work *work)
{
	LOG_WRN("MCUmgr DFU timed out");

	dfu_lock_release(&mcumgr_owner);
}

static int32_t mcumgr_img_mgmt_cb(uint32_t event,
				  int32_t rc,
				  bool *abort_more,
				  void *data,
				  size_t data_size)
{
	LOG_INF("MCUmgr Image Management Event with the %d ID",
		__builtin_ctz(MGMT_EVT_GET_ID(event)));

	if (IS_ENABLED(CONFIG_DESKTOP_DFU_LOCK)) {
		if (!dfu_lock_owner_check_and_try(&mcumgr_owner)) {
			return MGMT_ERR_EACCESSDENIED;
		}
		k_work_reschedule(&dfu_timeout, DFU_TIMEOUT);
	}

	if (atomic_cas(&mcumgr_event_active, false, true)) {
		APP_EVENT_SUBMIT(new_ble_smp_transfer_event());
	}

	return MGMT_ERR_EOK;
}

static struct mgmt_callback img_mgmt_callback = {
	.callback = mcumgr_img_mgmt_cb,
	.event_id = MGMT_EVT_OP_IMG_MGMT_ALL,
};

static int32_t mcumgr_os_mgmt_reset_cb(uint32_t event,
				       int32_t rc,
				       bool *abort_more,
				       void *data,
				       size_t data_size)
{
	LOG_INF("MCUmgr OS Management Reset Event");

	if (IS_ENABLED(CONFIG_DESKTOP_DFU_LOCK)) {
		if (!dfu_lock_owner_check_and_try(&mcumgr_owner)) {
			return MGMT_ERR_EACCESSDENIED;
		}
		k_work_reschedule(&dfu_timeout, DFU_TIMEOUT);
	}

	return MGMT_ERR_EOK;
}

static struct mgmt_callback os_mgmt_reset_callback = {
	.callback = mcumgr_os_mgmt_reset_cb,
	.event_id = MGMT_EVT_OP_OS_MGMT_RESET,
};

static bool app_event_handler(const struct app_event_header *aeh)
{
	if (is_ble_smp_transfer_event(aeh)) {
		bool res = atomic_cas(&mcumgr_event_active, true, false);

		__ASSERT_NO_MSG(res);
		ARG_UNUSED(res);

		return false;
	}

	if (is_module_state_event(aeh)) {
		const struct module_state_event *event =
			cast_module_state_event(aeh);

		if (check_state(event, MODULE_ID(main), MODULE_STATE_READY)) {
#if CONFIG_BOOTLOADER_MCUBOOT
			int err = boot_write_img_confirmed();

			if (err) {
				LOG_ERR("Cannot confirm a running image");
			}
#endif
			k_work_init_delayable(&dfu_timeout, dfu_timeout_handler);

			mgmt_callback_register(&img_mgmt_callback);
			mgmt_callback_register(&os_mgmt_reset_callback);
		}
		return false;
	}

	/* If event is unhandled, unsubscribe. */
	__ASSERT_NO_MSG(false);

	return false;
}

APP_EVENT_LISTENER(MODULE, app_event_handler);
APP_EVENT_SUBSCRIBE(MODULE, module_state_event);
APP_EVENT_SUBSCRIBE_FINAL(MODULE, ble_smp_transfer_event);
