/*
 * Copyright (c) 2023 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>

#include <zephyr/logging/log.h>
#include <zephyr/logging/log_ctrl.h>
LOG_MODULE_REGISTER(dfu_lock, CONFIG_DESKTOP_DFU_LOCK_LOG_LEVEL);

#include "dfu_lock.h"

static const struct dfu_lock_owner *previous_owner;
static const struct dfu_lock_owner *current_owner;

static atomic_t dfu_lock = ATOMIC_INIT(false);

bool dfu_lock_try(const struct dfu_lock_owner *new_owner)
{
	bool is_ownership_changed;

	__ASSERT_NO_MSG(new_owner != NULL);

	is_ownership_changed = atomic_cas(&dfu_lock, false, true);
	if (!is_ownership_changed) {
		return false;
	}

	__ASSERT_NO_MSG(current_owner == NULL);
	current_owner = new_owner;

	LOG_DBG("New DFU owner locked: %s", new_owner->name);

	if (previous_owner && previous_owner->owner_changed) {
		if (previous_owner != current_owner) {
			previous_owner->owner_changed(new_owner);
		}
	}

	return true;
}

void dfu_lock_release(const struct dfu_lock_owner *owner)
{
	bool is_ownership_released;

	if (current_owner != owner) {
		return;
	}

	previous_owner = current_owner;
	current_owner = NULL;

	is_ownership_released = atomic_cas(&dfu_lock, true, false);
	__ASSERT_NO_MSG(is_ownership_released);

	LOG_DBG("DFU lock released by %s", owner->name);
}

bool dfu_lock_owner_check(const struct dfu_lock_owner *owner)
{
	return (owner == current_owner);
}

bool dfu_lock_owner_check_and_try(const struct dfu_lock_owner *owner)
{
	bool is_ownership_changed;

	if (dfu_lock_owner_check(owner)) {
		return true;
	}

	is_ownership_changed = dfu_lock_try(owner);
	if (is_ownership_changed) {
		return true;
	}

	LOG_WRN("DFU lock failed by %s because of %s ownership",
		owner->name, current_owner->name);

	return false;
}
