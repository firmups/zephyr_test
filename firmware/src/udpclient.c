#include <zephyr/net/socket.h>
#include <zephyr/kernel.h>
#include <string.h>
#include <stdio.h>
#include <zephyr/net/net_ip.h>
#include "udpclient.h"
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(udp_client);

#define SERVER_ADDR "192.0.2.2" // Replace with your server IP
#define SERVER_PORT 53585

static int recv_with_timeout(int sock, void *buf, size_t len, int timeout_ms)
{
	struct zsock_pollfd pfd = {
		.fd = sock,
		.events = ZSOCK_POLLIN,
		.revents = 0,
	};

	int pr = zsock_poll(&pfd, 1, timeout_ms);
	if (pr == 0) {
		LOG_INF("Socket receive timeout");
		errno = EAGAIN; // timeout
		return -1;
	}
	if (pr < 0) {
		// errno set by poll
		return -1;
	}

	// Ready for read
	return zsock_recv(sock, buf, len, 0);
}

enum firmups_sdk_error_code udp_send_data(uint8_t *send_buffer, uint16_t send_buffer_size,
					  uint8_t *response_buffer, uint16_t response_buffer_size,
					  uint16_t *response_size, void *userdata)
{
	int sock;
	struct sockaddr_in server_addr;
	(void)userdata; // Unused parameter

	// Create UDP socket
	sock = zsock_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (sock < 0) {
		LOG_ERR("Failed to create socket\n");
		return FIRMUPS_SDK_ERROR_COMMUNICATION_FAILURE;
	}

	// Set up server address
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(SERVER_PORT);

	int ret = net_addr_pton(AF_INET, SERVER_ADDR, &server_addr.sin_addr);
	if (ret < 0) {
		LOG_ERR("Invalid IP address\n");
		zsock_close(sock);
		return -1;
	}

	// Send message to server
	int sent = zsock_sendto(sock, send_buffer, send_buffer_size, 0,
				(struct sockaddr *)&server_addr, sizeof(server_addr));
	if (sent < 0) {
		LOG_ERR("Failed to send data\n");
		zsock_close(sock);
		return FIRMUPS_SDK_ERROR_COMMUNICATION_FAILURE;
	}

	LOG_DBG("Sent %d bytes to server\n", sent);

	// Receive response
	int received = recv_with_timeout(sock, response_buffer, response_buffer_size, 5000);
	// int received = zsock_recv(sock, response_buffer, response_buffer_size, 0);
	if (received > 0) {
		LOG_DBG("Received %d bytes\n", received);
		*response_size = received;
	} else {
		LOG_ERR("No response or error receiving\n");
		zsock_close(sock);
		return FIRMUPS_SDK_ERROR_TIMEOUT;
	}

	zsock_close(sock);
	return FIRMUPS_SDK_ERROR_NONE;
}
