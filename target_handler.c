/*
Copyright (c) 2019, Intel Corporation

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

    * Redistributions of source code must retain the above copyright notice,
      this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of Intel Corporation nor the names of its contributors
      may be used to endorse or promote products derived from this software
      without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/time.h>
#include <unistd.h>
#include <string.h>
#include <poll.h>

#include "asd_common.h"
#include "target_handler.h"
#include "gpio.h"
#include "logging.h"
#include "mem_helper.h"

#define JTAG_CLOCK_CYCLE_MILLISECONDS 1000

STATUS initialize_gpios(Target_Control_Handle *state);
STATUS find_gpio(char *gpio_name, int *gpio_number);
STATUS deinitialize_gpios(Target_Control_Handle *state);
STATUS on_power_event(Target_Control_Handle *state, ASD_EVENT *event);
STATUS on_platform_reset_event(Target_Control_Handle *state, ASD_EVENT *event);
STATUS on_prdy_event(Target_Control_Handle *state, ASD_EVENT *event);
STATUS on_xdp_present_event(Target_Control_Handle *state, ASD_EVENT *event);
STATUS initialize_gpio(Target_Control_GPIO *gpio);

static const ASD_LogStream stream = ASD_LogStream_Pins;
static const ASD_LogOption option = ASD_LogOption_None;

Target_Control_Handle *TargetHandler()
{
	Target_Control_Handle *state =
		(Target_Control_Handle *)malloc(sizeof(Target_Control_Handle));

	if (state == NULL)
		return NULL;

	state->dbus = dbus_helper();
	if (state->dbus == NULL) {
		free(state);
		return NULL;
	}

	state->initialized = false;

	for (int i = 0; i < NUM_GPIOS; i++) {
		state->gpios[i].number = -1;
		state->gpios[i].fd = -1;
		state->gpios[i].handler = NULL;
		state->gpios[i].active_low = false;
		state->gpios[i].type = PIN_GPIO;
	}

	strcpy_safe(state->gpios[BMC_TCK_MUX_SEL].name,
		    sizeof(state->gpios[BMC_TCK_MUX_SEL].name),
		    "BMC_TCK_MUX_SEL", sizeof("BMC_TCK_MUX_SEL"));
	state->gpios[BMC_TCK_MUX_SEL].direction = GPIO_DIRECTION_LOW;
	state->gpios[BMC_TCK_MUX_SEL].edge = GPIO_EDGE_NONE;

	strcpy_safe(state->gpios[BMC_PREQ_N].name,
		    sizeof(state->gpios[BMC_PREQ_N].name), "BMC_PREQ_N",
		    sizeof("BMC_PREQ_N"));
	state->gpios[BMC_PREQ_N].direction = GPIO_DIRECTION_HIGH;
	state->gpios[BMC_PREQ_N].edge = GPIO_EDGE_NONE;
	state->gpios[BMC_PREQ_N].active_low = true;

	strcpy_safe(state->gpios[BMC_PRDY_N].name,
		    sizeof(state->gpios[BMC_PRDY_N].name), "BMC_PRDY_N",
		    sizeof("BMC_PRDY_N"));
	state->gpios[BMC_PRDY_N].direction = GPIO_DIRECTION_IN;
	state->gpios[BMC_PRDY_N].edge = GPIO_EDGE_FALLING;
	state->gpios[BMC_PRDY_N].active_low = true;

	strcpy_safe(state->gpios[BMC_RSMRST_B].name,
		    sizeof(state->gpios[BMC_RSMRST_B].name), "BMC_RSMRST_B",
		    sizeof("BMC_RSMRST_B"));
	state->gpios[BMC_RSMRST_B].direction = GPIO_DIRECTION_IN;
	state->gpios[BMC_RSMRST_B].edge = GPIO_EDGE_NONE;

	strcpy_safe(state->gpios[BMC_CPU_PWRGD].name,
		    sizeof(state->gpios[BMC_CPU_PWRGD].name), "BMC_CPU_PWRGD",
		    sizeof("BMC_CPU_PWRGD"));
	state->gpios[BMC_CPU_PWRGD].direction = GPIO_DIRECTION_IN;
	state->gpios[BMC_CPU_PWRGD].edge = GPIO_EDGE_BOTH;
	state->gpios[BMC_CPU_PWRGD].type = PIN_DBUS;

	strcpy_safe(state->gpios[BMC_PLTRST_B].name,
		    sizeof(state->gpios[BMC_PLTRST_B].name), "BMC_PLTRST_B",
		    sizeof("BMC_PLTRST_B"));
	state->gpios[BMC_PLTRST_B].direction = GPIO_DIRECTION_IN;
	state->gpios[BMC_PLTRST_B].edge = GPIO_EDGE_BOTH;

	strcpy_safe(state->gpios[BMC_SYSPWROK].name,
		    sizeof(state->gpios[BMC_SYSPWROK].name), "BMC_SYSPWROK",
		    sizeof("BMC_SYSPWROK"));
	state->gpios[BMC_SYSPWROK].direction = GPIO_DIRECTION_HIGH;
	state->gpios[BMC_SYSPWROK].edge = GPIO_EDGE_NONE;

	strcpy_safe(state->gpios[BMC_PWR_DEBUG_N].name,
		    sizeof(state->gpios[BMC_PWR_DEBUG_N].name),
		    "BMC_PWR_DEBUG_N", sizeof("BMC_PWR_DEBUG_N"));
	state->gpios[BMC_PWR_DEBUG_N].direction = GPIO_DIRECTION_HIGH;
	state->gpios[BMC_PWR_DEBUG_N].edge = GPIO_EDGE_NONE;
	state->gpios[BMC_PWR_DEBUG_N].active_low = true;

	strcpy_safe(state->gpios[BMC_DEBUG_EN_N].name,
		    sizeof(state->gpios[BMC_DEBUG_EN_N].name), "BMC_DEBUG_EN_N",
		    sizeof("BMC_DEBUG_EN_N"));
	state->gpios[BMC_DEBUG_EN_N].direction = GPIO_DIRECTION_HIGH;
	state->gpios[BMC_DEBUG_EN_N].edge = GPIO_EDGE_NONE;
	state->gpios[BMC_DEBUG_EN_N].active_low = true;

	strcpy_safe(state->gpios[BMC_XDP_PRST_IN].name,
		    sizeof(state->gpios[BMC_XDP_PRST_IN].name),
		    "BMC_XDP_PRST_IN", sizeof("BMC_XDP_PRST_IN"));
	state->gpios[BMC_XDP_PRST_IN].direction = GPIO_DIRECTION_IN;
	state->gpios[BMC_XDP_PRST_IN].active_low = true;
	state->gpios[BMC_XDP_PRST_IN].edge = GPIO_EDGE_BOTH;

	state->event_cfg.break_all = false;
	state->event_cfg.report_MBP = false;
	state->event_cfg.report_PLTRST = false;
	state->event_cfg.report_PRDY = false;
	state->event_cfg.reset_break = false;

	initialize_powergood_pin_handler(state, PIN_GPIO);
	state->gpios[BMC_PLTRST_B].handler =
		(TargetHandlerEventFunctionPtr)on_platform_reset_event;
	state->gpios[BMC_PRDY_N].handler =
		(TargetHandlerEventFunctionPtr)on_prdy_event;
	state->gpios[BMC_XDP_PRST_IN].handler =
		(TargetHandlerEventFunctionPtr)on_xdp_present_event;

	// Change is_master_probe accordingly on your BMC implementations.
	// <MODIFY>
	state->is_master_probe = false;
	// </MODIFY>

	return state;
}

STATUS initialize_powergood_pin_handler(Target_Control_Handle *state,
					Pin_Type PinType)
{
	int result = ST_OK;
	if (PinType == PIN_GPIO) {
		state->gpios[BMC_CPU_PWRGD].handler =
			(TargetHandlerEventFunctionPtr)on_power_event;
	}
	return result;
}

STATUS target_initialize(Target_Control_Handle *state)
{
	STATUS result;
	int value = 0;
	if (state == NULL || state->initialized)
		return ST_ERR;

	result = initialize_gpios(state);

	if (result == ST_OK) {
		result = gpio_get_value(state->gpios[BMC_XDP_PRST_IN].fd,
					&value);
		if (result != ST_OK) {
			ASD_log(ASD_LogLevel_Error, stream, option,
				"Failed check XDP state or XDP not available");
		} else if (value == 1) {
			result = ST_ERR;
			ASD_log(ASD_LogLevel_Error, stream, option,
				"XDP Presence Detected");
		}
	}

	// specifically drive debug enable to assert
	if (result == ST_OK) {
		result = gpio_set_value(state->gpios[BMC_DEBUG_EN_N].fd, 1);
		if (result != ST_OK) {
			ASD_log(ASD_LogLevel_Error, stream, option,
				"Failed to assert debug enable");
		}
	}

	if (result == ST_OK) {
		result = dbus_initialize(state->dbus);
	}

	if (result == ST_OK)
		state->initialized = true;
	else
		deinitialize_gpios(state);

	return result;
}

STATUS initialize_gpios(Target_Control_Handle *state)
{
	STATUS result = ST_OK;
	int i;

	for (i = 0; i < NUM_GPIOS; i++) {
		if (state->gpios[i].type == PIN_GPIO) {
			result = initialize_gpio(&state->gpios[i]);
			if (result != ST_OK)
				break;
			// do a read to clear any bogus events on startup
			int dummy;
			result = gpio_get_value(state->gpios[i].fd, &dummy);
			if (result != ST_OK)
				break;
		}
	}

	if (result == ST_OK)
		ASD_log(ASD_LogLevel_Info, stream, option,
			"GPIOs initialized successfully");
	else
		ASD_log(ASD_LogLevel_Error, stream, option,
			"GPIOs initialization failed");
	return result;
}

STATUS initialize_gpio(Target_Control_GPIO *gpio)
{
	int num;
	STATUS result = find_gpio(gpio->name, &num);

	if (result != ST_OK) {
		ASD_log(ASD_LogLevel_Error, stream, option,
			"Failed to find gpio for %s", gpio->name);
	} else {
		result = gpio_export(num, &gpio->fd);
		if (result != ST_OK)
			ASD_log(ASD_LogLevel_Error, stream, option,
				"Gpio export failed for %s", gpio->name);
#ifdef ENABLE_DEBUG_LOGGING
		else
			ASD_log(ASD_LogLevel_Debug, stream, option,
				"Gpio export succeeded for %s num %d fd %d",
				gpio->name, num, gpio->fd);
#endif
	}

	if (result == ST_OK) {
		result = gpio_set_active_low(num, gpio->active_low);
		if (result != ST_OK)
			ASD_log(ASD_LogLevel_Error, stream, option,
				"Gpio set active low failed for %s",
				gpio->name);
	}

	if (result == ST_OK) {
		result = gpio_set_direction(num, gpio->direction);
		if (result != ST_OK)
			ASD_log(ASD_LogLevel_Error, stream, option,
				"Gpio set direction failed for %s", gpio->name);
	}

	if (result == ST_OK) {
		result = gpio_set_edge(num, gpio->edge);
		if (result != ST_OK)
			ASD_log(ASD_LogLevel_Error, stream, option,
				"Gpio set edge failed for %s", gpio->name);
	}

	if (result == ST_OK) {
		gpio->number = num;
		ASD_log(ASD_LogLevel_Info, stream, option,
			"gpio %s initialized to %d", gpio->name, gpio->number);
	}

	return result;
}

STATUS find_gpio(char *gpio_name, int *gpio_number)
{
	// This function will soon be replaced with code to
	// scan the system at runtime and produce the gpio
	// number that corresponds to the gpio name.
	STATUS result = ST_OK;
	if (strcmp(gpio_name, "BMC_TCK_MUX_SEL") == 0)
		*gpio_number = 213;
	else if (strcmp(gpio_name, "BMC_PREQ_N") == 0)
		*gpio_number = 212;
	else if (strcmp(gpio_name, "BMC_PRDY_N") == 0)
		*gpio_number = 47;
	else if (strcmp(gpio_name, "BMC_RSMRST_B") == 0)
		*gpio_number = 146;
	else if (strcmp(gpio_name, "BMC_CPU_PWRGD") == 0)
		*gpio_number = 201;
	else if (strcmp(gpio_name, "BMC_PLTRST_B") == 0)
		*gpio_number = 46;
	else if (strcmp(gpio_name, "BMC_SYSPWROK") == 0)
		*gpio_number = 145;
	else if (strcmp(gpio_name, "BMC_PWR_DEBUG_N") == 0)
		*gpio_number = 135;
	else if (strcmp(gpio_name, "BMC_DEBUG_EN_N") == 0)
		*gpio_number = 37;
	else if (strcmp(gpio_name, "BMC_XDP_PRST_IN") == 0)
		*gpio_number = 137;
	else
		result = ST_ERR;

	return result;
}

STATUS target_deinitialize(Target_Control_Handle *state)
{
	if (state == NULL || !state->initialized)
		return ST_ERR;

	for (int i = 0; i < NUM_GPIOS; i++) {
		if (state->gpios[i].type == PIN_GPIO) {
			if (state->gpios[i].fd != -1) {
				close(state->gpios[i].fd);
				state->gpios[i].fd = -1;
			}
		}
	}

	dbus_deinitialize(state->dbus);

	return deinitialize_gpios(state);
}

STATUS deinitialize_gpios(Target_Control_Handle *state)
{
	STATUS result = ST_OK;
	STATUS retcode = ST_OK;
	int i;

	for (i = 0; i < NUM_GPIOS; i++) {
		if (state->gpios[i].type == PIN_GPIO) {
			retcode = gpio_set_direction(state->gpios[i].number,
						     GPIO_DIRECTION_IN);
			if (retcode != ST_OK) {
				ASD_log(ASD_LogLevel_Error, stream, option,
					"Gpio set direction failed for %s",
					state->gpios[i].name);
				result = ST_ERR;
			}
			retcode = gpio_unexport(state->gpios[i].number);
			if (retcode != ST_OK) {
				ASD_log(ASD_LogLevel_Error, stream, option,
					"Gpio export failed for %s",
					state->gpios[i].name);
				result = ST_ERR;
			}
		}
	}

	ASD_log(ASD_LogLevel_Info, stream, option,
		(result == ST_OK) ? "GPIOs deinitialized successfully"
				  : "GPIOs deinitialized failed");
	return result;
}

STATUS target_event(Target_Control_Handle *state, struct pollfd poll_fd,
		    ASD_EVENT *event)
{
	STATUS result = ST_ERR;
	int i;

	if (state == NULL || !state->initialized || event == NULL)
		return ST_ERR;

	*event = ASD_EVENT_NONE;

	if (state->dbus && state->dbus->fd == poll_fd.fd
	    && (poll_fd.revents & POLLIN) == POLLIN) {
#ifdef ENABLE_DEBUG_LOGGING
		ASD_log(ASD_LogLevel_Debug, stream, option,
			"Handling dbus event for fd: %d", poll_fd.fd);
#endif
		result = dbus_process_event(state->dbus, event);
	} else if ((poll_fd.revents & POLL_GPIO) == POLL_GPIO) {
#ifdef ENABLE_DEBUG_LOGGING
		ASD_log(ASD_LogLevel_Debug, stream, option,
			"Handling event for fd: %d", poll_fd.fd);
#endif
		for (i = 0; i < NUM_GPIOS; i++) {
			if (state->gpios[i].type == PIN_GPIO) {
				if (state->gpios[i].fd == poll_fd.fd) {
					// do a read to clear the event
					int dummy;
					gpio_get_value(poll_fd.fd, &dummy);
					result = state->gpios[i].handler(state,
									 event);
					break;
				}
			}
		}
	} else {
		result = ST_OK;
	}

	return result;
}

STATUS on_power_event(Target_Control_Handle *state, ASD_EVENT *event)
{
	STATUS result;
	int value;

	result = gpio_get_value(state->gpios[BMC_CPU_PWRGD].fd, &value);
	if (result != ST_OK) {
		ASD_log(ASD_LogLevel_Error, stream, option,
			"Failed to get gpio data for CPU_PWRGD: %d", result);
	} else if (value == 1) {
#ifdef ENABLE_DEBUG_LOGGING
		ASD_log(ASD_LogLevel_Debug, stream, option, "Power restored");
#endif
		*event = ASD_EVENT_PWRRESTORE;
	} else {
#ifdef ENABLE_DEBUG_LOGGING
		ASD_log(ASD_LogLevel_Debug, stream, option, "Power fail");
#endif
		*event = ASD_EVENT_PWRFAIL;
	}
	return result;
}

STATUS on_platform_reset_event(Target_Control_Handle *state, ASD_EVENT *event)
{
	STATUS result;
	int value;

	result = gpio_get_value(state->gpios[BMC_PLTRST_B].fd, &value);
	if (result != ST_OK) {
		ASD_log(ASD_LogLevel_Error, stream, option,
			"Failed to get event status for PLTRST: %d", result);
	} else if (value == 1) {
#ifdef ENABLE_DEBUG_LOGGING
		ASD_log(ASD_LogLevel_Debug, stream, option,
			"Platform reset asserted");
#endif
		*event = ASD_EVENT_PLRSTASSERT;
		if (state->event_cfg.reset_break) {
#ifdef ENABLE_DEBUG_LOGGING
			ASD_log(ASD_LogLevel_Debug, stream, option,
				"ResetBreak detected PLT_RESET "
				"assert, asserting PREQ");
#endif
			result = gpio_set_value(state->gpios[BMC_PREQ_N].fd, 1);
			if (result != ST_OK) {
				ASD_log(ASD_LogLevel_Error, stream, option,
					"Failed to assert PREQ");
			}
		}
	} else {
#ifdef ENABLE_DEBUG_LOGGING
		ASD_log(ASD_LogLevel_Debug, stream, option,
			"Platform reset de-asserted");
#endif
		*event = ASD_EVENT_PLRSTDEASSRT;
	}

	return result;
}

STATUS on_prdy_event(Target_Control_Handle *state, ASD_EVENT *event)
{
	STATUS result = ST_OK;

#ifdef ENABLE_DEBUG_LOGGING
	ASD_log(ASD_LogLevel_Debug, stream, option,
		"CPU_PRDY Asserted Event Detected.");
#endif
	*event = ASD_EVENT_PRDY_EVENT;
	if (state->event_cfg.break_all) {
#ifdef ENABLE_DEBUG_LOGGING
		ASD_log(ASD_LogLevel_Debug, stream, option,
			"BreakAll detected PRDY, asserting PREQ");
#endif
		result = gpio_set_value(state->gpios[BMC_PREQ_N].fd, 1);
		if (result != ST_OK) {
			ASD_log(ASD_LogLevel_Error, stream, option,
				"Failed to assert PREQ");
		} else if (!state->event_cfg.reset_break) {
			usleep(10000);
#ifdef ENABLE_DEBUG_LOGGING
			ASD_log(ASD_LogLevel_Debug, stream, option,
				"CPU_PRDY, de-asserting PREQ");
#endif
			result = gpio_set_value(state->gpios[BMC_PREQ_N].fd, 0);
			if (result != ST_OK) {
				ASD_log(ASD_LogLevel_Error, stream, option,
					"Failed to deassert PREQ");
			}
		}
	}

	return result;
}

STATUS on_xdp_present_event(Target_Control_Handle *state, ASD_EVENT *event)
{
	STATUS result = ST_OK;
	(void)state; /* unused */

#ifdef ENABLE_DEBUG_LOGGING
	ASD_log(ASD_LogLevel_Debug, stream, option,
		"XDP Present state change detected");
#endif
	*event = ASD_EVENT_XDP_PRESENT;

	return result;
}

STATUS target_write(Target_Control_Handle *state, const Pin pin,
		    const bool assert)
{
	STATUS result = ST_OK;
	Target_Control_GPIO gpio;
	int value;
	if (state == NULL || !state->initialized) {
		ASD_log(ASD_LogLevel_Error, stream, option,
			"target_write, null or uninitialized state");
		return ST_ERR;
	}
	switch (pin) {
	case PIN_RESET_BUTTON:
		ASD_log(ASD_LogLevel_Info, stream, option,
			"Pin Write: %s reset button",
			assert ? "assert" : "deassert");
		if (assert && state->event_cfg.reset_break) {
#ifdef ENABLE_DEBUG_LOGGING
			ASD_log(ASD_LogLevel_Debug, stream, option,
				"Reset break armed, asserting PREQ");
#endif
			result = gpio_set_value(state->gpios[BMC_PREQ_N].fd, 1);
			if (result != ST_OK) {
				ASD_log(ASD_LogLevel_Error, stream, option,
					"Assert PREQ for ResetBreak failed.");
			}
		}
		if (result == ST_OK) {
			result = dbus_power_reset(state->dbus);
		}
		break;
	case PIN_POWER_BUTTON:
		ASD_log(ASD_LogLevel_Info, stream, option,
			"Pin Write: %s power button",
			assert ? "assert" : "deassert");
		if (assert) {
			if (state->event_cfg.reset_break) {
#ifdef ENABLE_DEBUG_LOGGING
				ASD_log(ASD_LogLevel_Debug, stream, option,
					"Reset break armed, asserting PREQ");
#endif
				result = gpio_set_value(
					state->gpios[BMC_PREQ_N].fd, 1);
				if (result != ST_OK) {
					ASD_log(ASD_LogLevel_Error, stream,
						option,
						"Assert PREQ for ResetBreak failed");
				}
			}
			if (result == ST_OK) {
				if (state->gpios[BMC_CPU_PWRGD].type
				    == PIN_GPIO)
					result = result = gpio_get_value(
						state->gpios[BMC_CPU_PWRGD].fd,
						&value);
				else
					result = dbus_get_hoststate(state->dbus,
								    &value);
				if (result != ST_OK) {
#ifdef ENABLE_DEBUG_LOGGING
					ASD_log(ASD_LogLevel_Debug, stream,
						option,
						"Failed to read gpio %s %d",
						gpio.name, gpio.number);
#endif
				} else {
					if (value) {
						result = dbus_power_off(
							state->dbus);
					} else {
						result = dbus_power_on(
							state->dbus);
					}
				}
			}
		}
		break;
	case PIN_PREQ:
	case PIN_TCK_MUX_SELECT:
	case PIN_SYS_PWR_OK:
	case PIN_EARLY_BOOT_STALL:
		gpio = state->gpios[ASD_PIN_TO_GPIO[pin]];
		ASD_log(ASD_LogLevel_Info, stream, option,
			"Pin Write: %s %s %d", assert ? "assert" : "deassert",
			gpio.name, gpio.number);
		result = gpio_set_value(gpio.fd, (uint8_t)(assert ? 1 : 0));
		if (result != ST_OK) {
			ASD_log(ASD_LogLevel_Error, stream, option,
				"Failed to set %s %s %d",
				assert ? "assert" : "deassert", gpio.name,
				gpio.number);
		}
		break;
	default:
#ifdef ENABLE_DEBUG_LOGGING
		ASD_log(ASD_LogLevel_Debug, stream, option,
			"Pin write: unsupported pin '%s'", gpio.name);
#endif
		result = ST_ERR;
		break;
	}

	return result;
}

STATUS target_read(Target_Control_Handle *state, Pin pin, bool *asserted)
{
	STATUS result;
	Target_Control_GPIO gpio;
	int value;
	if (state == NULL || asserted == NULL || !state->initialized) {
		ASD_log(ASD_LogLevel_Error, stream, option,
			"target_read, null or uninitialized state");
		return ST_ERR;
	}
	*asserted = false;

	switch (pin) {
	case PIN_PWRGOOD:
		if (state->gpios[BMC_CPU_PWRGD].type == PIN_GPIO) {
			result = gpio_get_value(state->gpios[BMC_CPU_PWRGD].fd,
						&value);
		} else {
			result = dbus_get_hoststate(state->dbus, &value);
		}
		if (result != ST_OK) {
#ifdef ENABLE_DEBUG_LOGGING
			ASD_log(ASD_LogLevel_Debug, stream, option,
				"Failed to read PIN %s %d", gpio.name,
				gpio.number);
#endif
		} else {
			*asserted = (value != 0);
#ifdef ENABLE_DEBUG_LOGGING
			ASD_log(ASD_LogLevel_Info, stream, option,
				"Pin read: %s powergood",
				*asserted ? "asserted" : "deasserted");
#endif
		}
		break;
	case PIN_PRDY:
	case PIN_PREQ:
	case PIN_SYS_PWR_OK:
	case PIN_EARLY_BOOT_STALL:
		gpio = state->gpios[ASD_PIN_TO_GPIO[pin]];

		result = gpio_get_value(gpio.fd, &value);
		if (result != ST_OK) {
			ASD_log(ASD_LogLevel_Error, stream, option,
				"Failed to read gpio %s %d", gpio.name,
				gpio.number);
		} else {
			*asserted = (value != 0);
			ASD_log(ASD_LogLevel_Info, stream, option,
				"Pin read: %s %s %d",
				*asserted ? "asserted" : "deasserted",
				gpio.name, gpio.number);
		}
		break;
	default:
#ifdef ENABLE_DEBUG_LOGGING
		ASD_log(ASD_LogLevel_Debug, stream, option,
			"Pin read: unsupported gpio '%s'", gpio.name);
#endif
		result = ST_ERR;
	}

	return result;
}

STATUS target_write_event_config(Target_Control_Handle *state,
				 const WriteConfig event_cfg, const bool enable)
{
	STATUS status = ST_OK;
	if (state == NULL || !state->initialized) {
		ASD_log(ASD_LogLevel_Error, stream, option,
			"target_write_event_config, null or uninitialized state");
		return ST_ERR;
	}

	switch (event_cfg) {
	case WRITE_CONFIG_BREAK_ALL: {
		if (state->event_cfg.break_all != enable) {
#ifdef ENABLE_DEBUG_LOGGING
			ASD_log(ASD_LogLevel_Debug, stream, option,
				"BREAK_ALL %s",
				enable ? "enabled" : "disabled");
#endif
			state->event_cfg.break_all = enable;
		}
		break;
	}
	case WRITE_CONFIG_RESET_BREAK: {
		if (state->event_cfg.reset_break != enable) {
#ifdef ENABLE_DEBUG_LOGGING
			ASD_log(ASD_LogLevel_Debug, stream, option,
				"RESET_BREAK %s",
				enable ? "enabled" : "disabled");
#endif
			state->event_cfg.reset_break = enable;
		}
		break;
	}
	case WRITE_CONFIG_REPORT_PRDY: {
#ifdef ENABLE_DEBUG_LOGGING
		if (state->event_cfg.report_PRDY != enable) {
			ASD_log(ASD_LogLevel_Debug, stream, option,
				"REPORT_PRDY %s",
				enable ? "enabled" : "disabled");
		}
#endif
		// do a read to ensure no outstanding prdys are present before
		// wait for prdy is enabled.
		int dummy;
		gpio_get_value(state->gpios[BMC_PRDY_N].fd, &dummy);
		state->event_cfg.report_PRDY = enable;
		break;
	}
	case WRITE_CONFIG_REPORT_PLTRST: {
		if (state->event_cfg.report_PLTRST != enable) {
#ifdef ENABLE_DEBUG_LOGGING
			ASD_log(ASD_LogLevel_Debug, stream, option,
				"REPORT_PLTRST %s",
				enable ? "enabled" : "disabled");
#endif
			state->event_cfg.report_PLTRST = enable;
		}
		break;
	}
	case WRITE_CONFIG_REPORT_MBP: {
		if (state->event_cfg.report_MBP != enable) {
#ifdef ENABLE_DEBUG_LOGGING
			ASD_log(ASD_LogLevel_Debug, stream, option,
				"REPORT_MBP %s",
				enable ? "enabled" : "disabled");
#endif
			state->event_cfg.report_MBP = enable;
		}
		break;
	}
	default: {
		ASD_log(ASD_LogLevel_Error, stream, option,
			"Invalid event config %d", event_cfg);
		status = ST_ERR;
	}
	}
	return status;
}

STATUS target_wait_PRDY(Target_Control_Handle *state, const uint8_t log2time)
{
	// The design for this calls for waiting for PRDY or until a timeout
	// occurs. The timeout is computed using the PRDY timeout setting
	// (log2time) and the JTAG TCLK.

	int timeout_ms;
	struct pollfd pfd;
	int poll_result;
	STATUS result = ST_OK;

	if (state == NULL || !state->initialized) {
		ASD_log(ASD_LogLevel_Error, stream, option,
			"target_wait_PRDY, null or uninitialized state");
		return ST_ERR;
	}

	// The timeout for commands that wait for a PRDY pulse is defined by the
	// period of the JTAG clock multiplied by 2^log2time.
	timeout_ms = JTAG_CLOCK_CYCLE_MILLISECONDS * (1 << log2time);

	pfd.events = POLL_GPIO;
	pfd.fd = state->gpios[BMC_PRDY_N].fd;
	poll_result = poll(&pfd, 1, timeout_ms);
	if (poll_result == 0) {
#ifdef ENABLE_DEBUG_LOGGING
		ASD_log(ASD_LogLevel_Debug, stream, option,
			"Wait PRDY timed out occurred");
#endif
		// future: we should return something to indicate a timeout
	} else if (poll_result > 0) {
		if (pfd.revents & POLL_GPIO) {
#ifdef ENABLE_DEBUG_LOGGING
			ASD_log(ASD_LogLevel_Trace, stream, option,
				"Wait PRDY complete, detected PRDY");
#endif
		}
	} else {
		ASD_log(ASD_LogLevel_Error, stream, option,
			"target_wait_PRDY poll failed: %d.", poll_result);
		result = ST_ERR;
	}
	return result;
}

STATUS target_get_fds(Target_Control_Handle *state, target_fdarr_t *fds,
		      int *num_fds)
{
	int index = 0;

	if (state == NULL || !state->initialized || fds == NULL
	    || num_fds == NULL) {
#ifdef ENABLE_DEBUG_LOGGING
		ASD_log(ASD_LogLevel_Trace, stream, option,
			"target_get_fds, null or uninitialized state");
#endif
		return ST_ERR;
	}

	if (state->event_cfg.report_PRDY && state->gpios[BMC_PRDY_N].fd != -1) {
		(*fds)[index].fd = state->gpios[BMC_PRDY_N].fd;
		(*fds)[index].events = POLL_GPIO;
		index++;
	}

	if (state->gpios[BMC_PLTRST_B].fd != -1) {
		(*fds)[index].fd = state->gpios[BMC_PLTRST_B].fd;
		(*fds)[index].events = POLL_GPIO;
		index++;
	}
	if (state->gpios[BMC_CPU_PWRGD].type == PIN_GPIO) {
		if (state->gpios[BMC_CPU_PWRGD].fd != -1) {
			(*fds)[index].fd = state->gpios[BMC_CPU_PWRGD].fd;
			(*fds)[index].events = POLL_GPIO;
			index++;
		}
	}

	if (state->gpios[BMC_XDP_PRST_IN].fd != -1) {
		(*fds)[index].fd = state->gpios[BMC_XDP_PRST_IN].fd;
		(*fds)[index].events = POLL_GPIO;
		index++;
	}

	if (state->dbus && state->dbus->fd != -1) {
		(*fds)[index].fd = state->dbus->fd;
		(*fds)[index].events = POLLIN;
		index++;
	}

	*num_fds = index;

	return ST_OK;
}

// target_wait_sync - This command will only be issued in a multiple
//   probe configuration where there are two or more TAP masters. The
//   WaitSync command is used to tell all TAP masters to wait until a
//   sync indication is received. The exact flow of sync signaling is
//   implementation specific. Command processing will continue after
//   either the Sync indication is received or the SyncTimeout is
//   reached. The SyncDelay is intended to be used in an implementation
//   where there is a single sync signal routed from a single designated
//   TAP Master to all other TAP Masters. The SyncDelay is used as an
//   implicit means to ensure that all other TAP Masters have reached
//   the WaitSync before the Sync signal is asserted.
//
// Parameters:
//  timeout - the SyncTimeout provides a timeout value to all slave
//    probes. If a slave probe does not receive the sync signal during
//    this timeout period, then a timeout occurs. The value is in
//    milliseconds (Range 0ms - 65s).
//  delay - the SyncDelay is meant to be used in a single master probe
//    sync singal implmentation. Upon receiving the WaitSync command,
//    the probe will delay for the Sync Delay Value before sending
//    the sync signal. This is to ensure that all slave probes
//    have reached WaitSync state prior to the sync being sent.
//    The value is in milliseconds (Range 0ms - 65s).
//
// Returns:
//  ST_OK if operation completed successfully.
//  ST_ERR if operation failed.
//  ST_TIMEOUT if failed to detect sync signal
STATUS target_wait_sync(Target_Control_Handle *state, const uint16_t timeout,
			const uint16_t delay)
{
	STATUS result = ST_OK;
	if (state == NULL || !state->initialized) {
#ifdef ENABLE_DEBUG_LOGGING
		ASD_log(ASD_LogLevel_Trace, stream, option,
			"target_wait_sync, null or uninitialized state");
#endif
		return ST_ERR;
	}

#ifdef ENABLE_DEBUG_LOGGING
	ASD_log(ASD_LogLevel_Debug, stream, option,
		"WaitSync(%s) - delay=%u ms - timeout=%u ms",
		state->is_master_probe ? "master" : "slave", delay, timeout);
#endif

	if (state->is_master_probe) {
		usleep((__useconds_t)(delay * 1000)); // convert from us to ms
		// Once delay has occurred, send out the sync signal.

		// <MODIFY>
		// hard code a error until code is implemented
		result = ST_ERR;
		// </MODIFY>
	} else {
		// Wait for sync signal to arrive.
		// timeout if sync signal is not received in the
		// milliseconds provided by the timeout parameter

		// <MODIFY>
		usleep((__useconds_t)(timeout * 1000)); // convert from us to ms
		// hard code a error/timeout until code is implemented
		result = ST_TIMEOUT;
		// when sync is detected, set result to ST_OK
		// </MODIFY>
	}

	return result;
}
