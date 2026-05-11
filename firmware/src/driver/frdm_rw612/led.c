#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include "led.h"

LOG_MODULE_REGISTER(led_driver, LOG_LEVEL_INF);
ATOMIC_DEFINE(led_bits, LED_END);

#define BLINK_STACK_SIZE      512
#define BLINK_THREAD_PRIORITY 5

K_THREAD_STACK_DEFINE(blink_stack, BLINK_STACK_SIZE);
static struct k_thread blink_thread;
static const char blink_thread_name[] = "led_blink";

/* The FRDM-RW612 board has an RGB LED (all active-low) */
static const struct gpio_dt_spec led_green = GPIO_DT_SPEC_GET(DT_NODELABEL(green_led), gpios);
static const struct gpio_dt_spec led_red = GPIO_DT_SPEC_GET(DT_NODELABEL(red_led), gpios);
static const struct gpio_dt_spec led_blue = GPIO_DT_SPEC_GET(DT_NODELABEL(blue_led), gpios);

static int single_led_init(const struct gpio_dt_spec *led)
{
	if (!device_is_ready(led->port)) {
		LOG_ERR("GPIO controller %s not ready", led->port->name);
		return -ENODEV;
	}
	return gpio_pin_configure_dt(led, GPIO_OUTPUT_INACTIVE);
}

static void thread_entry(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	LOG_INF("LED blink thread started");

	while (true) {
		bool orange = atomic_test_bit(led_bits, LED_ORANGE);

		if (atomic_test_bit(led_bits, LED_GREEN) || orange) {
			(void)gpio_pin_toggle_dt(&led_green);
		} else {
			gpio_pin_set_dt(&led_green, 0);
		}
		if (atomic_test_bit(led_bits, LED_RED) || orange) {
			(void)gpio_pin_toggle_dt(&led_red);
		} else {
			gpio_pin_set_dt(&led_red, 0);
		}
		if (atomic_test_bit(led_bits, LED_BLUE)) {
			(void)gpio_pin_toggle_dt(&led_blue);
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

	ret = single_led_init(&led_green);
	if (ret < 0) {
		LOG_ERR("Failed to init green LED (err %d)", ret);
		return ret;
	}
	ret = single_led_init(&led_red);
	if (ret < 0) {
		LOG_ERR("Failed to init red LED (err %d)", ret);
		return ret;
	}
	ret = single_led_init(&led_blue);
	if (ret < 0) {
		LOG_ERR("Failed to init blue LED (err %d)", ret);
		return ret;
	}

	k_tid_t tid = k_thread_create(&blink_thread, blink_stack,
				      K_THREAD_STACK_SIZEOF(blink_stack), thread_entry, NULL, NULL,
				      NULL, BLINK_THREAD_PRIORITY, 0, K_NO_WAIT);

#if defined(CONFIG_THREAD_NAME)
	k_thread_name_set(tid, blink_thread_name);
#endif

	LOG_INF("LED blink thread created");
	return 0;
}

void led_blink_start(enum led_type led)
{
	LOG_INF("Blink start (led %d)", led);
	atomic_set_bit(led_bits, led);
}

void led_blink_end(enum led_type led)
{
	LOG_INF("Blink end (led %d)", led);
	atomic_clear_bit(led_bits, led);
}
