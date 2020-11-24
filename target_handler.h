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

#ifndef _TARGET_CONTROL_HANDLER_H_
#define _TARGET_CONTROL_HANDLER_H_

#include "config.h"

#include <poll.h>
#include <stdbool.h>

#include "asd_common.h"
#include "dbus_helper.h"
#include "gpio.h"

#define POLL_GPIO (POLLPRI + POLLERR)
#define CHIP_BUFFER_SIZE 32

#define GPIO_SYSFS_SUPPORT_DEPRECATED

#ifdef GPIO_SYSFS_SUPPORT_DEPRECATED
// GPIO base needs to be adjusted according to your platform settings.
// Base can be found into /sys/class/gpio/gpiochipX/base file. If multiple
// gpiochip are found in your gpio folder use /sys/class/gpio/gpiochipX/label
// to find out the gpio controller that handles your gpios.
#define AST2500_GPIO_BASE_FILE "/sys/class/gpio/gpiochip0/base"
#define CHIP_FNAME_BUFF_SIZE 48
#endif

#define WOLF_PASS_PLATFORM_ID 0x7B
#define WILSON_CITY_PLATFORM_ID 0x91
#define WILSON_POINT_PLATFORM_ID 0x9A
#define COOPER_CITY_PLATFORM_ID 0x9D

typedef enum
{
    READ_TYPE_MIN = -1,
    READ_TYPE_PROBE = 0,
    READ_TYPE_PIN,
    READ_TYPE_MAX // Insert new read cfg indices before
                  // READ_STATUS_INDEX_MAX
} ReadType;

typedef enum
{
    WRITE_CONFIG_MIN = -1,
    WRITE_CONFIG_BREAK_ALL = 0,
    WRITE_CONFIG_RESET_BREAK,
    WRITE_CONFIG_REPORT_PRDY,
    WRITE_CONFIG_REPORT_PLTRST,
    WRITE_CONFIG_REPORT_MBP,
    WRITE_CONFIG_MAX // Insert before WRITE_EVENT_CONFIG_MAX
} WriteConfig;

typedef struct event_configuration
{
    bool break_all;
    bool reset_break;
    bool report_PRDY;
    bool report_PLTRST;
    bool report_MBP;
} event_configuration;

#define ALL_TARGET_CONTROL_GPIOS(FUNC)                                         \
    FUNC(BMC_TCK_MUX_SEL)                                                      \
    FUNC(BMC_PREQ_N)                                                           \
    FUNC(BMC_PRDY_N)                                                           \
    FUNC(BMC_RSMRST_B)                                                         \
    FUNC(BMC_CPU_PWRGD)                                                        \
    FUNC(BMC_PLTRST_B)                                                         \
    FUNC(BMC_SYSPWROK)                                                         \
    FUNC(BMC_PWR_DEBUG_N)                                                      \
    FUNC(BMC_DEBUG_EN_N)                                                       \
    FUNC(BMC_XDP_PRST_IN)                                                      \
    FUNC(POWER_BTN)                                                            \
    FUNC(RESET_BTN)

#define NUM_GPIOS 10

typedef enum
{
    ALL_TARGET_CONTROL_GPIOS(TO_ENUM)
} Target_Control_GPIOS;

static const char* TARGET_CONTROL_GPIO_STRINGS[] = {
    ALL_TARGET_CONTROL_GPIOS(TO_STRING)};

// Maps from ASD Protocol pin definitions to BMC GPIOs
static const Target_Control_GPIOS ASD_PIN_TO_GPIO[] = {
    BMC_CPU_PWRGD,   // PIN_PWRGOOD
    BMC_PREQ_N,      // PIN_PREQ
    RESET_BTN,       // PIN_RESET_BUTTON
    POWER_BTN,       // PIN_POWER_BUTTON
    BMC_PWR_DEBUG_N, // PIN_EARLY_BOOT_STALL
    BMC_SYSPWROK,    // PIN_SYS_PWR_OK
    BMC_PRDY_N,      // PIN_PRDY
    BMC_TCK_MUX_SEL, // PIN_TCK_MUX_SELECT
};

#define NUM_DBUS_FDS 1
typedef struct pollfd target_fdarr_t[NUM_GPIOS + NUM_DBUS_FDS];

typedef STATUS (*TargetHandlerEventFunctionPtr)(void*, ASD_EVENT*);

#define ALL_PIN_TYPES(FUNC)                                                    \
    FUNC(PIN_NONE)                                                             \
    FUNC(PIN_GPIO)                                                             \
    FUNC(PIN_DBUS)                                                             \
    FUNC(PIN_GPIOD)

typedef enum
{
    ALL_PIN_TYPES(TO_ENUM)
} Pin_Type;

static const char* PIN_TYPE_STRINGS[] = {ALL_PIN_TYPES(TO_STRING)};

typedef struct Target_Control_GPIO
{
    char name[30];
    int number;
    TargetHandlerEventFunctionPtr handler;
    int fd;
    GPIO_DIRECTION direction;
    GPIO_EDGE edge;
    struct gpiod_line* line;
    struct gpiod_chip* chip;
    bool active_low;
    Pin_Type type;
} Target_Control_GPIO;

typedef struct Target_Control_Handle
{
    event_configuration event_cfg;
    bool initialized;
    Target_Control_GPIO gpios[NUM_GPIOS];
    Dbus_Handle* dbus;
    bool is_master_probe;
} Target_Control_Handle;

typedef struct data_json_map
{
    const char* fname_json;
    char ftype;
    size_t offset;
    const char* (*enum_strings)[];
    int arr_size;
} data_json_map;

Target_Control_Handle* TargetHandler();
STATUS target_initialize(Target_Control_Handle* state);
STATUS target_deinitialize(Target_Control_Handle* state);
STATUS target_write(Target_Control_Handle* state, Pin pin, bool assert);
STATUS target_read(Target_Control_Handle* state, Pin pin, bool* asserted);
STATUS target_write_event_config(Target_Control_Handle* state,
                                 WriteConfig event_cfg, bool enable);
STATUS target_wait_PRDY(Target_Control_Handle* state, uint8_t log2time);
STATUS target_get_fds(Target_Control_Handle* state, target_fdarr_t* fds,
                      int* num_fds);
STATUS target_event(Target_Control_Handle* state, struct pollfd poll_fd,
                    ASD_EVENT* event);
STATUS target_wait_sync(Target_Control_Handle* state, uint16_t timeout,
                        uint16_t delay);
STATUS on_power_event(Target_Control_Handle* state, ASD_EVENT* event);
STATUS initialize_powergood_pin_handler(Target_Control_Handle* state);
STATUS target_get_i2c_config(i2c_options* i2c);
#endif // _TARGET_CONTROL_HANDLER_H_
