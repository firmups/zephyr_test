#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include "button.h"

LOG_MODULE_REGISTER(button_driver, LOG_LEVEL_INF);

#define DEBOUNCE_MS 25

/* nRF5340-DK button 1 (sw0 alias) */
static const struct gpio_dt_spec user_button = GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);
static struct gpio_callback btn_cb_data;

static void button_debounced_work_handler(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(button_debounced_work, button_debounced_work_handler);

void (*button_pressed_callback)(void) = NULL;

/* Dedicated thread for the button callback so the system work queue is never
 * blocked by long-running operations (e.g. BLE + UDP round-trips). */
static K_SEM_DEFINE(button_action_sem, 0, 1);

static void button_action_thread_fn(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	while (true) {
		k_sem_take(&button_action_sem, K_FOREVER);
		if (button_pressed_callback != NULL) {
			button_pressed_callback();
		}
	}
}

/* Stack large enough for BLE + SDK + firmware download operations.
 * Priority 10 keeps this below bt_rx (default 8) so the BLE stack can
 * always preempt and deliver notifications during a blocking wait. */
K_THREAD_DEFINE(button_action_thread, 4096, button_action_thread_fn,
		NULL, NULL, NULL, 10, 0, 0);

static void button_isr(const struct device *port, struct gpio_callback *cb, uint32_t pins)
{
	ARG_UNUSED(port);
	ARG_UNUSED(cb);

	if (pins & BIT(user_button.pin)) {
		(void)k_work_reschedule(&button_debounced_work, K_MSEC(DEBOUNCE_MS));
	}
}

static void button_debounced_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	int val = gpio_pin_get_dt(&user_button);
	if (val < 0) {
		LOG_WRN("Failed to read button state (%d)", val);
		return;
	}

	bool is_active = (user_button.dt_flags & GPIO_ACTIVE_LOW) ? (val == 0) : (val != 0);
	if (is_active) {
		LOG_INF("User button pressed");
		k_sem_give(&button_action_sem);
	}
}

void button_init(void (*callback)(void))
{
	button_pressed_callback = callback;

	if (!device_is_ready(user_button.port)) {
		LOG_ERR("Button GPIO controller %s not ready", user_button.port->name);
		return;
	}

	int ret = gpio_pin_configure_dt(&user_button, GPIO_INPUT);
	if (ret < 0) {
		LOG_ERR("Failed to configure button pin (err %d)", ret);
		return;
	}

	ret = gpio_pin_interrupt_configure_dt(&user_button, GPIO_INT_EDGE_TO_INACTIVE);
	if (ret < 0) {
		LOG_ERR("Failed to configure button interrupt (err %d)", ret);
		return;
	}

	gpio_init_callback(&btn_cb_data, button_isr, BIT(user_button.pin));
	ret = gpio_add_callback(user_button.port, &btn_cb_data);
	if (ret < 0) {
		LOG_ERR("Failed to add button callback (err %d)", ret);
		return;
	}

	LOG_INF("Button interrupt armed on pin %u (active %s)", user_button.pin,
		(user_button.dt_flags & GPIO_ACTIVE_LOW) ? "LOW" : "HIGH");
}
