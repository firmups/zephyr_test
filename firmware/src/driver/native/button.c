#include <zephyr/logging/log.h>
#include "button.h"
#include <zephyr/shell/shell.h>

LOG_MODULE_REGISTER(button_driver, LOG_LEVEL_INF);

static void (*callback_fn)(void) = NULL;

void button_init(void (*callback)(void))
{
	LOG_INF("Button initialized");
	callback_fn = callback;
}

static int cmd_press_button(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	if (!callback_fn) {
		shell_error(sh, "Button callback not set");
		return -EINVAL;
	}
	callback_fn();
	shell_print(sh, "Button pressed");
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_button,
			       SHELL_CMD(press, NULL, "Simulate a button press", cmd_press_button),
			       SHELL_SUBCMD_SET_END);

/* Register the top-level 'button' command with its subcommands */
SHELL_CMD_REGISTER(button, &sub_button, "Button command group.", NULL);
