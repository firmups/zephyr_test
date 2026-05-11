/*
 * Auto-connect to WiFi at APPLICATION level, before net_config_init (priority 95)
 * waits for an IPv4 address via DHCPv4.
 */

#include <zephyr/kernel.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/net/wifi.h>

#define WIFI_SSID CONFIG_APP_WIFI_SSID
#define WIFI_PSK  CONFIG_APP_WIFI_PSK

static int wifi_connect_init(void)
{
	struct net_if *iface = net_if_get_first_wifi();

	if (!iface) {
		return -ENODEV;
	}

	struct wifi_connect_req_params params = {
		.ssid = (const uint8_t *)WIFI_SSID,
		.ssid_length = sizeof(WIFI_SSID) - 1,
		.psk = (const uint8_t *)WIFI_PSK,
		.psk_length = sizeof(WIFI_PSK) - 1,
		.channel = WIFI_CHANNEL_ANY,
		.security = WIFI_SECURITY_TYPE_PSK,
		.mfp = WIFI_MFP_OPTIONAL,
	};

	return net_mgmt(NET_REQUEST_WIFI_CONNECT, iface, &params, sizeof(params));
}

SYS_INIT(wifi_connect_init, APPLICATION, 90);
