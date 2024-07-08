/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/types.h>
#include <errno.h>
#include <zephyr/kernel.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>

#include <zephyr/settings/settings.h>

#include <dk_buttons_and_leds.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(bt_multi_use_case, LOG_LEVEL_INF);

/* Semaphore timeout in seconds. */
#define INIT_SEM_TIMEOUT (60)

/* Identifiers of Bluetooth identities for use case A and B.*/
#define BT_ID_USE_CASE_A (1)
#define BT_ID_USE_CASE_B (2)

BUILD_ASSERT(CONFIG_BT_ID_MAX > BT_ID_USE_CASE_A);
BUILD_ASSERT(CONFIG_BT_ID_MAX > BT_ID_USE_CASE_B);
BUILD_ASSERT(BT_ID_USE_CASE_B > BT_ID_USE_CASE_A);

/* Bluetooth connection limit for use case A and B. */
#define BT_MAX_CONN_USE_CASE_A (1)
#define BT_MAX_CONN_USE_CASE_B (1)

BUILD_ASSERT(CONFIG_BT_MAX_CONN >= (BT_MAX_CONN_USE_CASE_A + BT_MAX_CONN_USE_CASE_B));

/* Names encoded in the advertising data for use case A and B. */
#define NAME_USE_CASE_A "NCS use case A"
#define NAME_USE_CASE_B "NCS use case B"
#define NAME_LEN(_name) (sizeof(_name) - 1)

/* Bluetooth GATT UUIDs for use case A and B. */
#define BT_UUID_UCA_SERVICE BT_UUID_DECLARE_128( \
	BT_UUID_128_ENCODE(0xD3E55223, 0x659C, 0x457A, 0xA9EE, 0xF0991ED8BB61))
#define BT_UUID_UCA_NAME BT_UUID_DECLARE_128( \
	BT_UUID_128_ENCODE(0x21C5F9BD, 0xAD9C, 0x4D18, 0x8F3A, 0x411E6E3ADC71))

#define BT_UUID_UCB_SERVICE BT_UUID_DECLARE_128( \
	BT_UUID_128_ENCODE(0x3822749C, 0xBCCD, 0x4595, 0xB61F, 0xA6C49A81BDFD))
#define BT_UUID_UCB_NAME BT_UUID_DECLARE_128( \
	BT_UUID_128_ENCODE(0x27B42154, 0x4E54, 0x48FB, 0xADF0, 0xBEB49F74796C))

/* Length of the Bluetooth GATT Name value for use case A and B. */
#define BT_GATT_NAME_LEN_USE_CASE_A (30)
#define BT_GATT_NAME_LEN_USE_CASE_B (40)

/* LED related defines. */
#define RUN_STATUS_LED          DK_LED1
#define RUN_LED_BLINK_INTERVAL  1000
#define CONN_LED_USE_CASE_A     DK_LED2
#define CONN_LED_USE_CASE_B     DK_LED3

static void init_work_handle(struct k_work *w);

static K_SEM_DEFINE(init_work_sem, 0, 1);
static K_WORK_DEFINE(init_work, init_work_handle);

static struct bt_le_ext_adv *adv_set_use_case_a;
static struct bt_le_ext_adv *adv_set_use_case_b;

BUILD_ASSERT(CONFIG_BT_EXT_ADV_MAX_ADV_SET >= 2);

static void adv_use_case_a_restart_work_handle(struct k_work *w);
static void adv_use_case_b_restart_work_handle(struct k_work *w);

static K_WORK_DEFINE(adv_use_case_a_restart_work, adv_use_case_a_restart_work_handle);
static K_WORK_DEFINE(adv_use_case_b_restart_work, adv_use_case_b_restart_work_handle);

static const struct bt_data ad_use_case_a[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA(BT_DATA_NAME_COMPLETE, NAME_USE_CASE_A, NAME_LEN(NAME_USE_CASE_A)),
};

static const struct bt_data ad_use_case_b[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA(BT_DATA_NAME_COMPLETE, NAME_USE_CASE_B, NAME_LEN(NAME_USE_CASE_B)),
};

static uint8_t conn_cnt_use_case_a;
static uint8_t conn_cnt_use_case_b;

static char gatt_name_use_case_a[BT_GATT_NAME_LEN_USE_CASE_A] = "Use Case A";
static char gatt_name_use_case_b[BT_GATT_NAME_LEN_USE_CASE_B] = "Use Case B";

static ssize_t use_case_a_name_read(struct bt_conn *conn, const struct bt_gatt_attr *attr,
				    void *buf, uint16_t len, uint16_t offset)
{
	int err;
	struct bt_conn_info conn_info;

	err = bt_conn_get_info(conn, &conn_info);
	__ASSERT_NO_MSG(!err);

	LOG_INF("Use case A: GATT Read: handle %d, conn %p, id %d",
		bt_gatt_attr_value_handle(attr), (void *)conn, conn_info.id);

	if (conn_info.id != BT_ID_USE_CASE_A) {
		LOG_WRN("Use case A: GATT Read: invalid id: id=%d", conn_info.id);
		return BT_GATT_ERR(BT_ATT_ERR_READ_NOT_PERMITTED);
	}

	if (offset != 0) {
		LOG_WRN("Use case A: GATT Read: invalid offset: off=%d", offset);
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
	}

	return bt_gatt_attr_read(conn, attr, buf, len, offset, gatt_name_use_case_a,
				 strnlen(gatt_name_use_case_a, sizeof(gatt_name_use_case_a)));
}

static ssize_t use_case_a_name_write(struct bt_conn *conn, const struct bt_gatt_attr *attr,
				     const void *buf, uint16_t len, uint16_t offset, uint8_t flags)
{
	int err;
	struct bt_conn_info conn_info;

	err = bt_conn_get_info(conn, &conn_info);
	__ASSERT_NO_MSG(!err);

	LOG_INF("Use case A: GATT Write: handle %d, conn %p, id %d",
		bt_gatt_attr_value_handle(attr), (void *)conn, conn_info.id);

	if (conn_info.id != BT_ID_USE_CASE_A) {
		LOG_WRN("Use case A: GATT Write: invalid id: id=%d", conn_info.id);
		return BT_GATT_ERR(BT_ATT_ERR_WRITE_NOT_PERMITTED);
	}

	if (offset != 0) {
		LOG_WRN("Use case A: GATT Write: invalid offset: off=%d", offset);
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
	}

	if (len > sizeof(gatt_name_use_case_a)) {
		LOG_WRN("Use case A: GATT Write: invalid length: off=%d", offset);
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
	}

	memcpy(gatt_name_use_case_a, buf, len);
	if (len < sizeof(gatt_name_use_case_a)) {
		gatt_name_use_case_a[len] = 0;
	}

	return len;
}

/* Service Declaration for use case A. */
BT_GATT_SERVICE_DEFINE(use_case_a_svc,
BT_GATT_PRIMARY_SERVICE(BT_UUID_UCA_SERVICE),
	BT_GATT_CHARACTERISTIC(BT_UUID_UCA_NAME,
			       BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE,
			       BT_GATT_PERM_READ | BT_GATT_PERM_WRITE,
			       use_case_a_name_read, use_case_a_name_write, NULL),
);

static ssize_t use_case_b_name_read(struct bt_conn *conn, const struct bt_gatt_attr *attr,
				    void *buf, uint16_t len, uint16_t offset)
{
	int err;
	struct bt_conn_info conn_info;

	err = bt_conn_get_info(conn, &conn_info);
	__ASSERT_NO_MSG(!err);

	LOG_INF("Use case B: GATT Read: handle %d, conn %p, id %d",
		bt_gatt_attr_value_handle(attr), (void *)conn, conn_info.id);

	if (conn_info.id != BT_ID_USE_CASE_B) {
		LOG_WRN("Use case B: GATT Read: invalid id: id=%d", conn_info.id);
		return BT_GATT_ERR(BT_ATT_ERR_READ_NOT_PERMITTED);
	}

	if (offset != 0) {
		LOG_WRN("Use case B: GATT Read: invalid offset: off=%d", offset);
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
	}

	return bt_gatt_attr_read(conn, attr, buf, len, offset, gatt_name_use_case_b,
				 strnlen(gatt_name_use_case_b, sizeof(gatt_name_use_case_b)));
}

static ssize_t use_case_b_name_write(struct bt_conn *conn, const struct bt_gatt_attr *attr,
				     const void *buf, uint16_t len, uint16_t offset, uint8_t flags)
{
	int err;
	struct bt_conn_info conn_info;

	err = bt_conn_get_info(conn, &conn_info);
	__ASSERT_NO_MSG(!err);

	LOG_INF("Use case B: GATT Write: handle %d, conn %p, id %d",
		bt_gatt_attr_value_handle(attr), (void *)conn, conn_info.id);

	if (conn_info.id != BT_ID_USE_CASE_B) {
		LOG_WRN("Use case B: GATT Write: invalid id: id=%d", conn_info.id);
		return BT_GATT_ERR(BT_ATT_ERR_WRITE_NOT_PERMITTED);
	}

	if (offset != 0) {
		LOG_WRN("Use case B: GATT Write: invalid offset: off=%d", offset);
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
	}

	if (len > sizeof(gatt_name_use_case_b)) {
		LOG_WRN("Use case B: GATT Write: invalid length: off=%d", offset);
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
	}

	memcpy(gatt_name_use_case_b, buf, len);
	if (len < sizeof(gatt_name_use_case_b)) {
		gatt_name_use_case_b[len] = 0;
	}

	return len;
}

/* Service Declaration for use case B. */
BT_GATT_SERVICE_DEFINE(use_case_b_svc,
BT_GATT_PRIMARY_SERVICE(BT_UUID_UCB_SERVICE),
	BT_GATT_CHARACTERISTIC(BT_UUID_UCB_NAME,
			       BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE,
			       BT_GATT_PERM_READ | BT_GATT_PERM_WRITE,
			       use_case_b_name_read, use_case_b_name_write, NULL),
);

static int bt_adv_set_use_case_a_start(void)
{
	int err;
	struct bt_le_ext_adv_start_param ext_adv_start_param = {0};

	__ASSERT_NO_MSG(adv_set_use_case_a);

	if (conn_cnt_use_case_a >= BT_MAX_CONN_USE_CASE_A) {
		LOG_DBG("Use case A: connection limit reached: advertising cannot be resumed");
		return 0;
	}

	err = bt_le_ext_adv_start(adv_set_use_case_a, &ext_adv_start_param);
	if (err) {
		LOG_ERR("Use case A: bt_le_ext_adv_start returned error: %d", err);
		return err;
	}

	return 0;
}

static int bt_adv_set_use_case_b_start(void)
{
	int err;
	struct bt_le_ext_adv_start_param ext_adv_start_param = {0};

	__ASSERT_NO_MSG(adv_set_use_case_b);


	if (conn_cnt_use_case_b >= BT_MAX_CONN_USE_CASE_B) {
		LOG_DBG("Use case B: connection limit reached: advertising cannot be resumed");
		return 0;
	}

	err = bt_le_ext_adv_start(adv_set_use_case_b, &ext_adv_start_param);
	if (err) {
		LOG_ERR("Use case B: bt_le_ext_adv_start returned error: %d", err);
		return err;
	}

	return 0;
}

static void adv_use_case_a_restart_work_handle(struct k_work *w)
{
	bt_adv_set_use_case_a_start();
}

static void adv_use_case_b_restart_work_handle(struct k_work *w)
{
	bt_adv_set_use_case_b_start();
}

static void connected(struct bt_conn *conn, uint8_t conn_err)
{
	int err;
	char *use_case_id_str;
	char addr_str[BT_ADDR_LE_STR_LEN];
	struct bt_conn_info conn_info;

	/* Obtain additional connection information to check Bluetooth identity that
	 * is attached to the connection object.
	 */
	err = bt_conn_get_info(conn, &conn_info);
	__ASSERT_NO_MSG(!err);

	/* Check the Bluetooth identity attached to this connection object. */
	__ASSERT_NO_MSG((conn_info.id == BT_ID_USE_CASE_A) ||
			(conn_info.id == BT_ID_USE_CASE_B));
	use_case_id_str = (conn_info.id == BT_ID_USE_CASE_A) ? "A" : "B";

	/* Encode the connected peer address. */
	bt_addr_le_to_str(bt_conn_get_dst(conn), addr_str, sizeof(addr_str));

	if (conn_err) {
		LOG_ERR("Connection failed with %s for use case %s (err %u)",
			addr_str, use_case_id_str, conn_err);
		return;
	}

	LOG_INF("Connected with %s for use case %s conn %p",
		addr_str, use_case_id_str, (void *)conn);

	/* Increase the connection counter and start advertising if there are free connection
	 * slots allocated to the given use case. */
	if (conn_info.id == BT_ID_USE_CASE_A) {
		conn_cnt_use_case_a++;
		bt_adv_set_use_case_a_start();

		dk_set_led(CONN_LED_USE_CASE_A, true);
	} else {
		/* Use case B */
		conn_cnt_use_case_b++;
		bt_adv_set_use_case_b_start();

		dk_set_led(CONN_LED_USE_CASE_B, true);
	}
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	int err;
	char *use_case_id_str;
	char addr_str[BT_ADDR_LE_STR_LEN];
	struct bt_conn_info conn_info;

	/* Obtain additional connection information to check Bluetooth identity that
	 * is attached to the connection object.
	 */
	err = bt_conn_get_info(conn, &conn_info);
	__ASSERT_NO_MSG(!err);

	/* Check the Bluetooth identity attached to this connection object. */
	__ASSERT_NO_MSG((conn_info.id == BT_ID_USE_CASE_A) ||
			(conn_info.id == BT_ID_USE_CASE_B));
	use_case_id_str = (conn_info.id == BT_ID_USE_CASE_A) ? "A" : "B";

	/* Encode the disconnected peer address. */
	bt_addr_le_to_str(bt_conn_get_dst(conn), addr_str, sizeof(addr_str));

	LOG_INF("Disconnected (reason %u) with %s for use case %s conn %p",
		reason, addr_str, use_case_id_str, (void *)conn);

	/* Decrease the connection counter and start advertising. */
	if (conn_info.id == BT_ID_USE_CASE_A) {
		conn_cnt_use_case_a--;
		(void) k_work_submit(&adv_use_case_a_restart_work);

		if (conn_cnt_use_case_a == 0) {
			dk_set_led(CONN_LED_USE_CASE_A, false);
		}
	} else {
		/* Use case B */
		conn_cnt_use_case_b--;
		(void) k_work_submit(&adv_use_case_b_restart_work);

		if (conn_cnt_use_case_b == 0) {
			dk_set_led(CONN_LED_USE_CASE_B, false);
		}
	}
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected        = connected,
	.disconnected     = disconnected,
};

static int bt_adv_set_use_case_a_setup(void)
{
	int err;
	static struct bt_le_adv_param adv_param = {
		.id = BT_ID_USE_CASE_A,
		.options = (BT_LE_ADV_OPT_CONNECTABLE),
		.interval_min = BT_GAP_ADV_FAST_INT_MIN_1, /* 30 ms */
		.interval_max = BT_GAP_ADV_FAST_INT_MAX_1, /* 60 ms */
	};

	__ASSERT_NO_MSG(!adv_set_use_case_a);

	/* Create the advertising set for use case A. */
	err = bt_le_ext_adv_create(&adv_param, NULL, &adv_set_use_case_a);
	if (err) {
		LOG_ERR("Use case A: bt_le_ext_adv_create returned error: %d", err);
		return err;
	}

	/* Set the advertising data for use case A. */
	err = bt_le_ext_adv_set_data(adv_set_use_case_a, ad_use_case_a, ARRAY_SIZE(ad_use_case_a),
				     NULL, 0);
	if (err) {
		LOG_ERR("Use case A: bt_le_ext_adv_set_data returned error: %d", err);
		return err;
	}

	return 0;
}

static int bt_adv_set_use_case_b_setup(void)
{
	int err;
	static struct bt_le_adv_param adv_param = {
		.id = BT_ID_USE_CASE_B,
		.options = (BT_LE_ADV_OPT_CONNECTABLE),
		.interval_min = BT_GAP_ADV_FAST_INT_MIN_2, /* 100 ms */
		.interval_max = BT_GAP_ADV_FAST_INT_MAX_2, /* 150 ms */
	};

	__ASSERT_NO_MSG(!adv_set_use_case_b);

	/* Create the advertising set for use case B. */
	err = bt_le_ext_adv_create(&adv_param, NULL, &adv_set_use_case_b);
	if (err) {
		LOG_ERR("Use case B: bt_le_ext_adv_create returned error: %d", err);
		return err;
	}

	/* Set the advertising data for use case B. */
	err = bt_le_ext_adv_set_data(adv_set_use_case_b, ad_use_case_b, ARRAY_SIZE(ad_use_case_b),
				     NULL, 0);
	if (err) {
		LOG_ERR("Use case B: bt_le_ext_adv_set_data returned error: %d", err);
		return err;
	}

	return 0;
}

static int bt_ids_create(void)
{
	int ret;
	size_t count;

	/* Check if Bluetooth identites weren't already created. */
	bt_id_get(NULL, &count);
	if (count > BT_ID_USE_CASE_B) {
		return 0;
	}

	/* Create the application identity. */
	do {
		ret = bt_id_create(NULL, NULL);
		if (ret < 0) {
			return ret;
		}
	} while (ret != BT_ID_USE_CASE_B);

	return 0;
}

static void init_work_handle(struct k_work *w)
{
	int err;

	err = dk_leds_init();
	if (err) {
		LOG_ERR("LEDs init failed (err %d)", err);
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

	/* Create Bluetooth Identities for use case A and B. */
	err = bt_ids_create();
	if (err) {
		LOG_ERR("Bluetooth identity failed to create (err %d)", err);
		return;
	}

	/* Set up Bluetooth advertising sets for use case A and B. */
	err = bt_adv_set_use_case_a_setup();
	if (err) {
		LOG_ERR("Setup of Bluetooth advertiser for use case A failed (err %d)", err);
		return;
	}

	err = bt_adv_set_use_case_b_setup();
	if (err) {
		LOG_ERR("Setup of Bluetooth advertiser for use case B failed (err %d)", err);
		return;
	}

	/* Start Bluetooth advertising for use case A and B. */
	err = bt_adv_set_use_case_a_start();
	if (err) {
		LOG_ERR("Bluetooth advertising for use case A failed (err %d)", err);
		return;
	}

	err = bt_adv_set_use_case_b_start();
	if (err) {
		LOG_ERR("Bluetooth advertising for use case B failed (err %d)", err);
		return;
	}

	k_sem_give(&init_work_sem);
}

int main(void)
{
	int err;
	int blink_status = 0;

	LOG_INF("Starting Bluetooth Multiple Use Case example");

	/* Switch to the cooperative thread context before interaction
	 * with the Bluetooth API.
	 */
	(void) k_work_submit(&init_work);
	err = k_sem_take(&init_work_sem, K_SECONDS(INIT_SEM_TIMEOUT));
	if (err) {
		k_panic();
		return 0;
	}

	for (;;) {
		dk_set_led(RUN_STATUS_LED, (++blink_status) % 2);
		k_sleep(K_MSEC(RUN_LED_BLINK_INTERVAL));
	}
}
