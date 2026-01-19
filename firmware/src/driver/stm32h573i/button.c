#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include "button.h"

LOG_MODULE_REGISTER(button_driver, LOG_LEVEL_INF);

#define DEBOUNCE_MS             25
#define BUTTON_SPEC(node_label) GPIO_DT_SPEC_GET(DT_NODELABEL(node_label), gpios)

static const struct gpio_dt_spec user_button = BUTTON_SPEC(user_button);
static struct gpio_callback btn_cb_data;

static void button_debounced_work_handler(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(button_debounced_work, button_debounced_work_handler);

void (*button_pressed_callback)(void) = NULL;

static void button_isr(const struct device *port, struct gpio_callback *cb, uint32_t pins)
{
	ARG_UNUSED(port);
	ARG_UNUSED(cb);

	if (pins & BIT(user_button.pin)) {
		(void)k_work_reschedule(&button_debounced_work, K_MSEC(DEBOUNCE_MS));
	}
}

/* Debounced handler: read pin state and act */
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
		if (button_pressed_callback != NULL) {
			button_pressed_callback();
		}
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

	/* Register ISR callback */
	gpio_init_callback(&btn_cb_data, button_isr, BIT(user_button.pin));
	ret = gpio_add_callback(user_button.port, &btn_cb_data);
	if (ret < 0) {
		LOG_ERR("Failed to add button callback (err %d)", ret);
		return;
	}

	LOG_INF("Button interrupt armed on pin %u (active %s)", user_button.pin,
		(user_button.dt_flags & GPIO_ACTIVE_LOW) ? "LOW" : "HIGH");
}
