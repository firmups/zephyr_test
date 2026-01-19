#include "firmware_updater.h"
#include "filesystem.h"
#include "udpclient.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/dfu/mcuboot.h>
#include <firmups-device-sdk/sdk.h>
#include <zephyr/fs/fs.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/dfu/flash_img.h>
#include <zephyr/random/random.h>

#include <stdio.h>

/* ===================== Types/Defines ===================== */
LOG_MODULE_REGISTER(firmware_updater, CONFIG_FIRMUPS_LOG_LEVEL);
#define UPDATE_PATH FILESYSTEM_MOUNT_POINT "/update.bin"

struct firmware_updater_context {
	uint32_t firmware_version;
	uint32_t desired_firmware_version;
	struct firmups_sdk_context *firmups_context;
};

/* ===================== Predeclarations ===================== */
static enum firmups_sdk_error_code random_bytes(uint8_t *buffer, uint16_t size, void *userdata);
static enum firmups_sdk_error_code get_key(uint8_t *key_buffer, uint16_t key_buffer_size,
					   void *userdata);
static enum firmups_sdk_error_code send_data(uint8_t *send_buffer, uint16_t send_buffer_size,
					     uint8_t *response_buffer,
					     uint16_t response_buffer_size, uint16_t *response_size,
					     void *userdata);
static int write_firmware_to_secondary_slot(char const *path);
static bool file_exists(const char *path);

/* ===================== Public Functions ===================== */
struct firmware_updater_context *firmware_updater_initialize(uint8_t *work_buffer,
							     uint32_t work_buffer_size,
							     uint32_t device_id,
							     uint32_t firmware_version)
{
	if (work_buffer_size < sizeof(struct firmware_updater_context) || work_buffer == NULL) {
		return NULL;
	}

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
		.send_data = send_data,
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
		struct firmups_sdk_device_info_update fw_info;
		fw_info.firmware = context->firmware_version;
		fw_info.status = 0;
		enum firmups_sdk_error_code error =
			firmups_sdk_set_device_info(context->firmups_context, &fw_info);
		if (error != FIRMUPS_SDK_ERROR_NONE) {
			LOG_ERR("Device info could not be updated");
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
	} else {
		LOG_INF("Firmware is up to date. Version: %d", fw_info.firmware);
		return false;
	}
}

uint32_t firmware_updater_get_firmware_version(struct firmware_updater_context *context)
{
	if (context == NULL) {
		return 0;
	} else {
		return context->firmware_version;
	}
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

	LOG_INF("Starting firmware download....");
	uint8_t download_buffer[1500];
	enum firmups_sdk_error_code error;
	error = firmups_sdk_firmware_download_initialize(context->firmups_context,
							 context->desired_firmware_version,
							 download_buffer, sizeof(download_buffer));
	if (error != FIRMUPS_SDK_ERROR_NONE) {
		LOG_ERR("Firmware download context could not be initialized");
		return -1;
	}
	bool download_complete = false;
	if (file_exists(UPDATE_PATH)) {
		(void)fs_unlink(UPDATE_PATH);
	}
	uint8_t const *chunk_pointer;

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
			int rc = filesystem_append_bytes(UPDATE_PATH, chunk_pointer,
							 (size_t)chunk_size);
			if (rc < 0) {
				LOG_ERR("Append failed: %d", rc);
				return rc;
			}
		}
	}
	LOG_INF("Firmware download completed");
	error = firmups_sdk_firmware_download_finish(context->firmups_context);
	if (error != FIRMUPS_SDK_ERROR_NONE) {
		LOG_ERR("Firmware download could not be finalized");
		return -1;
	}

	LOG_INF("Writing firmware to slot1 partition...");
	int rc = write_firmware_to_secondary_slot(UPDATE_PATH);
	if (rc != 0) {
		LOG_ERR("Firmware could not be written to flash. Aborting...");
		return rc;
	}

	boot_request_upgrade(BOOT_UPGRADE_TEST);
	LOG_INF("Firmware update applied. Rebooting system...");
	k_sleep(K_MSEC(500));
	sys_reboot(SYS_REBOOT_COLD);
	k_sleep(K_FOREVER);
}

/* ===================== Private Functions ===================== */
// int firmups_sdk_log_debug(const char *file, int line, const char *format, ...)
// {
// 	va_list args;
// 	va_start(args, format);
// 	uint8_t buffer[256];
// 	int ret = sprintf(buffer, format, args);
// 	va_end(args);
// 	LOG_DBG("DEBUG: %s:%d: %s", file, line, buffer);
// 	return ret;
// }

int firmups_sdk_log_info(const char *file, int line, const char *format, ...)
{
	va_list args;
	va_start(args, format);
	uint8_t buffer[256];
	int ret = sprintf(buffer, format, args);
	va_end(args);
	LOG_INF("INFO: %s:%d: %s", file, line, buffer);
	return ret;
}

int firmups_sdk_log_warning(const char *file, int line, const char *format, ...)
{
	va_list args;
	va_start(args, format);
	uint8_t buffer[256];
	int ret = sprintf(buffer, format, args);
	va_end(args);
	LOG_WRN("WARNING: %s:%d: %s", file, line, buffer);
	return ret;
}

int firmups_sdk_log_error(const char *file, int line, const char *format, ...)
{
	va_list args;
	va_start(args, format);
	uint8_t buffer[256];
	int ret = sprintf(buffer, format, args);
	va_end(args);
	LOG_ERR("ERROR: %s:%d: %s", file, line, buffer);
	return ret;
}

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
		return FIRMUPS_SDK_ERROR_BUFFER_TOO_SMALL; // Buffer too small
	}
	memcpy(key_buffer, hardcoded_key, key_buffer_size);
	return FIRMUPS_SDK_ERROR_NONE; // Success
}

static enum firmups_sdk_error_code send_data(uint8_t *send_buffer, uint16_t send_buffer_size,
					     uint8_t *response_buffer,
					     uint16_t response_buffer_size, uint16_t *response_size,
					     void *userdata)
{
	enum firmups_sdk_error_code ret =
		udp_send_data(send_buffer, send_buffer_size, response_buffer, response_buffer_size,
			      response_size, userdata);
	return ret;
}

static int write_firmware_to_secondary_slot(char const *path)
{
	int rc;
	struct fs_file_t file;
	fs_file_t_init(&file);

	/* Open the image file (must be an MCUboot-signed image) */
	rc = fs_open(&file, path, FS_O_READ);
	if (rc < 0) {
		LOG_ERR("fs_open('%s') failed: %d", path, rc);
		return rc;
	}

	/* Get the flash area ID from the fixed partition label */
	uint8_t area_id = FIXED_PARTITION_ID(slot1_partition);
	const struct flash_area *fa;

	rc = flash_area_open(area_id, &fa);
	if (rc) {
		LOG_ERR("flash_area_open(%u) failed: %d", area_id, rc);
		fs_close(&file);
		return rc;
	}

	/* Erase the entire target partition (safe baseline) */
	rc = flash_area_erase(fa, 0, fa->fa_size);
	if (rc) {
		LOG_ERR("flash_area_erase size=0x%zx failed: %d", fa->fa_size, rc);
		flash_area_close(fa);
		fs_close(&file);
		return rc;
	}

	/* Initialize buffered writer tied to this partition */
	struct flash_img_context ctx;
	rc = flash_img_init_id(&ctx, area_id);
	if (rc) {
		LOG_ERR("flash_img_init_id(%u) failed: %d", area_id, rc);
		flash_area_close(fa);
		fs_close(&file);
		return rc;
	}

	/* Stream the file to flash in chunks */
	uint8_t buf[1024];
	ssize_t rd;
	size_t total = 0;

	do {
		rd = fs_read(&file, buf, sizeof(buf));
		if (rd < 0) {
			LOG_ERR("fs_read failed: %zd", rd);
			rc = (int)rd;
			break;
		}
		if (rd > 0) {
			/* flush=false for intermediate chunks */
			rc = flash_img_buffered_write(&ctx, buf, (size_t)rd, false);
			if (rc) {
				LOG_ERR("flash_img_buffered_write failed: %d", rc);
				break;
			}
			total += (size_t)rd;
		}
	} while (rd > 0);

	/* Final flush to commit remaining buffered bytes and pad to block boundary */
	if (rc == 0) {
		rc = flash_img_buffered_write(&ctx, NULL, 0, true); /* flush=true */
		if (rc) {
			LOG_ERR("final flush failed: %d", rc);
		}
	}

	flash_area_close(fa);
	fs_close(&file);

	if (rc == 0) {
		LOG_INF("Wrote %zu bytes to slot1_partition", total);
	}
	return rc;
}

static bool file_exists(const char *path)
{
	struct fs_dirent entry;

	int ret = fs_stat(path, &entry);
	return (ret == 0); // true = existiert
}
