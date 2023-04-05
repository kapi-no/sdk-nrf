/*
 * Copyright (c) 2023 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef _DFU_LOCK_H_
#define _DFU_LOCK_H_

#include <zephyr/types.h>

struct dfu_lock_owner {
	const char *name;

	void (*owner_changed)(const struct dfu_lock_owner *new_owner);
};

bool dfu_lock_try(const struct dfu_lock_owner *new_owner);

void dfu_lock_release(const struct dfu_lock_owner *owner);

bool dfu_lock_owner_check(const struct dfu_lock_owner *owner);

bool dfu_lock_owner_check_and_try(const struct dfu_lock_owner *owner);

#endif /* _DFU_LOCK_H_ */
