/*
 * Copyright (c) 2022-2025 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/__assert.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/settings/settings.h>

#include <bluetooth/services/fast_pair/adv_manager.h>
#include <bluetooth/services/fast_pair/fast_pair.h>
#include <bluetooth/adv_prov/fast_pair.h>

#include <dk_buttons_and_leds.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(fp_sample, LOG_LEVEL_INF);

#include "hids_helper.h"
#include "battery_module.h"

#define RUN_STATUS_LED						DK_LED1
#define CON_STATUS_LED						DK_LED2
#define FP_ADV_MODE_STATUS_LED					DK_LED3

#define FP_ADV_MODE_BUTTON_MASK					DK_BTN1_MSK
#define VOLUME_UP_BUTTON_MASK					DK_BTN2_MSK
#define BOND_REMOVE_BUTTON_MASK					DK_BTN3_MSK
#define VOLUME_DOWN_BUTTON_MASK					DK_BTN4_MSK

#define RUN_LED_BLINK_INTERVAL_MS				1000
#define FP_ADV_MODE_SHOW_UI_INDICATION_LED_BLINK_INTERVAL_MS	500
#define FP_ADV_MODE_HIDE_UI_INDICATION_LED_BLINK_INTERVAL_MS	1500

#define FP_DISC_ADV_TIMEOUT_MINUTES				(10)

#define INIT_SEM_TIMEOUT_SECONDS				(60)

static bool pairing_mode = true;
static bool show_ui_pairing = true;
static struct bt_conn *peer;

static void init_work_handle(struct k_work *w);
static void fp_adv_mode_status_led_work_handle(struct k_work *w);
static void fp_disc_adv_timeout_work_handle(struct k_work *w);

static K_SEM_DEFINE(init_work_sem, 0, 1);
static K_WORK_DEFINE(init_work, init_work_handle);
static K_WORK_DELAYABLE_DEFINE(fp_adv_mode_status_led_work, fp_adv_mode_status_led_work_handle);
static K_WORK_DELAYABLE_DEFINE(fp_disc_adv_timeout_work, fp_disc_adv_timeout_work_handle);

/* Trigger used to configure advertising in the discoverable mode (pairing mode). */
BT_FAST_PAIR_ADV_MANAGER_TRIGGER_REGISTER(
	fp_adv_trigger_pairing_mode,
	"pairing_mode",
	(&(const struct bt_fast_pair_adv_manager_trigger_config) {
		.pairing_mode = true,
		.suspend_rpa = true,
	}));

/* Trigger used to configure advertising in the not discoverable mode (subsequent connections). */
BT_FAST_PAIR_ADV_MANAGER_TRIGGER_REGISTER(fp_adv_trigger_subsequent_mode, "subsequent_mode", NULL);

static void bond_cnt_cb(const struct bt_bond_info *info, void *user_data)
{
	size_t *cnt = user_data;

	(*cnt)++;
}

static size_t bond_cnt(void)
{
	size_t cnt = 0;

	bt_foreach_bond(BT_ID_DEFAULT, bond_cnt_cb, &cnt);

	return cnt;
}

static bool can_pair(void)
{
	return (bond_cnt() < CONFIG_BT_MAX_PAIRED);
}

static bool adv_payload_update_required(void)
{
	bool update_required;
	static bool prev_pairing_mode = true;
	static bool prev_show_ui_pairing = true;

	if ((prev_pairing_mode == pairing_mode) &&
	    (prev_show_ui_pairing != show_ui_pairing) &&
	    (peer == NULL)) {
		update_required = true;
	} else {
		update_required = false;
	}

	prev_pairing_mode = pairing_mode;
	prev_show_ui_pairing = show_ui_pairing;

	return update_required;
}

static void triggers_clear(void)
{
	bt_fast_pair_adv_manager_request(&fp_adv_trigger_pairing_mode, false);
	bt_fast_pair_adv_manager_request(&fp_adv_trigger_subsequent_mode, false);
}

static void triggers_update(void)
{
	if (!can_pair()) {
		if ((bt_fast_pair_adv_manager_is_pairing_mode()) || show_ui_pairing) {
			int ret;

			ARG_UNUSED(ret);

			LOG_INF("Automatically switching to not discoverable advertising, hide UI "
				"indication, because all bond slots are taken");

			show_ui_pairing = false;
			pairing_mode = false;

			ret = k_work_reschedule(&fp_adv_mode_status_led_work, K_NO_WAIT);
			__ASSERT_NO_MSG((ret == 0) || (ret == 1));
		}
	}

	bt_le_adv_prov_fast_pair_show_ui_pairing(show_ui_pairing);

	bt_fast_pair_adv_manager_request(&fp_adv_trigger_pairing_mode, pairing_mode);
	bt_fast_pair_adv_manager_request(&fp_adv_trigger_subsequent_mode, !pairing_mode);

	if (adv_payload_update_required()) {
		bt_fast_pair_adv_manager_payload_refresh();
	}

	if (pairing_mode) {
		LOG_INF("Triggers configured for discoverable mode");
	} else {
		LOG_INF("Triggers configured for not discoverable mode, %s UI indication enabled",
			show_ui_pairing ? "show" : "hide");

		(void) k_work_cancel_delayable(&fp_disc_adv_timeout_work);
	}
}

static void fp_adv_mode_status_led_work_handle(struct k_work *w)
{
	int ret;
	static bool led_on = true;

	ARG_UNUSED(ret);
	ARG_UNUSED(w);

	if (bt_fast_pair_adv_manager_is_pairing_mode()) {
		/* Discoverable mode. */
		dk_set_led_on(FP_ADV_MODE_STATUS_LED);
		return;
	}

	/* Not discoverable mode. */
	dk_set_led(FP_ADV_MODE_STATUS_LED, led_on);
	led_on = !led_on;

	ret = k_work_reschedule(&fp_adv_mode_status_led_work, show_ui_pairing ?
			K_MSEC(FP_ADV_MODE_SHOW_UI_INDICATION_LED_BLINK_INTERVAL_MS) :
			K_MSEC(FP_ADV_MODE_HIDE_UI_INDICATION_LED_BLINK_INTERVAL_MS));
	__ASSERT_NO_MSG(ret == 1);
}

static void fp_disc_adv_timeout_work_handle(struct k_work *w)
{
	ARG_UNUSED(w);

	__ASSERT_NO_MSG(pairing_mode);
	__ASSERT_NO_MSG(!peer);

	LOG_INF("Discoverable advertising timed out");

	/* Switch to not discoverable advertising showing UI indication. */
	pairing_mode = false;
	show_ui_pairing = true;

	fp_adv_mode_status_led_work_handle(NULL);
	triggers_update();
}

static void connected(struct bt_conn *conn, uint8_t err)
{
	LOG_INF("Connected");

	dk_set_led_on(CON_STATUS_LED);
	peer = conn;
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	LOG_INF("Disconnected, reason 0x%02x %s", reason, bt_hci_err_to_str(reason));

	dk_set_led_off(CON_STATUS_LED);
	peer = NULL;
}

static void security_changed(struct bt_conn *conn, bt_security_t level, enum bt_security_err err)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	if (!err) {
		LOG_INF("Security changed: %s level %u", addr, level);
	} else {
		LOG_WRN("Security failed: %s level %u err %d %s", addr, level, err,
			bt_security_err_to_str(err));
	}
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected        = connected,
	.disconnected     = disconnected,
	.security_changed = security_changed,
};

static void pairing_complete(struct bt_conn *conn, bool bonded)
{
	if (bonded && (bt_fast_pair_adv_manager_is_pairing_mode())) {
		int ret;

		pairing_mode = false;
		show_ui_pairing = true;

		triggers_update();

		ret = k_work_reschedule(&fp_adv_mode_status_led_work, K_NO_WAIT);
		__ASSERT_NO_MSG((ret == 0) || (ret == 1));
		ARG_UNUSED(ret);
	}
}

static enum bt_security_err pairing_accept(struct bt_conn *conn,
					   const struct bt_conn_pairing_feat *const feat)
{
	ARG_UNUSED(conn);
	ARG_UNUSED(feat);

	enum bt_security_err ret;

	if (pairing_mode) {
		LOG_WRN("Normal Bluetooth pairing not allowed outside of pairing mode");
		ret = BT_SECURITY_ERR_PAIR_NOT_ALLOWED;
	} else {
		LOG_INF("Accept normal Bluetooth pairing");
		ret = BT_SECURITY_ERR_SUCCESS;
	}

	return ret;
}

static const char *volume_change_to_str(enum hids_helper_volume_change volume_change)
{
	const char *res = NULL;

	switch (volume_change) {
	case HIDS_HELPER_VOLUME_CHANGE_DOWN:
		res = "Decrease";
		break;

	case HIDS_HELPER_VOLUME_CHANGE_NONE:
		break;

	case HIDS_HELPER_VOLUME_CHANGE_UP:
		res = "Increase";
		break;

	default:
		/* Should not happen. */
		__ASSERT_NO_MSG(false);
		break;
	}

	return res;
}

static void hid_volume_control_send(enum hids_helper_volume_change volume_change)
{
	const char *operation_str = volume_change_to_str(volume_change);
	int err = hids_helper_volume_ctrl(volume_change);

	if (!err) {
		if (operation_str) {
			LOG_INF("%s audio volume", operation_str);
		}
	} else {
		/* HID host not connected or not subscribed. Silently drop HID data. */
	}
}

static void volume_control_btn_handle(uint32_t button_state, uint32_t has_changed)
{
	static enum hids_helper_volume_change volume_change = HIDS_HELPER_VOLUME_CHANGE_NONE;
	enum hids_helper_volume_change new_volume_change = volume_change;

	if (has_changed & VOLUME_UP_BUTTON_MASK) {
		if (button_state & VOLUME_UP_BUTTON_MASK) {
			new_volume_change = HIDS_HELPER_VOLUME_CHANGE_UP;
		} else if (volume_change == HIDS_HELPER_VOLUME_CHANGE_UP) {
			new_volume_change = HIDS_HELPER_VOLUME_CHANGE_NONE;
		}
	}

	if (has_changed & VOLUME_DOWN_BUTTON_MASK) {
		if (button_state & VOLUME_DOWN_BUTTON_MASK) {
			new_volume_change = HIDS_HELPER_VOLUME_CHANGE_DOWN;
		} else if (volume_change == HIDS_HELPER_VOLUME_CHANGE_DOWN) {
			new_volume_change = HIDS_HELPER_VOLUME_CHANGE_NONE;
		}
	}

	if (volume_change != new_volume_change) {
		volume_change = new_volume_change;
		hid_volume_control_send(volume_change);
	}
}

static void fp_adv_mode_btn_handle(uint32_t button_state, uint32_t has_changed)
{
	uint32_t button_pressed = button_state & has_changed;

	if (button_pressed & FP_ADV_MODE_BUTTON_MASK) {
		int ret;

		if (pairing_mode) {
			pairing_mode = false;
			show_ui_pairing = true;
		} else {
			if (show_ui_pairing) {
				show_ui_pairing = false;
			} else {
				pairing_mode = true;
			}
		}

		triggers_update();

		ret = k_work_reschedule(&fp_adv_mode_status_led_work, K_NO_WAIT);
		__ASSERT_NO_MSG((ret == 0) || (ret == 1));
		ARG_UNUSED(ret);
	}
}

static void bond_remove_btn_handle(uint32_t button_state, uint32_t has_changed)
{
	uint32_t button_pressed = button_state & has_changed;

	if (button_pressed & BOND_REMOVE_BUTTON_MASK) {
		int err;

		triggers_clear();

		err = bt_unpair(BT_ID_DEFAULT, NULL);
		if (err) {
			LOG_ERR("Cannot remove bonds (err %d)", err);
		} else {
			LOG_INF("Bonds removed");
		}

		triggers_update();
	}
}

static void button_changed(uint32_t button_state, uint32_t has_changed)
{
	__ASSERT_NO_MSG(!k_is_in_isr());
	__ASSERT_NO_MSG(!k_is_preempt_thread());

	fp_adv_mode_btn_handle(button_state, has_changed);
	volume_control_btn_handle(button_state, has_changed);
	bond_remove_btn_handle(button_state, has_changed);
}

static void fp_account_key_written(struct bt_conn *conn)
{
	LOG_INF("Fast Pair Account Key has been written");
}

static void fp_adv_state_changed(bool active)
{
	if (!bt_fast_pair_adv_manager_is_pairing_mode()) {
		/* The advertising does not use the Fast Pair discoverable mode.*/
		return;
	}

	if (!active) {
		(void) k_work_cancel_delayable(&fp_disc_adv_timeout_work);
	} else {
		(void) k_work_reschedule(&fp_disc_adv_timeout_work,
					 K_MINUTES(FP_DISC_ADV_TIMEOUT_MINUTES));
	}
}

static struct bt_fast_pair_adv_manager_info_cb fp_adv_info_cb = {
	.adv_state_changed = fp_adv_state_changed,
};

static void init_work_handle(struct k_work *w)
{
	int err;
	int ret;
	static const struct bt_conn_auth_cb conn_auth_callbacks = {
		.pairing_accept = pairing_accept,
	};
	static struct bt_conn_auth_info_cb auth_info_cb = {
		.pairing_complete = pairing_complete
	};
	static struct bt_fast_pair_info_cb fp_info_callbacks = {
		.account_key_written = fp_account_key_written,
	};

	/* It is assumed that this function executes in the cooperative thread context. */
	__ASSERT_NO_MSG(!k_is_preempt_thread());
	__ASSERT_NO_MSG(!k_is_in_isr());

	err = dk_leds_init();
	if (err) {
		LOG_ERR("LEDs init failed (err %d)", err);
		return;
	}

	err = dk_buttons_init(button_changed);
	if (err) {
		LOG_ERR("Buttons init failed (err %d)", err);
		return;
	}

	err = bt_conn_auth_cb_register(&conn_auth_callbacks);
	if (err) {
		LOG_ERR("Registering authentication callbacks failed (err %d)", err);
		return;
	}

	err = bt_conn_auth_info_cb_register(&auth_info_cb);
	if (err) {
		LOG_ERR("Registering authentication info callbacks failed (err %d)", err);
		return;
	}

	err = bt_fast_pair_info_cb_register(&fp_info_callbacks);
	if (err) {
		LOG_ERR("Registering Fast Pair info callbacks failed (err %d)", err);
		return;
	}

	err = hids_helper_init();
	if (err) {
		LOG_ERR("HIDS init failed (err %d)", err);
		return;
	}

	err = bt_enable(NULL);
	if (err) {
		LOG_ERR("Bluetooth init failed (err %d)", err);
		return;
	}

	LOG_INF("Bluetooth initialized");

	err = settings_load();
	if (err) {
		LOG_ERR("Settings load failed (err: %d)", err);
		return;
	}

	LOG_INF("Settings loaded");

	err = bt_fast_pair_enable();
	if (err) {
		LOG_ERR("Fast Pair enable failed (err: %d)", err);
		return;
	}

	err = battery_module_init();
	if (err) {
		LOG_ERR("Battery module init failed (err %d)", err);
		return;
	}

	err = bt_le_adv_prov_fast_pair_set_battery_mode(BT_FAST_PAIR_ADV_BATTERY_MODE_SHOW_UI_IND);
	if (err) {
		LOG_ERR("Setting advertising battery mode failed (err %d)", err);
		return;
	}

	err = bt_fast_pair_adv_manager_info_cb_register(&fp_adv_info_cb);
	if (err) {
		LOG_ERR("Registering Fast Pair Advertising Manager callbacks failed (err %d)",
			err);
		return;
	}

	triggers_update();

	err = bt_fast_pair_adv_manager_enable();
	if (err) {
		LOG_ERR("Fast Pair Advertising Manager enable failed failed (err %d)", err);
		return;
	}

	ret = k_work_schedule(&fp_adv_mode_status_led_work, K_NO_WAIT);
	__ASSERT_NO_MSG(ret == 1);
	ARG_UNUSED(ret);

	k_sem_give(&init_work_sem);
}

int main(void)
{
	bool run_led_on = true;
	int err;

	LOG_INF("Starting Bluetooth Fast Pair input device sample");

	/* Switch to the cooperative thread context before interaction
	 * with the Fast Pair API.
	 */
	(void) k_work_submit(&init_work);
	err = k_sem_take(&init_work_sem, K_SECONDS(INIT_SEM_TIMEOUT_SECONDS));
	if (err) {
		k_panic();
		return 0;
	}

	LOG_INF("Sample has started");

	for (;;) {
		dk_set_led(RUN_STATUS_LED, run_led_on);
		run_led_on = !run_led_on;
		k_sleep(K_MSEC(RUN_LED_BLINK_INTERVAL_MS));
	}
}
