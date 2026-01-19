#ifndef LED_H
#define LED_H

enum led_type {
	LED_GREEN,
	LED_ORANGE,
	LED_RED,
	LED_BLUE,
	LED_END,
};

int led_initialize(void);
void led_blink_start(enum led_type led);
void led_blink_end(enum led_type led);

#endif // LED_H
