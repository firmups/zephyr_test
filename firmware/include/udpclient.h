#ifndef UDPCLIENT_H
#define UDPCLIENT_H

#include <stdint.h>
#include <firmups-device-sdk/error.h>

enum firmups_sdk_error_code udp_send_data(uint8_t *send_buffer, uint16_t send_buffer_size,
					  uint8_t *response_buffer, uint16_t response_buffer_size,
					  uint16_t *response_size, void *userdata);

#endif // UDPCLIENT_H
