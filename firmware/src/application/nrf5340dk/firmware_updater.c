#include "firmware_updater.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/dfu/mcuboot.h>
#include <firmups-device-sdk/sdk.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/dfu/flash_img.h>
#include <zephyr/random/random.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>

#include <stdio.h>

/* ===================== Types/Defines ===================== */
LOG_MODULE_REGISTER(firmware_updater, CONFIG_FIRMUPS_LOG_LEVEL);

/* FIRMUPS gateway GATT UUIDs (must match the RW612 peripheral) */
#define BT_UUID_FIRMUPS_GW_SVC_VAL                                                                 \
	BT_UUID_128_ENCODE(0xf1e20001, 0x1a2b, 0x4b7e, 0x9a1c, 0x8d3f2e5c7b0aULL)
#define BT_UUID_FIRMUPS_GW_MSG_VAL                                                                 \
	BT_UUID_128_ENCODE(0xf1e20002, 0x1a2b, 0x4b7e, 0x9a1c, 0x8d3f2e5c7b0aULL)
#define BT_UUID_FIRMUPS_GW_RESP_VAL                                                                \
	BT_UUID_128_ENCODE(0xf1e20003, 0x1a2b, 0x4b7e, 0x9a1c, 0x8d3f2e5c7b0aULL)

static struct bt_uuid_128 uuid_svc = BT_UUID_INIT_128(BT_UUID_FIRMUPS_GW_SVC_VAL);
static struct bt_uuid_128 uuid_msg = BT_UUID_INIT_128(BT_UUID_FIRMUPS_GW_MSG_VAL);
static struct bt_uuid_128 uuid_resp = BT_UUID_INIT_128(BT_UUID_FIRMUPS_GW_RESP_VAL);

struct firmware_updater_context {
	uint32_t firmware_version;
	uint32_t desired_firmware_version;
	struct firmups_sdk_context *firmups_context;
};

/* ===================== BLE central state ===================== */
static struct bt_conn *ble_conn;
static uint16_t msg_char_handle;
static struct bt_gatt_subscribe_params notify_params;
static struct bt_gatt_discover_params disc_params;
static struct bt_gatt_exchange_params mtu_params;

static K_SEM_DEFINE(response_sem, 0, 1);
static K_SEM_DEFINE(ble_ready_sem, 0, 1);

static uint8_t *g_response_buf;
static uint16_t *g_response_size;
static uint16_t g_response_capacity;

/* ===================== Predeclarations ===================== */
static enum firmups_sdk_error_code random_bytes(uint8_t *buffer, uint16_t size, void *userdata);
static enum firmups_sdk_error_code get_key(uint8_t *key_buffer, uint16_t key_buffer_size,
					   void *userdata);
static enum firmups_sdk_error_code ble_send_data(uint8_t *send_buffer, uint16_t send_buffer_size,
						 uint8_t *response_buffer,
						 uint16_t response_buffer_size,
						 uint16_t *response_size, void *userdata);

/* ===================== BLE notification callback ===================== */
static uint8_t notify_cb(struct bt_conn *conn, struct bt_gatt_subscribe_params *params,
			 const void *data, uint16_t len)
{
	ARG_UNUSED(conn);
	ARG_UNUSED(params);

	if (data == NULL) {
		/* Unsubscribed */
		return BT_GATT_ITER_STOP;
	}

	uint16_t copy_len = MIN(len, g_response_capacity);

	memcpy(g_response_buf, data, copy_len);
	*g_response_size = copy_len;
	k_sem_give(&response_sem);
	return BT_GATT_ITER_CONTINUE;
}

/* ===================== GATT discovery ===================== */
static uint8_t discover_cb(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			   struct bt_gatt_discover_params *params)
{
	if (attr == NULL) {
		/* Discovery complete */
		if (msg_char_handle != 0 && notify_params.value_handle != 0) {
			notify_params.notify = notify_cb;
			notify_params.value = BT_GATT_CCC_NOTIFY;
			int err = bt_gatt_subscribe(conn, &notify_params);
			if (err) {
				LOG_ERR("Subscribe failed: %d", err);
			} else {
				LOG_INF("Subscribed to response notifications");
				k_sem_give(&ble_ready_sem);
			}
		} else {
			LOG_ERR("FIRMUPS characteristics not fully discovered");
		}
		return BT_GATT_ITER_STOP;
	}

	const struct bt_gatt_chrc *chrc = attr->user_data;

	if (bt_uuid_cmp(chrc->uuid, &uuid_msg.uuid) == 0) {
		msg_char_handle = chrc->value_handle;
		LOG_INF("Found message write char, handle 0x%04x", msg_char_handle);
	} else if (bt_uuid_cmp(chrc->uuid, &uuid_resp.uuid) == 0) {
		notify_params.value_handle = chrc->value_handle;
		notify_params.ccc_handle = chrc->value_handle + 1;
		LOG_INF("Found response notify char, handle 0x%04x", notify_params.value_handle);
	}

	return BT_GATT_ITER_CONTINUE;
}

static void start_discovery(struct bt_conn *conn)
{
	disc_params.uuid = NULL;
	disc_params.func = discover_cb;
	disc_params.start_handle = BT_ATT_FIRST_ATTRIBUTE_HANDLE;
	disc_params.end_handle = BT_ATT_LAST_ATTRIBUTE_HANDLE;
	disc_params.type = BT_GATT_DISCOVER_CHARACTERISTIC;

	int err = bt_gatt_discover(conn, &disc_params);
	if (err) {
		LOG_ERR("GATT discovery failed: %d", err);
	}
}

/* ===================== BLE connection callbacks ===================== */
static void mtu_exchange_cb(struct bt_conn *conn, uint8_t err,
			    struct bt_gatt_exchange_params *params)
{
	if (err) {
		LOG_WRN("MTU exchange failed: %d, proceeding with discovery", err);
	} else {
		LOG_INF("MTU exchanged: %u bytes", bt_gatt_get_mtu(conn));
	}
	start_discovery(conn);
}

static void connected_cb(struct bt_conn *conn, uint8_t err)
{
	if (err) {
		LOG_ERR("BLE connection failed: %d", err);
		bt_conn_unref(conn);
		ble_conn = NULL;
		return;
	}
	LOG_INF("Connected to gateway");
	ble_conn = bt_conn_ref(conn);

	mtu_params.func = mtu_exchange_cb;
	int mtu_err = bt_gatt_exchange_mtu(conn, &mtu_params);
	if (mtu_err) {
		LOG_WRN("bt_gatt_exchange_mtu failed: %d, proceeding", mtu_err);
		start_discovery(conn);
	}
}

static void disconnected_cb(struct bt_conn *conn, uint8_t reason)
{
	LOG_WRN("Disconnected from gateway (reason 0x%02x)", reason);
	bt_conn_unref(ble_conn);
	ble_conn = NULL;
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = connected_cb,
	.disconnected = disconnected_cb,
};

/* ===================== BLE scan ===================== */
static bool adv_data_cb(struct bt_data *data, void *user_data)
{
	bool *found = user_data;

	if (data->type != BT_DATA_UUID128_ALL && data->type != BT_DATA_UUID128_SOME) {
		return true; /* continue parsing */
	}

	/* Check each 16-byte UUID in the AD field */
	for (int i = 0; i + 16 <= data->data_len; i += 16) {
		struct bt_uuid_128 adv_uuid;

		bt_uuid_create(&adv_uuid.uuid, data->data + i, 16);
		if (bt_uuid_cmp(&adv_uuid.uuid, &uuid_svc.uuid) == 0) {
			*found = true;
			return false; /* stop parsing */
		}
	}
	return true;
}

static void scan_cb(const bt_addr_le_t *addr, int8_t rssi, uint8_t type, struct net_buf_simple *buf)
{
	if (ble_conn != NULL) {
		return; /* already connected */
	}

	bool found = false;

	bt_data_parse(buf, adv_data_cb, &found);
	if (!found) {
		return;
	}

	LOG_INF("Found FIRMUPS gateway, connecting...");
	bt_le_scan_stop();

	struct bt_conn_le_create_param *create_param = BT_CONN_LE_CREATE_CONN;
	struct bt_le_conn_param *conn_param = BT_LE_CONN_PARAM_DEFAULT;
	struct bt_conn *conn;

	int err = bt_conn_le_create(addr, create_param, conn_param, &conn);
	if (err) {
		LOG_ERR("bt_conn_le_create failed: %d", err);
	} else {
		bt_conn_unref(conn); /* ref taken in connected_cb */
	}
}

/* ===================== SDK transport callback ===================== */
static enum firmups_sdk_error_code ble_send_data(uint8_t *send_buffer, uint16_t send_buffer_size,
						 uint8_t *response_buffer,
						 uint16_t response_buffer_size,
						 uint16_t *response_size, void *userdata)
{
	ARG_UNUSED(userdata);

	if (ble_conn == NULL) {
		LOG_ERR("BLE not connected");
		return FIRMUPS_SDK_ERROR_COMMUNICATION_FAILURE;
	}

	g_response_buf = response_buffer;
	g_response_size = response_size;
	g_response_capacity = response_buffer_size;

	int err = bt_gatt_write_without_response(ble_conn, msg_char_handle, send_buffer,
						 send_buffer_size, false);
	if (err) {
		LOG_ERR("GATT write failed: %d", err);
		return FIRMUPS_SDK_ERROR_COMMUNICATION_FAILURE;
	}

	if (k_sem_take(&response_sem, K_MSEC(5000)) != 0) {
		LOG_ERR("BLE response timeout");
		return FIRMUPS_SDK_ERROR_TIMEOUT;
	}

	return FIRMUPS_SDK_ERROR_NONE;
}

/* ===================== BLE scan + connect ===================== */
static bool scan_and_connect(void)
{
	if (ble_conn != NULL) {
		return true;
	}

	k_sem_reset(&ble_ready_sem);

	struct bt_le_scan_param scan_param = {
		.type = BT_LE_SCAN_TYPE_ACTIVE,
		.options = BT_LE_SCAN_OPT_NONE,
		.interval = BT_GAP_SCAN_FAST_INTERVAL,
		.window = BT_GAP_SCAN_FAST_WINDOW,
	};
	int err = bt_le_scan_start(&scan_param, scan_cb);
	if (err) {
		LOG_ERR("Scan start failed: %d", err);
		return false;
	}

	LOG_INF("Scanning for FIRMUPS gateway...");
	if (k_sem_take(&ble_ready_sem, K_SECONDS(5)) != 0) {
		LOG_WRN("No gateway found within 5s");
		bt_le_scan_stop();
		return false;
	}

	LOG_INF("BLE gateway ready");
	return true;
}

/* ===================== Public Functions ===================== */
bool firmware_updater_connect(struct firmware_updater_context *context)
{
	ARG_UNUSED(context);
	return scan_and_connect();
}

void firmware_updater_disconnect(struct firmware_updater_context *context)
{
	ARG_UNUSED(context);
	if (ble_conn != NULL) {
		bt_conn_disconnect(ble_conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
	}
}

struct firmware_updater_context *firmware_updater_initialize(uint8_t *work_buffer,
							     uint32_t work_buffer_size,
							     uint32_t device_id,
							     uint32_t firmware_version)
{
	if (work_buffer_size < sizeof(struct firmware_updater_context) || work_buffer == NULL) {
		return NULL;
	}

	int err = bt_enable(NULL);
	if (err) {
		LOG_ERR("bt_enable failed: %d", err);
		return NULL;
	}

	/* SDK init */
	struct firmware_updater_context *context = (struct firmware_updater_context *)work_buffer;

	context->firmware_version = firmware_version;
	context->desired_firmware_version = 0;
	work_buffer = work_buffer + sizeof(struct firmware_updater_context);
	work_buffer_size -= sizeof(struct firmware_updater_context);

	struct firmups_sdk_api sdk_api = {
		.get_random_bytes = random_bytes,
		.random_bytes_userdata = NULL,
		.get_key = get_key,
		.get_key_userdata = NULL,
		.send_data = ble_send_data,
		.send_data_userdata = NULL,
		.receive_data = NULL,
		.receive_data_userdata = NULL,
	};
	context->firmups_context =
		firmups_sdk_initialize(work_buffer, work_buffer_size, &sdk_api, device_id);
	if (context->firmups_context == NULL) {
		return NULL;
	}

	LOG_INF("Firmware updater initialized. Device ID: %d, Firmware Version: %d", device_id,
		firmware_version);

	if (!boot_is_img_confirmed()) {
		LOG_INF("Confirming current firmware image...");
		int rc = boot_write_img_confirmed();
		if (rc != 0) {
			LOG_ERR("Failed to confirm image: %d", rc);
			k_sleep(K_MSEC(500));
			sys_reboot(SYS_REBOOT_COLD);
			k_sleep(K_FOREVER);
		}
		LOG_INF("Firmware update applied successfully. Informing backend...");
		if (scan_and_connect()) {
			struct firmups_sdk_device_info_update fw_info;
			fw_info.firmware = context->firmware_version;
			fw_info.status = 0;
			enum firmups_sdk_error_code error =
				firmups_sdk_set_device_info(context->firmups_context, &fw_info);
			if (error != FIRMUPS_SDK_ERROR_NONE) {
				LOG_ERR("Device info could not be updated");
			}
		} else {
			LOG_ERR("Could not connect to gateway to report firmware status");
		}
	}

	return context;
}

bool firmware_update_available(struct firmware_updater_context *context)
{
	if (context == NULL) {
		return false;
	}

	if (context->firmware_version != context->desired_firmware_version &&
	    context->desired_firmware_version != 0) {
		return true;
	}

	LOG_INF("Fetching device info");
	struct firmups_sdk_device_info fw_info;
	enum firmups_sdk_error_code error =
		firmups_sdk_get_device_info(context->firmups_context, &fw_info);
	if (error != FIRMUPS_SDK_ERROR_NONE) {
		LOG_ERR("Device info could not be fetched");
		return false;
	}

	context->desired_firmware_version = fw_info.desired_firmware;
	if (context->firmware_version < context->desired_firmware_version) {
		LOG_INF("Firmware update required. Current: %d, Desired: %d",
			context->firmware_version, context->desired_firmware_version);
		return true;
	}

	LOG_INF("Firmware is up to date. Version: %d", fw_info.firmware);
	return false;
}

uint32_t firmware_updater_get_firmware_version(struct firmware_updater_context *context)
{
	if (context == NULL) {
		return 0;
	}
	return context->firmware_version;
}

int firmware_updater_update_firmware(struct firmware_updater_context *context)
{
	if (context == NULL) {
		return -1;
	}
	LOG_INF("Starting firmware update process...");
	if (context->firmware_version >= context->desired_firmware_version) {
		LOG_INF("No firmware update required.");
		return 0;
	}

	/* Erase slot1 before streaming into it */
	uint8_t area_id = PARTITION_ID(slot1_partition);
	const struct flash_area *fa;
	int rc = flash_area_open(area_id, &fa);
	if (rc) {
		LOG_ERR("flash_area_open(%u) failed: %d", area_id, rc);
		return rc;
	}
	uint32_t flash_slot_size = fa->fa_size;
	rc = flash_area_erase(fa, 0, flash_slot_size);
	flash_area_close(fa);
	if (rc) {
		LOG_ERR("flash_area_erase failed: %d", rc);
		return rc;
	}

	struct flash_img_context img_ctx;
	rc = flash_img_init_id(&img_ctx, area_id);
	if (rc) {
		LOG_ERR("flash_img_init_id(%u) failed: %d", area_id, rc);
		return rc;
	}

	LOG_INF("Starting firmware download directly to slot1...");
	uint8_t download_buffer[1000];
	enum firmups_sdk_error_code error;
	error = firmups_sdk_firmware_download_initialize(context->firmups_context,
							 context->desired_firmware_version,
							 download_buffer, sizeof(download_buffer));
	if (error != FIRMUPS_SDK_ERROR_NONE) {
		LOG_ERR("Firmware download context could not be initialized");
		return -1;
	}

	bool download_complete = false;
	const uint8_t *chunk_pointer;
	uint32_t bytes_downloaded = 0;
	uint8_t last_reported_pct = 0;

	while (!download_complete) {
		uint16_t chunk_size = 0;
		error = firmups_sdk_firmware_download_get_chunk(
			context->firmups_context, &chunk_pointer, &chunk_size, &download_complete);
		if (error != FIRMUPS_SDK_ERROR_NONE) {
			LOG_ERR("Firmware chunk could not be downloaded");
			return -1;
		}
		LOG_DBG("Downloaded chunk of size: %d", chunk_size);

		if (chunk_size > 0) {
			rc = flash_img_buffered_write(&img_ctx, chunk_pointer, chunk_size, false);
			if (rc) {
				LOG_ERR("flash_img_buffered_write failed: %d", rc);
				return rc;
			}
			bytes_downloaded += chunk_size;
			uint8_t pct = (uint8_t)((bytes_downloaded * 100UL) / flash_slot_size);
			if (pct >= last_reported_pct + 5) {
				last_reported_pct = (pct / 5) * 5;
				LOG_INF("Firmware download: %u%% (%u bytes)", last_reported_pct,
					bytes_downloaded);
			}
		}
	}

	rc = flash_img_buffered_write(&img_ctx, NULL, 0, true);
	if (rc) {
		LOG_ERR("flash_img flush failed: %d", rc);
		return rc;
	}
	LOG_INF("Firmware download to slot1 complete");

	error = firmups_sdk_firmware_download_finish(context->firmups_context);
	if (error != FIRMUPS_SDK_ERROR_NONE) {
		LOG_ERR("Firmware download could not be finalized");
		return -1;
	}

	boot_request_upgrade(BOOT_UPGRADE_TEST);
	LOG_INF("Firmware update applied. Rebooting system...");
	k_sleep(K_MSEC(500));
	sys_reboot(SYS_REBOOT_COLD);
	k_sleep(K_FOREVER);
}

/* ===================== SDK required log functions ===================== */
int firmups_sdk_log_info(const char *file, int line, const char *format, ...)
{
	va_list args;
	va_start(args, format);
	uint8_t buffer[256];
	int ret = vsnprintf(buffer, sizeof(buffer), format, args);
	va_end(args);
	LOG_INF("INFO: %s:%d: %s", file, line, buffer);
	return ret;
}

int firmups_sdk_log_warning(const char *file, int line, const char *format, ...)
{
	va_list args;
	va_start(args, format);
	uint8_t buffer[256];
	int ret = vsnprintf(buffer, sizeof(buffer), format, args);
	va_end(args);
	LOG_WRN("WARNING: %s:%d: %s", file, line, buffer);
	return ret;
}

int firmups_sdk_log_error(const char *file, int line, const char *format, ...)
{
	va_list args;
	va_start(args, format);
	uint8_t buffer[256];
	int ret = vsnprintf(buffer, sizeof(buffer), format, args);
	va_end(args);
	LOG_ERR("ERROR: %s:%d: %s", file, line, buffer);
	return ret;
}

/* ===================== Private Functions ===================== */
static enum firmups_sdk_error_code random_bytes(uint8_t *buffer, uint16_t size, void *userdata)
{
	(void)userdata;
	int ret = sys_csrand_get(buffer, size);
	if (ret != 0) {
		LOG_ERR("Could not get random bytes.");
		return FIRMUPS_SDK_ERROR_CRYPTOGRAPHIC_FAILURE;
	}
	return FIRMUPS_SDK_ERROR_NONE;
}

static enum firmups_sdk_error_code get_key(uint8_t *key_buffer, uint16_t key_buffer_size,
					   void *userdata)
{
	(void)userdata;
	const uint8_t hardcoded_key[16] = {0x48, 0x65, 0x6c, 0x6c, 0x6f, 0x57, 0x6f, 0x72,
					   0x6c, 0x64, 0x35, 0x48, 0x75, 0x95, 0x63, 0x24};
	if (key_buffer_size > sizeof(hardcoded_key)) {
		return FIRMUPS_SDK_ERROR_BUFFER_TOO_SMALL;
	}
	memcpy(key_buffer, hardcoded_key, key_buffer_size);
	return FIRMUPS_SDK_ERROR_NONE;
}
