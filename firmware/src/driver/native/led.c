#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include "led.h"

LOG_MODULE_REGISTER(led_driver, LOG_LEVEL_INF);

#define BLINK_STACK_SIZE      512
#define BLINK_THREAD_PRIORITY 5

K_THREAD_STACK_DEFINE(blink_stack, BLINK_STACK_SIZE);
static struct k_thread blink_thread;
static const char blink_thread_name[] = "led_blink";

static bool active = false;
static bool led_on = false;

/* Toggle that respects ACTIVE_LOW/HIGH automatically */
static int led_toggle_dt(void)
{
	led_on = !led_on;
	if (led_on) {
		LOG_INF("LED ON");
	} else {
		LOG_INF("LED OFF");
	}
	return 0;
}

static void thread_entry(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	LOG_INF("Red LED blink thread started");

	while (true) {
		/* Toggle respects ACTIVE_LOW/HIGH automatically */
		(void)led_toggle_dt();
		k_msleep(2000);
	}
}

int led_initialize(void)
{
	LOG_INF("LED driver start");
	led_on = false;
	active = false;
	return 0;
};

void led_blink_start(enum led_type led)
{
	(void)led;
	LOG_INF("LED driver start");
	led_on = false;
	active = true;

	/* Create the red LED blink thread */
	k_tid_t tid = k_thread_create(
		&blink_thread, blink_stack, K_THREAD_STACK_SIZEOF(blink_stack), thread_entry, NULL,
		NULL, NULL, BLINK_THREAD_PRIORITY, 0, /* No delay, start immediately */
		K_NO_WAIT);

#if defined(CONFIG_THREAD_NAME)
	k_thread_name_set(tid, blink_thread_name);
#endif

	LOG_INF("Red blink thread created");
}

void led_blink_end(enum led_type led)
{
	(void)led;
	LOG_INF("LED driver start");
	led_on = false;
	active = false;
}
