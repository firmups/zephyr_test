#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include "led.h"

LOG_MODULE_REGISTER(led_driver, LOG_LEVEL_INF);
ATOMIC_DEFINE(led_bits, LED_END);

/* Get GPIO specs from the DT "gpios" property of each node */
#define LED_SPEC(node_label)  GPIO_DT_SPEC_GET(DT_NODELABEL(node_label), gpios)
#define BLINK_STACK_SIZE      512
#define BLINK_THREAD_PRIORITY 5

K_THREAD_STACK_DEFINE(blink_stack, BLINK_STACK_SIZE);
static struct k_thread blink_thread;
static const char blink_thread_name[] = "led_blink";

/* Your node labels from the DTS */
static const struct gpio_dt_spec led_green = LED_SPEC(green_led_0);
static const struct gpio_dt_spec led_orange = LED_SPEC(orange_led_0);
static const struct gpio_dt_spec led_red = LED_SPEC(red_led_0);
static const struct gpio_dt_spec led_blue = LED_SPEC(blue_led_0);

/* Helper to init one LED */
static int single_led_init(const struct gpio_dt_spec *led)
{
	if (!device_is_ready(led->port)) {
		LOG_ERR("GPIO controller %s not ready", led->port->name);
		return -ENODEV;
	}
	/* Configure as output. The flags in DT already include ACTIVE_LOW,
	 * so we just set initial level to INACTIVE (= LED off).
	 */
	return gpio_pin_configure_dt(led, GPIO_OUTPUT_INACTIVE);
}

/* Toggle that respects ACTIVE_LOW/HIGH automatically */
static int led_toggle_dt(const struct gpio_dt_spec *led)
{
	return gpio_pin_toggle_dt(led);
}

static void thread_entry(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	LOG_INF("LED blink thread started");

	while (true) {
		if (atomic_test_bit(led_bits, LED_GREEN)) {
			(void)led_toggle_dt(&led_green);
		} else {
			gpio_pin_set_dt(&led_green, 0);
		}
		if (atomic_test_bit(led_bits, LED_ORANGE)) {
			(void)led_toggle_dt(&led_orange);
		} else {
			gpio_pin_set_dt(&led_orange, 0);
		}
		if (atomic_test_bit(led_bits, LED_RED)) {
			(void)led_toggle_dt(&led_red);
		} else {
			gpio_pin_set_dt(&led_red, 0);
		}
		if (atomic_test_bit(led_bits, LED_BLUE)) {
			(void)led_toggle_dt(&led_blue);
		} else {
			gpio_pin_set_dt(&led_blue, 0);
		}
		k_msleep(500);
	}
}

int led_initialize(void)
{
	int ret;
	LOG_INF("LED driver start");
	/* Initialize only the red LED (others are available if needed) */
	ret = single_led_init(&led_green);
	if (ret < 0) {
		LOG_ERR("Failed to init green LED (err %d). Halting.", ret);
		return ret;
	}
	ret = single_led_init(&led_orange);
	if (ret < 0) {
		LOG_ERR("Failed to init orange LED (err %d). Halting.", ret);
		return ret;
	}
	ret = single_led_init(&led_red);
	if (ret < 0) {
		LOG_ERR("Failed to init red LED (err %d). Halting.", ret);
		return ret;
	}
	ret = single_led_init(&led_blue);
	if (ret < 0) {
		LOG_ERR("Failed to init blue LED (err %d). Halting.", ret);
		return ret;
	}

	ret = gpio_pin_set_dt(&led_green, 0);
	if (ret < 0) {
		LOG_WRN("Couldn't set initial green LED state (err %d)", ret);
		return ret;
	}
	ret = gpio_pin_set_dt(&led_orange, 0);
	if (ret < 0) {
		LOG_WRN("Couldn't set initial orange LED state (err %d)", ret);
		return ret;
	}
	ret = gpio_pin_set_dt(&led_red, 0);
	if (ret < 0) {
		LOG_WRN("Couldn't set initial red LED state (err %d)", ret);
		return ret;
	}
	ret = gpio_pin_set_dt(&led_blue, 0);
	if (ret < 0) {
		LOG_WRN("Couldn't set initial blue LED state (err %d)", ret);
		return ret;
	}

	k_tid_t tid = k_thread_create(
		&blink_thread, blink_stack, K_THREAD_STACK_SIZEOF(blink_stack), thread_entry, NULL,
		NULL, NULL, BLINK_THREAD_PRIORITY, 0, /* No delay, start immediately */
		K_NO_WAIT);

#if defined(CONFIG_THREAD_NAME)
	k_thread_name_set(tid, blink_thread_name);
#endif

	LOG_INF("Red blink thread created");
	return 0;
}

void led_blink_start(enum led_type led)
{
	switch (led) {
	case LED_GREEN:
		LOG_INF("Blink green start");
		atomic_set_bit(led_bits, LED_GREEN);
		break;
	case LED_ORANGE:
		LOG_INF("Blink orange start");
		atomic_set_bit(led_bits, LED_ORANGE);
		break;
	case LED_RED:
		LOG_INF("Blink red start");
		atomic_set_bit(led_bits, LED_RED);
		break;
	case LED_BLUE:
		LOG_INF("Blink blue start");
		atomic_set_bit(led_bits, LED_BLUE);
		break;
	case LED_END:
		break;
	}
}

void led_blink_end(enum led_type led)
{
	switch (led) {
	case LED_GREEN:
		LOG_INF("Blink green stop");
		atomic_clear_bit(led_bits, LED_GREEN);
		break;
	case LED_ORANGE:
		LOG_INF("Blink orange stop");
		atomic_clear_bit(led_bits, LED_ORANGE);
		break;
	case LED_RED:
		LOG_INF("Blink red stop");
		atomic_clear_bit(led_bits, LED_RED);
		break;
	case LED_BLUE:
		LOG_INF("Blink blue stop");
		atomic_clear_bit(led_bits, LED_BLUE);
		break;
	case LED_END:
		break;
	}
}
