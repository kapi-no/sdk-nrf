/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef _FP_FMDN_BEACON_ACTIONS_H_
#define _FP_FMDN_BEACON_ACTIONS_H_

#include <zephyr/bluetooth/gatt.h>

#include <bluetooth/services/fast_pair/fmdn.h>

/**
 * @defgroup fp_fmdn_beacon_actions Fast Pair FMDN Beacon Actions
 * @brief Fast Pair FMDN Beacon Actions module for the FMDN extension.
 *
 * Fast Pair FMDN Beacon Actions module handles requests that are sent
 * on the Beacon Actions characteristics and are relevant for the FMDN
 * extension.
 *
 * @{
 */

#ifdef __cplusplus
extern "C" {
#endif

/* Beacon Actions GATT Characteristic UUID defined by the FMDN Accessory specification. */
#define FP_FMDN_BEACON_ACTIONS_UUID \
	BT_UUID_DECLARE_128(BT_UUID_128_ENCODE(0xFE2C1238, 0x8366, 0x4814, 0x8EB0, 0x01DE32100BEA))

/* Beacon Actions GATT Characteristic definition for the Fast Pair service. */
#define FP_FMDN_BEACON_ACTIONS_CHARACTERISTIC                                 \
	BT_GATT_CHARACTERISTIC(FP_FMDN_BEACON_ACTIONS_UUID,                   \
		BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE | BT_GATT_CHRC_NOTIFY, \
		BT_GATT_PERM_READ | BT_GATT_PERM_WRITE,                       \
		fp_fmdn_beacon_actions_read,                                  \
		fp_fmdn_beacon_actions_write,                                 \
		NULL),                                                        \
	BT_GATT_CCC(fp_fmdn_beacon_actions_ccc_cfg_changed,                   \
		BT_GATT_PERM_READ | BT_GATT_PERM_WRITE)

/* Handle the read operation on the Beacon Actions characteristic. */
ssize_t fp_fmdn_beacon_actions_read(struct bt_conn *conn,
				    const struct bt_gatt_attr *attr,
				    void *buf,
				    uint16_t len,
				    uint16_t offset);

/* Handle the write operation on the Beacon Actions characteristic. */
ssize_t fp_fmdn_beacon_actions_write(struct bt_conn *conn,
				     const struct bt_gatt_attr *attr,
				     const void *buf,
				     uint16_t len,
				     uint16_t offset,
				     uint8_t flags);

/* Handle the write operation on the CCC descriptor of the Beacon Actions characteristic. */
void fp_fmdn_beacon_actions_ccc_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value);

#ifdef __cplusplus
}
#endif

/**
 * @}
 */

#endif /* _FP_FMDN_BEACON_ACTIONS_H_ */
