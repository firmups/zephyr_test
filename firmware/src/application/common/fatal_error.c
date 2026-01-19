#include "fatal_error.h"
#include "led.h"

#include <zephyr/kernel.h>

void blink_red_led_forever(void)
{
	led_blink_end(LED_GREEN);
	led_blink_end(LED_ORANGE);
	led_blink_end(LED_BLUE);
	led_blink_start(LED_RED);
	while (1) {
		k_sleep(K_MSEC(100000));
	}
}
