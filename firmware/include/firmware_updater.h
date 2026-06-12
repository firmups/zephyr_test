#ifndef FIRMWARE_UPDATER_H
#define FIRMWARE_UPDATER_H

#include <stdint.h>
#include <stdbool.h>

struct firmware_updater_context;

struct firmware_updater_context *firmware_updater_initialize(uint8_t *work_buffer,
							     uint32_t work_buffer_size,
							     uint32_t device_id,
							     uint32_t firmware_version);
bool firmware_update_available(struct firmware_updater_context *context);
uint32_t firmware_updater_get_firmware_version(struct firmware_updater_context *context);
int firmware_updater_update_firmware(struct firmware_updater_context *context);

#ifdef NRF5340DK
bool firmware_updater_connect(struct firmware_updater_context *context);
void firmware_updater_disconnect(struct firmware_updater_context *context);
#endif

#endif // FIRMWARE_UPDATER_H
