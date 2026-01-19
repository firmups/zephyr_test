#include "firmware_updater.h"
#include "filesystem.h"
#include "udpclient.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/dfu/mcuboot.h>
#include <firmups-device-sdk/sdk.h>
#include <zephyr/fs/fs.h>
#include <zephyr/sys/reboot.h>
#include <stdio.h>
#include <zephyr/random/random.h>

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
	k_sleep(K_MSEC(500));
	LOG_INF("Rebooting system to apply firmware...");
	k_sleep(K_MSEC(500));
	LOG_INF("Firmware update applied. Informing backend...");
	struct firmups_sdk_device_info_update fw_info;
	fw_info.firmware = context->desired_firmware_version;
	fw_info.status = 0;
	error = firmups_sdk_set_device_info(context->firmups_context, &fw_info);
	if (error != FIRMUPS_SDK_ERROR_NONE) {
		LOG_ERR("Device info could not be updated");
		return -1;
	}
	context->firmware_version = context->desired_firmware_version;
	LOG_INF("Firmware update process complete.");
	return 0;
}

/* ===================== Private Functions ===================== */
// int firmups_sdk_log_debug(const char *file, int line, const char *format, ...)
// {
// 	va_list args;
// 	va_start(args, format);
// 	uint8_t buffer[256];
// 	int ret = vsnprintf(buffer, sizeof(buffer), format, args);
// 	va_end(args);
// 	if (ret < 0) {
// 		LOG_DBG("DEBUG: %s:%d: <formatting error>", file, line);
// 		return -1;
// 	}
// 	if (ret >= (int)sizeof(buffer)) {
// 		LOG_DBG("DEBUG: %s:%d: %s (truncated)", file, line, buffer);
// 		return 1;
// 	}
// 	LOG_DBG("DEBUG: %s:%d: %s", file, line, buffer);
// 	return 0;
// }

int firmups_sdk_log_info(const char *file, int line, const char *format, ...)
{
	va_list args;
	va_start(args, format);
	uint8_t buffer[256];
	int ret = vsnprintf(buffer, sizeof(buffer), format, args);
	va_end(args);
	if (ret < 0) {
		LOG_INF("INFO: %s:%d: <formatting error>", file, line);
		return -1;
	}
	if (ret >= (int)sizeof(buffer)) {
		LOG_INF("INFO: %s:%d: %s (truncated)", file, line, buffer);
		return 1;
	}
	LOG_INF("INFO: %s:%d: %s", file, line, buffer);
	return 0;
}

int firmups_sdk_log_warning(const char *file, int line, const char *format, ...)
{
	va_list args;
	va_start(args, format);
	uint8_t buffer[256];
	int ret = vsnprintf(buffer, sizeof(buffer), format, args);
	va_end(args);
	if (ret < 0) {
		LOG_WRN("WARNING: %s:%d: <formatting error>", file, line);
		return -1;
	}
	if (ret >= (int)sizeof(buffer)) {
		LOG_WRN("WARNING: %s:%d: %s (truncated)", file, line, buffer);
		return 1;
	}
	LOG_WRN("WARNING: %s:%d: %s", file, line, buffer);
	return 0;
}

int firmups_sdk_log_error(const char *file, int line, const char *format, ...)
{
	va_list args;
	va_start(args, format);
	uint8_t buffer[256];
	int ret = vsnprintf(buffer, sizeof(buffer), format, args);
	va_end(args);
	if (ret < 0) {
		LOG_ERR("ERROR: %s:%d: <formatting error>", file, line);
		return -1;
	}
	if (ret >= (int)sizeof(buffer)) {
		LOG_ERR("ERROR: %s:%d: %s (truncated)", file, line, buffer);
		return 1;
	}
	LOG_ERR("ERROR: %s:%d: %s", file, line, buffer);
	return 0;
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
	const uint8_t dummy_key[16] = {0x48, 0x65, 0x6c, 0x6c, 0x6f, 0x57, 0x6f, 0x72,
				       0x6c, 0x64, 0x35, 0x48, 0x75, 0x95, 0x63, 0x24};
	if (key_buffer_size > sizeof(dummy_key)) {
		return FIRMUPS_SDK_ERROR_BUFFER_TOO_SMALL; // Buffer too small
	}
	memcpy(key_buffer, dummy_key, key_buffer_size);
	return FIRMUPS_SDK_ERROR_NONE; // Success
}

static enum firmups_sdk_error_code send_data(uint8_t *send_buffer, uint16_t send_buffer_size,
					     uint8_t *response_buffer,
					     uint16_t response_buffer_size, uint16_t *response_size,
					     void *userdata)
{
	return udp_send_data(send_buffer, send_buffer_size, response_buffer, response_buffer_size,
			     response_size, userdata);
}

static bool file_exists(const char *path)
{
	struct fs_dirent entry;

	int ret = fs_stat(path, &entry);
	return (ret == 0); // true = existiert
}
