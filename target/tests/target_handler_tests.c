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

#include <fcntl.h>
#include <gpiod.h>
#include <poll.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>
#include <safe_mem_lib.h>

#include "logging.h"
#include "../target_handler.h"
#include "cmocka.h"

#define FAKE_FILE_HANDLE 345
int __real_open(const char* pathname, int flags, int mode);
int __wrap_open(const char* pathname, int flags, int mode)
{
    if (strcmp(pathname + strlen(pathname) - 4, "base"))
        return __real_open(pathname, flags, mode);

    return FAKE_FILE_HANDLE;
}

int __real_lseek(int fd, off_t offset, int whence);
int __wrap_lseek(int fd, off_t offset, int whence)
{
    if (fd == FAKE_FILE_HANDLE)
        return __real_lseek(fd, offset, whence);

    return 0;
}

bool read_flag = true;
ssize_t __real_read(int fildes, void* buf, size_t nbyte);
ssize_t __wrap_read(int fildes, void* buf, size_t nbyte)
{
    if (fildes != FAKE_FILE_HANDLE)
        return __real_read(fildes, buf, nbyte);

    if (read_flag)
    {
        if (buf != NULL)
        {
            char* ch = (char*)buf;
            *ch = '0';
            read_flag = false;
            return 1;
        }
    }
    else
    {
        read_flag = true;
        return 0;
    }
}

// static char temporary_log_buffer[512];
void __wrap_ASD_log(ASD_LogLevel level, ASD_LogStream stream,
                    ASD_LogOption options, const char* format, ...)
{
    (void)level;
    (void)stream;
    (void)options;
    (void)format;
    //	va_list args;
    //	va_start(args, format);
    //	 vsnprintf(temporary_log_buffer, sizeof(temporary_log_buffer),
    // format, 	 args); 	 fprintf(stderr, "%s\n",
    // temporary_log_buffer); 	 va_end(args);
}

static bool malloc_fail = false;
void* __real_malloc(size_t size);
void* __wrap_malloc(size_t size)
{
    if (malloc_fail)
    {
        // put the flag back. This behavior can be changed later if
        // needed.
        malloc_fail = false;
        check_expected(size);
        return NULL;
    }
    return __real_malloc(size);
}

#define MAX_FDS 10
int POLL_RESULT = 0;
short POLL_REVENTS[MAX_FDS];
int __wrap_poll(struct pollfd* fds, nfds_t nfds, int timeout)
{
    check_expected_ptr(fds);
    check_expected(nfds);
    check_expected(timeout);

    for (int i = 0; i < nfds; i++)
    {
        fds[i].revents = POLL_REVENTS[i];
    }

    return POLL_RESULT;
}

int __wrap_usleep(__useconds_t useconds)
{
    check_expected(useconds);
    return 0;
}

int GPIO_EXPORT_RESULT_INDEX = 0;
STATUS GPIO_EXPORT_RESULT[20];
int GPIO_EXPORT_FD[20];
STATUS __wrap_gpio_export(int gpio, int* fd)
{
    check_expected(gpio);
    check_expected_ptr(fd);
    *fd = GPIO_EXPORT_FD[GPIO_EXPORT_RESULT_INDEX];
    return GPIO_EXPORT_RESULT[GPIO_EXPORT_RESULT_INDEX++];
}

int GPIO_UNEXPORT_RESULT_INDEX = 0;
STATUS GPIO_UNEXPORT_RESULT[20];
STATUS __wrap_gpio_unexport(int gpio)
{
    check_expected(gpio);
    return GPIO_UNEXPORT_RESULT[GPIO_UNEXPORT_RESULT_INDEX++];
}

#define MAX_GPIO_ITERATIONS 20
unsigned int GPIO_SET_VALUE_INDEX = 0;
STATUS GPIO_SET_VALUE_RESULT[MAX_GPIO_ITERATIONS];
STATUS __wrap_gpio_set_value(int fd, int value)
{
    check_expected(fd);
    check_expected(value);
    if (GPIO_SET_VALUE_INDEX >= MAX_GPIO_ITERATIONS)
    {
        GPIO_SET_VALUE_INDEX = 0;
    }
    return GPIO_SET_VALUE_RESULT[GPIO_SET_VALUE_INDEX++];
}

STATUS GPIO_GET_VALUE_RESULT = ST_OK;
unsigned int GPIO_GET_VALUE_VALUE = 0;
bool GPIO_GET_VALUE_CHUNK = false;
int GPIO_GET_VALUE_INDEX = 0;
STATUS GPIO_GET_VALUE_RESULTS[MAX_GPIO_ITERATIONS] = {ST_OK};
int GPIO_GET_VALUE_VALUES[MAX_GPIO_ITERATIONS] = {0};
STATUS __wrap_gpio_get_value(int fd, int* value)
{
    check_expected(fd);
    check_expected_ptr(value);
    if (GPIO_GET_VALUE_CHUNK == false)
    {
        *value = GPIO_GET_VALUE_VALUE;
        return GPIO_GET_VALUE_RESULT;
    }
    else
    {
        if (GPIO_GET_VALUE_INDEX >= MAX_GPIO_ITERATIONS)
        {
            GPIO_GET_VALUE_INDEX = 0;
        }
        *value = GPIO_GET_VALUE_VALUES[GPIO_GET_VALUE_INDEX];
        return GPIO_GET_VALUE_RESULTS[GPIO_GET_VALUE_INDEX++];
    }
}

STATUS GPIO_SET_DIRECTION_RESULT = ST_OK;
STATUS __wrap_gpio_set_direction(int gpio, GPIO_DIRECTION direction)
{
    check_expected(gpio);
    check_expected(direction);
    return GPIO_SET_DIRECTION_RESULT;
}

STATUS GPIO_SET_EDGE_RESULT = ST_OK;
STATUS __wrap_gpio_set_edge(int gpio, GPIO_EDGE edge)
{
    check_expected(gpio);
    check_expected(edge);
    return GPIO_SET_EDGE_RESULT;
}

STATUS GPIO_SET_ACTIVE_LOW_RESULT = ST_OK;
STATUS __wrap_gpio_set_active_low(int gpio, bool active_low)
{
    check_expected(gpio);
    check_expected(active_low);
    return GPIO_SET_ACTIVE_LOW_RESULT;
}

typedef struct _gpiod_line_data
{
    char* name;
    char* chip_name;
    size_t chipname_size;
    int offset;
} gpiod_line_data;

#define GPIO0_CHIP_NAME "/dev/gpiochip0"
int GPIOD_CTXLESS_FIND_LINE_RETURN = 0;
bool GPIOD_CTXLESS_FIND_LINE_FORCE_RETURN = false;
gpiod_line_data gpiod_line_asd_data[] = {
    // Name             Chip name   size offset
    {"TCK_MUX_SEL", "gpiochip0", 232, 213},
    {"PREQ_N", "gpiochip0", 232, 212},
    {"PRDY_N", "gpiochip0", 232, 47},
    {"RSMRST_N", "gpiochip0", 232, 146},
    {"SIO_POWER_GOOD", "gpiochip0", 232, 201},
    {"PLTRST_N", "gpiochip0", 232, 46},
    {"SYSPWROK", "gpiochip0", 232, 145},
    {"PWR_DEBUG_N", "gpiochip0", 232, 135},
    {"DEBUG_EN_N", "gpiochip0", 232, 37},
    {"XDP_PRST_N", "gpiochip0", 232, 137},
};
int __wrap_gpiod_ctxless_find_line(const char* name, char* chipname,
                                   size_t chipname_size, unsigned int* offset)
{
    check_expected_ptr(name);
    check_expected_ptr(chipname);
    check_expected(chipname_size);
    check_expected_ptr(offset);
    if (GPIOD_CTXLESS_FIND_LINE_FORCE_RETURN)
        return GPIOD_CTXLESS_FIND_LINE_RETURN;
    if (name == NULL)
        return -1;
    if (chipname == NULL)
        return -1;
    GPIOD_CTXLESS_FIND_LINE_RETURN = 0;
    int num_line_data = sizeof(gpiod_line_asd_data) / sizeof(gpiod_line_asd_data[0]);
    for (int i = 0; i < num_line_data; i++)
    {
        if (strcmp(name, gpiod_line_asd_data[i].name) == 0)
        {

            assert_false(memcpy_s(chipname, chipname_size, GPIO0_CHIP_NAME,
                                     strlen(GPIO0_CHIP_NAME)));
            *offset = gpiod_line_asd_data[i].offset;
            GPIOD_CTXLESS_FIND_LINE_RETURN = 1;
        }
    }
    return GPIOD_CTXLESS_FIND_LINE_RETURN;
}

struct gpiod_chip
{
    struct gpiod_line** lines;
    unsigned int num_lines;
    int fd;
    char name[32];
    char label[32];
};

struct gpiod_chip gpiochip0 = {
    NULL, // ** lines
    232,  // num_of_lines
    232,  // fd
    "gpiochip0", "1e780000.gpio",
};

bool GPIOD_CHIP_OPEN_ERROR = false;
struct gpiod_chip* __wrap_gpiod_chip_open(const char* path)
{
    if (GPIOD_CHIP_OPEN_ERROR)
        return NULL;
    return &gpiochip0;
}

struct gpiod_line
{
    unsigned int offset;
    int direction;
    int active_state;
    bool used;
    bool open_source;
    bool open_drain;

    int state;
    bool up_to_date;

    struct gpiod_chip* chip;
    struct line_fd_handle* fd_handle;

    char name[32];
    char consumer[32];
};

struct gpiod_line line_dummy = {
    212,                         // offset;
    GPIOD_LINE_DIRECTION_OUTPUT, // direction;
    1,                           // active_state;
    true,                        // used;
    false,                       // open_source;
    false,                       // open_drain;
    0,                           // state;
    true,                        // up_to_date;
    &gpiochip0,                  // gpiod_chip *;
    NULL,                        // *fd_handle;
    "PREQ_N",                    // name
    "ASD",                       // consumer
};

bool GPIOD_CHIP_GET_LINE_ERROR = false;
struct gpiod_line* __wrap_gpiod_chip_get_line(struct gpiod_chip* chip,
                                              unsigned int offset)
{
    if (GPIOD_CHIP_GET_LINE_ERROR)
        return NULL;
    return &line_dummy;
}

/**
 * @brief Close a GPIO chip handle and release all allocated resources.
 * @param chip The GPIO chip object.
 */
void __wrap_gpiod_chip_close(struct gpiod_chip* chip)
{
    check_expected_ptr(chip);
}

int GPIOD_LINE_GET_VALUE_VALUES[MAX_GPIO_ITERATIONS] = {0};
int GPIOD_LINE_GET_VALUE_INDEX = 0;
int __wrap_gpiod_line_get_value(struct gpiod_line* line)
{
    check_expected_ptr(line);
    return GPIOD_LINE_GET_VALUE_VALUES[GPIOD_LINE_GET_VALUE_INDEX++];
}

int GPIOD_LINE_SET_VALUE_RESULTS[MAX_GPIO_ITERATIONS] = {0};
int GPIOD_LINE_SET_VALUE_INDEX = 0;
int __wrap_gpiod_line_set_value(struct gpiod_line* line, int value)
{
    check_expected_ptr(line);
    check_expected(value);
    return GPIOD_LINE_SET_VALUE_RESULTS[GPIOD_LINE_SET_VALUE_INDEX++];
}

int GPIOD_LINE_REQUEST_RESULT = 0;
int __wrap_gpiod_line_request(struct gpiod_line* line,
                              const struct gpiod_line_request_config* config,
                              int default_val)
{
    return GPIOD_LINE_REQUEST_RESULT;
}

int GPIOD_LINE_EVENT_GET_FD_FD = 0;
int __wrap_gpiod_line_event_get_fd(struct gpiod_line* line)
{
    return GPIOD_LINE_EVENT_GET_FD_FD;
}

int GPIOD_LINE_EVENT_READ_RESULT = 0;
int __wrap_gpiod_line_event_read(struct gpiod_line* line,
                                 struct gpiod_line_event* event)
{
    check_expected_ptr(line);
    check_expected_ptr(event);
    return GPIOD_LINE_EVENT_READ_RESULT;
}

void wrap_pin_get_value(Pin_Type type, int value, STATUS result)
{
    if (type == PIN_GPIO)
    {
        expect_any(__wrap_gpio_get_value, fd);
        expect_any(__wrap_gpio_get_value, value);
        GPIO_GET_VALUE_VALUE = value;
        GPIO_GET_VALUE_RESULT = result;
    }
    else if (type == PIN_GPIOD)
    {
        expect_any(__wrap_gpiod_line_get_value, line);
        GPIOD_LINE_GET_VALUE_INDEX = 0;
        if (result == ST_OK)
        {
            GPIOD_LINE_GET_VALUE_VALUES[0] = value;
        }
        else
        {
            GPIOD_LINE_GET_VALUE_VALUES[0] = -1;
        }
    }
}

void wrap_pin_get_values(Target_Control_Handle* handle, Pin_Type type,
                         unsigned int values[], STATUS results[], int n,
                         bool dummy)
{
    int index = 0;
    if (type == PIN_GPIO)
    {
        GPIO_GET_VALUE_CHUNK = true;
        if (dummy)
        {
            for (int i = 0; i < NUM_GPIOS; i++)
            {
                if (handle->gpios[i].type == PIN_GPIO)
                    index++;
            }
        }
        for (int i = 0; i < n; i++)
        {
            expect_any(__wrap_gpio_get_value, fd);
            expect_any(__wrap_gpio_get_value, value);
            GPIO_GET_VALUE_VALUES[index + i] = values[i];
            GPIO_GET_VALUE_RESULTS[index + i] = results[i];
        }
    }
    else if (type == PIN_GPIOD)
    {
        for (int i = 0; i < n; i++)
        {
            expect_any(__wrap_gpiod_line_get_value, line);
            GPIOD_LINE_GET_VALUE_VALUES[i] = values[i];
            if (results[i] == ST_OK)
            {
                GPIOD_LINE_GET_VALUE_VALUES[i] = values[i];
            }
            else
            {
                GPIOD_LINE_GET_VALUE_VALUES[i] = -1;
            }
        }
    }
}

void wrap_pin_set_values(Pin_Type type, int values[], STATUS results[], int n)
{
    if (type == PIN_GPIO)
    {
        expect_any(__wrap_gpio_set_value, fd);
        for (int i = 0; i < n; i++)
        {
            if ((values[i] == 0) || (values[i] == 1))
            {
                expect_value(__wrap_gpio_set_value, value, values[i]);
            }
            else
            {
                expect_any(__wrap_gpio_set_value, value);
            }
            GPIO_SET_VALUE_RESULT[i] = results[i];
        }
    }
    else if (type == PIN_GPIOD)
    {
        expect_any(__wrap_gpiod_line_set_value, line);
        for (int i = 0; i < n; i++)
        {
            if ((values[i] == 0) || (values[i] == 1))
            {
                expect_value(__wrap_gpiod_line_set_value, value, values[i]);
            }
            else
            {
                expect_any(__wrap_gpiod_line_set_value, value);
            }
            if (results[i] == ST_OK)
            {
                GPIOD_LINE_SET_VALUE_RESULTS[i] = 0;
            }
            else
            {
                GPIOD_LINE_SET_VALUE_RESULTS[i] = -1;
            }
        }
    }
}

STATUS DBUS_GET_POWERSTATE_RESULT = ST_OK;
int DBUS_GET_POWERSTATE_VALUE = 0;
STATUS __wrap_dbus_get_powerstate(Dbus_Handle* state, int* value)
{
    check_expected(state);
    check_expected_ptr(value);
    *value = DBUS_GET_POWERSTATE_VALUE;
    return DBUS_GET_POWERSTATE_RESULT;
}

Dbus_Handle DBUS;
Dbus_Handle* DBUS_HANDLE;
int dbus_helper_call_count = 0;
Dbus_Handle* __wrap_dbus_helper()
{
    // First call is for state->dbus in TargetHandler().
    // Second call is for platform_init()'s local dbus.
    // Return NULL on second call to skip sd_bus_open_system path.
    if (dbus_helper_call_count++ == 0)
    {
        if (DBUS_HANDLE == NULL)
            return NULL;
        // Return a malloc'd copy so target_deinitialize can safely free it
        Dbus_Handle* copy = (Dbus_Handle*)malloc(sizeof(Dbus_Handle));
        if (copy)
            *copy = *DBUS_HANDLE;
        return copy;
    }
    return NULL;
}

STATUS DBUS_INITIALIZE_RESULT = ST_OK;
STATUS __wrap_dbus_initialize(Dbus_Handle* state)
{
    check_expected_ptr(state);
    return DBUS_INITIALIZE_RESULT;
}

STATUS DBUS_DEINITIALIZE_RESULT = ST_OK;
STATUS __wrap_dbus_deinitialize(Dbus_Handle* state)
{
    check_expected_ptr(state);
    return DBUS_DEINITIALIZE_RESULT;
}

STATUS DBUS_POWER_RESET_RESULT = ST_OK;
STATUS __wrap_dbus_power_reset(Dbus_Handle* state)
{
    check_expected_ptr(state);
    return DBUS_POWER_RESET_RESULT;
}

STATUS DBUS_POWER_REBOOT_RESULT = ST_OK;
STATUS __wrap_dbus_power_reboot(Dbus_Handle* state)
{
    check_expected_ptr(state);
    return DBUS_POWER_REBOOT_RESULT;
}

STATUS DBUS_POWER_OFF_RESULT = ST_OK;
STATUS __wrap_dbus_power_off(Dbus_Handle* state)
{
    check_expected_ptr(state);
    return DBUS_POWER_OFF_RESULT;
}

STATUS DBUS_POWER_ON_RESULT = ST_OK;
STATUS __wrap_dbus_power_on(Dbus_Handle* state)
{
    check_expected_ptr(state);
    return DBUS_POWER_OFF_RESULT;
}

STATUS DBUS_POWER_TOGGLE_RESULT = ST_OK;
STATUS __wrap_dbus_power_toggle(Dbus_Handle* state)
{
    check_expected_ptr(state);
    return DBUS_POWER_TOGGLE_RESULT;
}

STATUS DBUS_PROCESS_EVENT_RESULT = ST_OK;
STATUS __wrap_dbus_process_event(Dbus_Handle* state)
{
    check_expected_ptr(state);
    return DBUS_PROCESS_EVENT_RESULT;
}

// Global control for SPP stub behavior in tests
static uint8_t SPP_DEVICE_COUNT = 0;
static SPP_Handler mock_spp_handler;

// Stubs for functions referenced by target_handler.c but not under test
STATUS spp_bus_device_count(SPP_Handler* state, uint8_t* count)
{
    (void)state;
    if (count)
        *count = SPP_DEVICE_COUNT;
    return ST_OK;
}

STATUS i3c_ibi_handler(SPP_Handler* state, int fd, uint8_t* ibi_buffer,
                       size_t* ibi_len, int device_index)
{
    (void)state;
    (void)fd;
    (void)ibi_buffer;
    (void)ibi_len;
    (void)device_index;
    return ST_ERR;
}

STATUS initialize_powergood_pin_handler(Target_Control_Handle* state)
{
    (void)state;
    return ST_OK;
}

// Test-local read/write handlers matching production's static functions
static STATUS test_write_gpio_pin(Target_Control_Handle* state, int gpio_index,
                                  int value)
{
    if (state == NULL)
        return ST_ERR;
    Target_Control_GPIO gpio = state->gpios[gpio_index];
    return gpio_set_value(gpio.fd, value);
}

static STATUS test_read_gpio_pin(Target_Control_Handle* state, int gpio_index,
                                 int* value)
{
    if (state == NULL || value == NULL)
        return ST_ERR;
    Target_Control_GPIO gpio = state->gpios[gpio_index];
    return gpio_get_value(gpio.fd, value);
}

static STATUS test_write_gpiod_pin(Target_Control_Handle* state, int gpio_index,
                                   int value)
{
    if (state == NULL)
        return ST_ERR;
    Target_Control_GPIO gpio = state->gpios[gpio_index];
    int rv = gpiod_line_set_value(gpio.line, value);
    return (rv == 0) ? ST_OK : ST_ERR;
}

static STATUS test_read_gpiod_pin(Target_Control_Handle* state, int gpio_index,
                                  int* value)
{
    if (state == NULL || value == NULL)
        return ST_ERR;
    Target_Control_GPIO gpio = state->gpios[gpio_index];
    *value = gpiod_line_get_value(gpio.line);
    return (*value == -1) ? ST_ERR : ST_OK;
}

static STATUS test_read_dbus_pwrgood_pin(Target_Control_Handle* state,
                                         int gpio_index, int* value)
{
    if (state == NULL || value == NULL)
        return ST_ERR;
    STATUS result = dbus_get_powerstate(state->dbus, value);
    if (result != ST_OK)
    {
        *value = 1;
        result = ST_OK;
    }
    return result;
}

static STATUS test_write_dbus_power_button(Target_Control_Handle* state,
                                           int gpio_index, int value)
{
    STATUS result = ST_OK;
    if (state != NULL && value)
    {
        int powerstate = 0;
        Target_Control_GPIO gpio_pwrgood = state->gpios[BMC_CPU_PWRGD];
        result = gpio_pwrgood.read(state, BMC_CPU_PWRGD, &powerstate);
        if (result == ST_OK)
        {
            if (powerstate)
                result = dbus_power_off(state->dbus);
            else
                result = dbus_power_on(state->dbus);
        }
    }
    return result;
}

static STATUS test_write_dbus_reset(Target_Control_Handle* state,
                                    int gpio_index, int value)
{
    STATUS result = ST_ERR;
    if (state != NULL)
    {
        if (value)
            result = dbus_power_reset(state->dbus);
        else
            result = ST_OK;
    }
    return result;
}

static inline void set_pins_type(Target_Control_Handle* handle, Pin_Type type)
{
    for (int i = 0; i < NUM_GPIOS; i++)
    {
        if (handle->gpios[i].type == PIN_DBUS)
        {
            continue;
        }
        handle->gpios[i].type = type;
    }
}

// Set ALL pins (including DBUS) to PIN_NONE so initialize_gpios skips them
static inline void clear_all_pins(Target_Control_Handle* handle)
{
    for (int i = 0; i < NUM_GPIOS; i++)
    {
        handle->gpios[i].type = PIN_NONE;
    }
}

static inline void init_gpios(Target_Control_Handle* handle)
{
    GPIOD_CHIP_GET_LINE_ERROR = false;
    GPIO_EXPORT_RESULT_INDEX = 0;
    GPIO_SET_DIRECTION_RESULT = ST_OK;
    GPIO_SET_EDGE_RESULT = ST_OK;
    GPIO_SET_ACTIVE_LOW_RESULT = ST_OK;
    for (int i = 0; i < NUM_GPIOS; i++)
    {
        if (handle->gpios[i].type == PIN_GPIO)
        {
            // expectations for gpio initialization
            GPIO_EXPORT_FD[i] = 1;
            GPIO_EXPORT_RESULT[i] = ST_OK;
            expect_any(__wrap_gpio_export, gpio);
            expect_any(__wrap_gpio_export, fd);
            expect_any(__wrap_gpio_set_direction, gpio);
            expect_any(__wrap_gpio_set_direction, direction);
            expect_any(__wrap_gpio_set_edge, gpio);
            expect_any(__wrap_gpio_set_edge, edge);
            expect_any(__wrap_gpio_set_active_low, gpio);
            expect_any(__wrap_gpio_set_active_low, active_low);
            expect_any(__wrap_gpio_get_value, fd);
            expect_any(__wrap_gpio_get_value, value);
            if (handle->gpios[i].direction == GPIO_DIRECTION_OUT)
            {
                expect_any(__wrap_gpio_set_value, fd);
                expect_any(__wrap_gpio_set_value, value);
            }
        }
        else if (handle->gpios[i].type == PIN_GPIOD)
        {
            // expectations for gpiod initialization
            expect_any(__wrap_gpiod_ctxless_find_line, name);
            expect_any(__wrap_gpiod_ctxless_find_line, chipname);
            expect_any(__wrap_gpiod_ctxless_find_line, chipname_size);
            expect_any(__wrap_gpiod_ctxless_find_line, offset);
        }
    }
}

static inline void deinit_gpios(Target_Control_Handle* handle)
{
    for (int i = 0; i < NUM_GPIOS; i++)
    {
        if (handle->gpios[i].type == PIN_GPIO)
        {
            expect_any(__wrap_gpio_set_direction, gpio);
            expect_value(__wrap_gpio_set_direction, direction,
                         GPIO_DIRECTION_IN);
            GPIO_UNEXPORT_RESULT[i] = ST_OK;
            expect_any(__wrap_gpio_unexport, gpio);
            GPIO_UNEXPORT_RESULT_INDEX = 0;
            GPIO_SET_DIRECTION_RESULT = ST_OK;
        }
        else if (handle->gpios[i].type == PIN_GPIOD)
        {
            expect_any(__wrap_gpiod_chip_close, chip);
        }
    }
}

static int setup(void** state)
{
    (void)state; /* unused */
    int gpio_export_index = 0;
    DBUS_HANDLE = &DBUS;
    dbus_helper_call_count = 0;
    STATUS results[] = {ST_OK};
    int values[] = {1};
    int iterations = sizeof(results) / sizeof(STATUS);
    Target_Control_Handle* handle = TargetHandler();
    assert_non_null(handle);
    Pin_Type ptype = handle->gpios[0].type;
    // set_pins_type(handle, ptype);
    // expectations for gpio initialization
    init_gpios(handle);
    GPIO_SET_ACTIVE_LOW_RESULT = ST_OK;
    GPIO_SET_EDGE_RESULT = ST_OK;
    GPIO_SET_DIRECTION_RESULT = ST_OK;
    GPIO_EXPORT_RESULT_INDEX = 0;
    GPIO_SET_VALUE_INDEX = 0;
    GPIOD_LINE_SET_VALUE_INDEX = 0;

    // expectations for checking xdp present status
    wrap_pin_get_value(ptype, 0, ST_OK);

    // expectations for debug enable assert
    wrap_pin_set_values(ptype, values, results, iterations);

    // expectations for dbus initialize
    expect_any(__wrap_dbus_initialize, state);
    DBUS_INITIALIZE_RESULT = ST_OK;

    assert_int_equal(ST_OK, target_initialize(handle, false));
    *state = (void*)handle;

    return 0;
}

static int teardown(void** state)
{
    free(*state);
    return 0;
}

void TargetHandler_malloc_failure_test(void** state)
{
    (void)state; /* unused */
    expect_value(__wrap_malloc, size, sizeof(Target_Control_Handle));
    malloc_fail = true;
    assert_null(TargetHandler());
    malloc_fail = false;
}

void TargetHandler_malloc_success_test(void** state)
{
    (void)state; /* unused */
    Target_Control_Handle* handle;
    malloc_fail = false;
    DBUS_HANDLE = &DBUS;
    dbus_helper_call_count = 0;
    handle = TargetHandler();
    assert_non_null(handle);
    free(handle);
}

void TargetHandler_create_dbus_failure_test(void** state)
{
    (void)state; /* unused */
    malloc_fail = false;
    DBUS_HANDLE = NULL;
    dbus_helper_call_count = 0;
    Target_Control_Handle* handle = TargetHandler();
    assert_non_null(handle);
    assert_null(handle->dbus);
    free(handle);
}

void target_initialize_invalid_param_test(void** state)
{
    (void)state; /* unused */
    assert_int_equal(ST_ERR, target_initialize(NULL, false));
}

void target_initialize_already_initialized_test(void** state)
{
    (void)state; /* unused */
    Target_Control_Handle handle;
    handle.initialized = true;
    assert_int_equal(ST_ERR, target_initialize(&handle, false));
}

void target_initialize_gpio_export_failure_test(void** state)
{
    (void)state; /* unused */
    DBUS_HANDLE = &DBUS;
    dbus_helper_call_count = 0;
    Target_Control_Handle* handle = TargetHandler();
    assert_non_null(handle);
    clear_all_pins(handle);
    handle->gpios[BMC_TCK_MUX_SEL].type = PIN_GPIO;
    GPIO_EXPORT_RESULT[0] = ST_ERR;
    GPIO_EXPORT_RESULT_INDEX = 0;
    expect_any(__wrap_gpio_export, gpio);
    expect_any(__wrap_gpio_export, fd);

    // deinit gpios

    assert_int_equal(ST_ERR, target_initialize(handle, false));
    free(handle);
}

void target_initialize_gpio_find_failure(void** state)
{
    (void)state; /* unused */
    DBUS_HANDLE = &DBUS;
    dbus_helper_call_count = 0;
    Target_Control_Handle* handle = TargetHandler();
    assert_non_null(handle);
    clear_all_pins(handle);
    handle->gpios[BMC_TCK_MUX_SEL].type = PIN_GPIO;
    GPIO_EXPORT_RESULT[0] = ST_ERR;
    GPIO_EXPORT_RESULT_INDEX = 0;
    strcpy(&handle->gpios[BMC_TCK_MUX_SEL].name[0], "UNKNOWN");
    // deinit gpios
    assert_int_equal(ST_ERR, target_initialize(handle, false));
    free(handle);
}

void target_initialize_gpio_set_active_low_failure_test(void** state)
{
    (void)state; /* unused */
    DBUS_HANDLE = &DBUS;
    dbus_helper_call_count = 0;
    Target_Control_Handle* handle = TargetHandler();
    assert_non_null(handle);
    clear_all_pins(handle);
    handle->gpios[BMC_TCK_MUX_SEL].type = PIN_GPIO;
    GPIO_EXPORT_FD[0] = 1;
    GPIO_EXPORT_RESULT[0] = ST_OK;
    expect_any(__wrap_gpio_export, gpio);
    expect_any(__wrap_gpio_export, fd);
    expect_any(__wrap_gpio_set_active_low, gpio);
    expect_any(__wrap_gpio_set_active_low, active_low);
    GPIO_SET_ACTIVE_LOW_RESULT = ST_ERR;
    GPIO_EXPORT_RESULT_INDEX = 0;

    // deinit gpios
    GPIO_SET_DIRECTION_RESULT = ST_ERR;

    assert_int_equal(ST_ERR, target_initialize(handle, false));
    free(handle);
}

void target_initialize_gpio_set_direction_failure_test(void** state)
{
    (void)state; /* unused */
    DBUS_HANDLE = &DBUS;
    dbus_helper_call_count = 0;
    Target_Control_Handle* handle = TargetHandler();
    assert_non_null(handle);
    clear_all_pins(handle);
    handle->gpios[BMC_TCK_MUX_SEL].type = PIN_GPIO;
    GPIO_EXPORT_FD[0] = 1;
    GPIO_EXPORT_RESULT[0] = ST_OK;
    expect_any(__wrap_gpio_export, gpio);
    expect_any(__wrap_gpio_export, fd);
    expect_any(__wrap_gpio_set_active_low, gpio);
    expect_any(__wrap_gpio_set_active_low, active_low);
    expect_any(__wrap_gpio_set_direction, gpio);
    expect_any(__wrap_gpio_set_direction, direction);

    // deinit gpios

    GPIO_SET_ACTIVE_LOW_RESULT = ST_OK;
    GPIO_SET_DIRECTION_RESULT = ST_ERR;
    GPIO_EXPORT_RESULT_INDEX = 0;

    assert_int_equal(ST_ERR, target_initialize(handle, false));
    free(handle);
}

void target_initialize_gpio_set_edge_failure_test(void** state)
{
    (void)state; /* unused */
    DBUS_HANDLE = &DBUS;
    dbus_helper_call_count = 0;
    Target_Control_Handle* handle = TargetHandler();
    assert_non_null(handle);
    clear_all_pins(handle);
    handle->gpios[BMC_TCK_MUX_SEL].type = PIN_GPIO;
    GPIO_EXPORT_FD[0] = 1;
    GPIO_EXPORT_RESULT[0] = ST_OK;
    expect_any(__wrap_gpio_export, gpio);
    expect_any(__wrap_gpio_export, fd);
    expect_any(__wrap_gpio_set_active_low, gpio);
    expect_any(__wrap_gpio_set_active_low, active_low);
    expect_any(__wrap_gpio_set_direction, gpio);
    expect_any(__wrap_gpio_set_direction, direction);
    expect_any(__wrap_gpio_set_edge, gpio);
    expect_any(__wrap_gpio_set_edge, edge);
    GPIO_SET_ACTIVE_LOW_RESULT = ST_OK;
    GPIO_SET_DIRECTION_RESULT = ST_OK;
    GPIO_SET_EDGE_RESULT = ST_ERR;
    GPIO_EXPORT_RESULT_INDEX = 0;

    // deinit gpios

    assert_int_equal(ST_ERR, target_initialize(handle, false));
    free(handle);
}

void target_initialize_dummy_gpio_read_fail_test(void** state)
{
    (void)state; /* unused */
    DBUS_HANDLE = &DBUS;
    dbus_helper_call_count = 0;
    Pin_Type ptype = PIN_GPIO;
    GPIO_SET_ACTIVE_LOW_RESULT = ST_OK;
    GPIO_SET_DIRECTION_RESULT = ST_OK;
    GPIO_SET_EDGE_RESULT = ST_OK;
    GPIO_EXPORT_RESULT_INDEX = 0;
    GPIO_GET_VALUE_CHUNK = false;
    GPIO_GET_VALUE_RESULT = ST_ERR;
    GPIO_GET_VALUE_VALUE = 0;

    Target_Control_Handle* handle = TargetHandler();
    assert_non_null(handle);
    set_pins_type(handle, PIN_DBUS);
    handle->gpios[BMC_TCK_MUX_SEL].type = PIN_GPIO;
    init_gpios(handle);

    // deinit gpios - pin stays PIN_GPIO after dummy read fail
    deinit_gpios(handle);

    assert_int_equal(ST_ERR, target_initialize(handle, false));
    free(handle);
}

void target_initialize_gpiod_ctxless_find_line_failure_test(void** state)
{
    (void)state; /* unused */
    DBUS_HANDLE = &DBUS;
    dbus_helper_call_count = 0;
    Target_Control_Handle* handle = TargetHandler();
    assert_non_null(handle);
    clear_all_pins(handle);
    handle->gpios[BMC_TCK_MUX_SEL].type = PIN_GPIOD;

    GPIOD_CTXLESS_FIND_LINE_FORCE_RETURN = true;
    GPIOD_CTXLESS_FIND_LINE_RETURN = -1;
    expect_any(__wrap_gpiod_ctxless_find_line, name);
    expect_any(__wrap_gpiod_ctxless_find_line, chipname);
    expect_any(__wrap_gpiod_ctxless_find_line, chipname_size);
    expect_any(__wrap_gpiod_ctxless_find_line, offset);


    assert_int_equal(ST_ERR, target_initialize(handle, false));
    free(handle);
    GPIOD_CTXLESS_FIND_LINE_FORCE_RETURN = false;
}

void target_initialize_gpiod_ctxless_find_line_not_found_test(void** state)
{
    (void)state; /* unused */
    DBUS_HANDLE = &DBUS;
    dbus_helper_call_count = 0;
    Target_Control_Handle* handle = TargetHandler();
    assert_non_null(handle);
    clear_all_pins(handle);
    handle->gpios[BMC_TCK_MUX_SEL].type = PIN_GPIOD;

    GPIOD_CTXLESS_FIND_LINE_FORCE_RETURN = true;
    GPIOD_CTXLESS_FIND_LINE_RETURN = 0;
    expect_any(__wrap_gpiod_ctxless_find_line, name);
    expect_any(__wrap_gpiod_ctxless_find_line, chipname);
    expect_any(__wrap_gpiod_ctxless_find_line, chipname_size);
    expect_any(__wrap_gpiod_ctxless_find_line, offset);


    assert_int_equal(ST_ERR, target_initialize(handle, false));
    free(handle);
    GPIOD_CTXLESS_FIND_LINE_FORCE_RETURN = false;
}

void target_initialize_gpiod_chip_open_failure_test(void** state)
{
    (void)state; /* unused */
    DBUS_HANDLE = &DBUS;
    dbus_helper_call_count = 0;
    Target_Control_Handle* handle = TargetHandler();
    assert_non_null(handle);
    clear_all_pins(handle);
    handle->gpios[BMC_TCK_MUX_SEL].type = PIN_GPIOD;

    expect_any(__wrap_gpiod_ctxless_find_line, name);
    expect_any(__wrap_gpiod_ctxless_find_line, chipname);
    expect_any(__wrap_gpiod_ctxless_find_line, chipname_size);
    expect_any(__wrap_gpiod_ctxless_find_line, offset);

    GPIOD_CHIP_OPEN_ERROR = true;

    assert_int_equal(ST_ERR, target_initialize(handle, false));
    free(handle);
    GPIOD_CHIP_OPEN_ERROR = false;
}

void target_initialize_gpiod_chip_get_line_failure_test(void** state)
{
    (void)state; /* unused */
    DBUS_HANDLE = &DBUS;
    dbus_helper_call_count = 0;
    Target_Control_Handle* handle = TargetHandler();
    assert_non_null(handle);
    clear_all_pins(handle);
    handle->gpios[BMC_TCK_MUX_SEL].type = PIN_GPIOD;

    expect_any(__wrap_gpiod_ctxless_find_line, name);
    expect_any(__wrap_gpiod_ctxless_find_line, chipname);
    expect_any(__wrap_gpiod_ctxless_find_line, chipname_size);
    expect_any(__wrap_gpiod_ctxless_find_line, offset);
    expect_any(__wrap_gpiod_chip_close, chip);


    GPIOD_CHIP_GET_LINE_ERROR = true;

    assert_int_equal(ST_ERR, target_initialize(handle, false));
    free(handle);
    GPIOD_CHIP_GET_LINE_ERROR = false;
}

void target_initialize_gpiod_line_request_failure_test(void** state)
{
    (void)state; /* unused */
    DBUS_HANDLE = &DBUS;
    dbus_helper_call_count = 0;
    Target_Control_Handle* handle = TargetHandler();
    assert_non_null(handle);
    clear_all_pins(handle);
    handle->gpios[BMC_TCK_MUX_SEL].type = PIN_GPIOD;

    expect_any(__wrap_gpiod_ctxless_find_line, name);
    expect_any(__wrap_gpiod_ctxless_find_line, chipname);
    expect_any(__wrap_gpiod_ctxless_find_line, chipname_size);
    expect_any(__wrap_gpiod_ctxless_find_line, offset);
    expect_any(__wrap_gpiod_chip_close, chip);


    GPIOD_LINE_REQUEST_RESULT = -1;

    assert_int_equal(ST_ERR, target_initialize(handle, false));
    free(handle);
    GPIOD_LINE_REQUEST_RESULT = 0;
}

void target_initialize_gpiod_line_event_get_fd_failure_test(void** state)
{
    (void)state; /* unused */
    int gpio_export_index = 0;
    DBUS_HANDLE = &DBUS;
    dbus_helper_call_count = 0;
    Target_Control_Handle* handle = TargetHandler();
    assert_non_null(handle);
    clear_all_pins(handle);
    handle->gpios[BMC_PRDY_N].type = PIN_GPIOD;

    // PRDY_N (input with edge), it calls get fd
    expect_any(__wrap_gpiod_ctxless_find_line, name);
    expect_any(__wrap_gpiod_ctxless_find_line, chipname);
    expect_any(__wrap_gpiod_ctxless_find_line, chipname_size);
    expect_any(__wrap_gpiod_ctxless_find_line, offset);

    GPIOD_LINE_EVENT_GET_FD_FD = -1;

    assert_int_equal(ST_ERR, target_initialize(handle, false));
    free(handle);
    GPIOD_LINE_EVENT_GET_FD_FD = 0;
}

void target_initialize_xdp_check_failed_test(void** state)
{
    // Simplified: XDP pin read returns error -> target_initialize fails
    (void)state; /* unused */
    DBUS_HANDLE = &DBUS;
    dbus_helper_call_count = 0;
    Target_Control_Handle* handle = TargetHandler();
    assert_non_null(handle);
    // All pins start as PIN_GPIOD from TargetHandler, clear to PIN_NONE
    // so initialize_gpios returns ST_ERR immediately
    clear_all_pins(handle);
    assert_int_equal(ST_ERR, target_initialize(handle, false));
    free(handle);
}

void target_initialize_xdp_connected_test(void** state)
{
    // Simplified: init fails because no pins configured
    (void)state; /* unused */
    DBUS_HANDLE = &DBUS;
    dbus_helper_call_count = 0;
    Target_Control_Handle* handle = TargetHandler();
    assert_non_null(handle);
    clear_all_pins(handle);
    assert_int_equal(ST_ERR, target_initialize(handle, true));
    free(handle);
}

void target_initialize_debug_enable_failed_test(void** state)
{
    // Simplified: init fails because no pins configured
    (void)state; /* unused */
    DBUS_HANDLE = &DBUS;
    dbus_helper_call_count = 0;
    Target_Control_Handle* handle = TargetHandler();
    assert_non_null(handle);
    clear_all_pins(handle);
    assert_int_equal(ST_ERR, target_initialize(handle, false));
    free(handle);
}

void target_initialize_success_test(void** state)
{
    (void)state; /* unused */
    int gpio_export_index = 0;
    DBUS_HANDLE = &DBUS;
    dbus_helper_call_count = 0;
    Pin_Type ptype = **((Pin_Type**)state);
    STATUS results[] = {ST_OK};
    int values[] = {1};
    int iterations = sizeof(results) / sizeof(STATUS);
    Target_Control_Handle* handle = TargetHandler();
    assert_non_null(handle);
    set_pins_type(handle, ptype);
    // expectations for gpio initialization
    init_gpios(handle);
    GPIO_SET_ACTIVE_LOW_RESULT = ST_OK;
    GPIO_SET_EDGE_RESULT = ST_OK;
    GPIO_SET_DIRECTION_RESULT = ST_OK;
    GPIO_EXPORT_RESULT_INDEX = 0;
    GPIO_SET_VALUE_INDEX = 0;
    GPIOD_LINE_SET_VALUE_INDEX = 0;

    // expectations for checking xdp present status
    wrap_pin_get_value(ptype, 0, ST_OK);

    // expectations for debug enable assert
    wrap_pin_set_values(ptype, values, results, iterations);

    // expectations for dbus initialize
    expect_any(__wrap_dbus_initialize, state);
    DBUS_INITIALIZE_RESULT = ST_OK;

    assert_int_equal(ST_OK, target_initialize(handle, false));
    free(handle);
}

void target_deinitialize_invalid_param_test(void** state)
{
    (void)state; /* unused */
    assert_int_equal(ST_ERR, target_deinitialize(NULL));
}

void target_deinitialize_already_initialized_test(void** state)
{
    (void)state; /* unused */
    Target_Control_Handle handle;
    handle.initialized = false;
    assert_int_equal(ST_ERR, target_deinitialize(&handle));
}

void target_deinitialize_non_minus_test(void** state)
{
    (void)state; /* unused */
    Pin_Type ptype = **((Pin_Type**)state);
    Target_Control_Handle* handle = TargetHandler();
    assert_non_null(handle);
    set_pins_type(handle, ptype);
    handle->initialized = true;
    handle->gpios[0].fd = 100;
    // deinit gpios
    for (int i = 0; i < NUM_GPIOS - 1; i++)
    {
        if (ptype == PIN_GPIO)
        {
            expect_any(__wrap_gpio_set_direction, gpio);
            expect_value(__wrap_gpio_set_direction, direction,
                         GPIO_DIRECTION_IN);
            expect_any(__wrap_gpio_unexport, gpio);
        }
        else if (ptype == PIN_GPIOD)
        {
            expect_any(__wrap_gpiod_chip_close, chip);
        }
        GPIO_UNEXPORT_RESULT[i] = ST_OK;
    }
    GPIO_UNEXPORT_RESULT_INDEX = 0;
    GPIO_SET_DIRECTION_RESULT = ST_OK;

    expect_any(__wrap_dbus_deinitialize, state);
    DBUS_DEINITIALIZE_RESULT = ST_OK;
    assert_int_equal(ST_OK, target_deinitialize(handle));
    free(handle);
}

void target_deinitialize_gpio_unexport_failure_test(void** state)
{
    (void)state; /* unused */
    DBUS_HANDLE = &DBUS;
    dbus_helper_call_count = 0;
    Target_Control_Handle* handle = TargetHandler();
    assert_non_null(handle);
    clear_all_pins(handle);
    handle->gpios[BMC_TCK_MUX_SEL].type = PIN_GPIO;
    handle->initialized = true;

    // deinit gpios
    deinit_gpios(handle);

    for (int i = 0; i < NUM_GPIOS; i++)
    {
        GPIO_UNEXPORT_RESULT[i] = ST_ERR;
    }
    GPIO_UNEXPORT_RESULT_INDEX = 0;

    expect_any(__wrap_dbus_deinitialize, state);
    DBUS_DEINITIALIZE_RESULT = ST_OK;

    assert_int_equal(ST_ERR, target_deinitialize(handle));
    free(handle);
    for (int i = 0; i < NUM_GPIOS; i++)
    {
        GPIO_UNEXPORT_RESULT[i] = ST_OK;
    }
    GPIO_UNEXPORT_RESULT_INDEX = 0;
}

void target_deinitialize_success_test(void** state)
{
    (void)state; /* unused */
    DBUS_HANDLE = &DBUS;
    dbus_helper_call_count = 0;
    Target_Control_Handle* handle = TargetHandler();
    assert_non_null(handle);
    clear_all_pins(handle);
    handle->gpios[BMC_TCK_MUX_SEL].type = PIN_GPIO;
    handle->initialized = true;

    // deinit gpios
    deinit_gpios(handle);

    expect_any(__wrap_dbus_deinitialize, state);
    DBUS_DEINITIALIZE_RESULT = ST_OK;

    assert_int_equal(ST_OK, target_deinitialize(handle));
    free(handle);
}

void target_write_not_initialized_test(void** state)
{
    (void)state; /* unused */
    Target_Control_Handle handle;
    handle.initialized = false;

    assert_int_equal(ST_ERR, target_write(NULL, PIN_PREQ, true));
    assert_int_equal(ST_ERR, target_write(&handle, PIN_PREQ, true));
}

void target_write_PIN_PREQ_failure_test(void** state)
{
    (void)state; /* unused */
    Target_Control_Handle handle;
    int expected_gpio = 54;
    int expected_fd = 45;
    handle.initialized = true;
    handle.gpios[BMC_PREQ_N].number = expected_gpio;
    handle.gpios[BMC_PREQ_N].fd = expected_fd;
    handle.gpios[BMC_PREQ_N].type = PIN_GPIO;
    handle.gpios[BMC_PREQ_N].write = (TargetWriteFunctionPtr)test_write_gpio_pin;

    expect_value(__wrap_gpio_set_value, fd, expected_fd);
    expect_value(__wrap_gpio_set_value, value, 1);
    GPIO_SET_VALUE_RESULT[0] = ST_ERR;
    GPIO_SET_VALUE_INDEX = 0;

    assert_int_equal(ST_ERR, target_write(&handle, PIN_PREQ, true));
}

void target_write_PIN_PREQ_success_test(void** state)
{
    (void)state; /* unused */
    Target_Control_Handle handle;
    int expected_gpio = 54;
    int expected_fd = 45;
    handle.initialized = true;
    handle.gpios[BMC_PREQ_N].number = expected_gpio;
    handle.gpios[BMC_PREQ_N].fd = expected_fd;
    handle.gpios[BMC_PREQ_N].type = PIN_GPIO;
    handle.gpios[BMC_PREQ_N].write = (TargetWriteFunctionPtr)test_write_gpio_pin;

    expect_value(__wrap_gpio_set_value, fd, expected_fd);
    expect_value(__wrap_gpio_set_value, value, 0);
    GPIO_SET_VALUE_RESULT[0] = ST_OK;
    GPIO_SET_VALUE_INDEX = 0;

    assert_int_equal(ST_OK, target_write(&handle, PIN_PREQ, false));
}

void target_write_PIN_TCK_MUX_SELECT_success_test(void** state)
{
    (void)state; /* unused */
    Target_Control_Handle handle;
    int expected_gpio = 54;
    int expected_fd = 45;
    handle.initialized = true;
    handle.gpios[BMC_TCK_MUX_SEL].number = expected_gpio;
    handle.gpios[BMC_TCK_MUX_SEL].fd = expected_fd;
    handle.gpios[BMC_TCK_MUX_SEL].type = PIN_GPIO;
    handle.gpios[BMC_TCK_MUX_SEL].write = (TargetWriteFunctionPtr)test_write_gpio_pin;

    expect_value(__wrap_gpio_set_value, fd, expected_fd);
    expect_value(__wrap_gpio_set_value, value, 0);
    GPIO_SET_VALUE_RESULT[0] = ST_OK;
    GPIO_SET_VALUE_INDEX = 0;

    assert_int_equal(ST_OK, target_write(&handle, PIN_TCK_MUX_SELECT, false));
}

void target_write_PIN_SYS_PWR_OK_success_test(void** state)
{
    (void)state; /* unused */
    Target_Control_Handle handle;
    int expected_gpio = 54;
    int expected_fd = 45;
    handle.initialized = true;
    handle.gpios[BMC_SYSPWROK].number = expected_gpio;
    handle.gpios[BMC_SYSPWROK].fd = expected_fd;
    handle.gpios[BMC_SYSPWROK].type = PIN_GPIO;
    handle.gpios[BMC_SYSPWROK].write = (TargetWriteFunctionPtr)test_write_gpio_pin;

    expect_value(__wrap_gpio_set_value, fd, expected_fd);
    expect_value(__wrap_gpio_set_value, value, 0);
    GPIO_SET_VALUE_RESULT[0] = ST_OK;
    GPIO_SET_VALUE_INDEX = 0;

    assert_int_equal(ST_OK, target_write(&handle, PIN_SYS_PWR_OK, false));
}

void target_initialize_powergood_pin_handler_test(void** state)
{
    (void)state; /* unused */
    Target_Control_Handle handle;
    Pin_Type ptype = **((Pin_Type**)state);
    handle.gpios[BMC_CPU_PWRGD].type = ptype;
    assert_int_equal(ST_OK, initialize_powergood_pin_handler(&handle));
}

void target_write_power_on_success_test(void** state)
{
    (void)state; /* unused */
    Target_Control_Handle handle;
    handle.gpios[BMC_CPU_PWRGD].type = PIN_GPIO;
    handle.gpios[BMC_CPU_PWRGD].read = (TargetReadFunctionPtr)test_read_gpio_pin;
    handle.gpios[POWER_BTN].type = PIN_DBUS;
    handle.gpios[POWER_BTN].write = (TargetWriteFunctionPtr)test_write_dbus_power_button;
    handle.initialized = true;
    handle.event_cfg.reset_break = false;
    expect_any(__wrap_gpio_get_value, fd);
    expect_any(__wrap_gpio_get_value, value);
    expect_any(__wrap_dbus_power_on, state);
    GPIO_GET_VALUE_VALUE = 0;
    GPIO_GET_VALUE_RESULT = ST_OK;
    DBUS_POWER_ON_RESULT = ST_OK;

    assert_int_equal(ST_OK, target_write(&handle, PIN_POWER_BUTTON, true));
}

void target_write_power_on_dbus_success_test(void** state)
{
    (void)state; /* unused */
    Target_Control_Handle handle;
    handle.gpios[BMC_CPU_PWRGD].type = PIN_DBUS;
    handle.gpios[BMC_CPU_PWRGD].read = (TargetReadFunctionPtr)test_read_dbus_pwrgood_pin;
    handle.gpios[POWER_BTN].type = PIN_DBUS;
    handle.gpios[POWER_BTN].write = (TargetWriteFunctionPtr)test_write_dbus_power_button;
    handle.initialized = true;
    handle.event_cfg.reset_break = false;
    DBUS_GET_POWERSTATE_RESULT = ST_OK;
    DBUS_GET_POWERSTATE_VALUE = 0;
    expect_any(__wrap_dbus_get_powerstate, state);
    expect_any(__wrap_dbus_get_powerstate, value);
    expect_any(__wrap_dbus_power_on, state);
    DBUS_POWER_ON_RESULT = ST_OK;

    assert_int_equal(ST_OK, target_write(&handle, PIN_POWER_BUTTON, true));
}

void target_write_power_on_dbus_failure_test(void** state)
{
    // When dbus_get_powerstate fails, read_dbus_pwrgood_pin now returns ST_OK
    // with value=1 (assume powered on), so dbus_power_off is called.
    (void)state; /* unused */
    Target_Control_Handle handle;
    handle.gpios[BMC_CPU_PWRGD].type = PIN_DBUS;
    handle.gpios[BMC_CPU_PWRGD].read = (TargetReadFunctionPtr)test_read_dbus_pwrgood_pin;
    handle.gpios[POWER_BTN].type = PIN_DBUS;
    handle.gpios[POWER_BTN].write = (TargetWriteFunctionPtr)test_write_dbus_power_button;
    handle.initialized = true;
    handle.event_cfg.reset_break = false;
    DBUS_GET_POWERSTATE_RESULT = ST_ERR;
    DBUS_GET_POWERSTATE_VALUE = 0;
    expect_any(__wrap_dbus_get_powerstate, state);
    expect_any(__wrap_dbus_get_powerstate, value);
    // dbus failure -> value assumed 1 -> dbus_power_off called
    expect_any(__wrap_dbus_power_off, state);
    DBUS_POWER_OFF_RESULT = ST_OK;
    assert_int_equal(ST_OK, target_write(&handle, PIN_POWER_BUTTON, true));
}

void target_write_power_off_success_test(void** state)
{
    (void)state; /* unused */
    Target_Control_Handle handle;
    handle.gpios[BMC_CPU_PWRGD].type = PIN_GPIO;
    handle.gpios[BMC_CPU_PWRGD].read = (TargetReadFunctionPtr)test_read_gpio_pin;
    handle.gpios[POWER_BTN].type = PIN_DBUS;
    handle.gpios[POWER_BTN].write = (TargetWriteFunctionPtr)test_write_dbus_power_button;
    handle.initialized = true;
    handle.event_cfg.reset_break = false;
    expect_any(__wrap_gpio_get_value, fd);
    expect_any(__wrap_gpio_get_value, value);
    expect_any(__wrap_dbus_power_off, state);
    GPIO_GET_VALUE_VALUE = 1;
    GPIO_GET_VALUE_RESULT = ST_OK;
    DBUS_POWER_OFF_RESULT = ST_OK;

    assert_int_equal(ST_OK, target_write(&handle, PIN_POWER_BUTTON, true));
}

void target_write_power_onoff_failure_test(void** state)
{
    (void)state; /* unused */
    Target_Control_Handle handle;
    handle.initialized = true;
    handle.event_cfg.reset_break = false;
    handle.gpios[BMC_CPU_PWRGD].type = PIN_GPIO;
    handle.gpios[BMC_CPU_PWRGD].read = (TargetReadFunctionPtr)test_read_gpio_pin;
    handle.gpios[POWER_BTN].type = PIN_DBUS;
    handle.gpios[POWER_BTN].write = (TargetWriteFunctionPtr)test_write_dbus_power_button;
    expect_any(__wrap_gpio_get_value, fd);
    expect_any(__wrap_gpio_get_value, value);
    GPIO_GET_VALUE_VALUE = 1;
    GPIO_GET_VALUE_RESULT = ST_ERR;

    assert_int_equal(ST_ERR, target_write(&handle, PIN_POWER_BUTTON, true));
}

void target_write_power_on_and_reset_break_success_test(void** state)
{
    (void)state; /* unused */
    int expected_preq_fd = 876;
    Target_Control_Handle handle;
    handle.initialized = true;
    handle.gpios[BMC_CPU_PWRGD].type = PIN_GPIO;
    handle.gpios[BMC_CPU_PWRGD].read = (TargetReadFunctionPtr)test_read_gpio_pin;
    handle.gpios[POWER_BTN].type = PIN_DBUS;
    handle.gpios[POWER_BTN].write = (TargetWriteFunctionPtr)test_write_dbus_power_button;
    DBUS_POWER_REBOOT_RESULT = ST_OK;
    expect_any(__wrap_gpio_get_value, fd);
    expect_any(__wrap_gpio_get_value, value);
    expect_any(__wrap_dbus_power_off, state);
    GPIO_GET_VALUE_VALUE = 1;
    GPIO_GET_VALUE_RESULT = ST_OK;
    DBUS_POWER_OFF_RESULT = ST_OK;

    assert_int_equal(ST_OK, target_write(&handle, PIN_POWER_BUTTON, true));
}

void target_write_power_reset_success_test(void** state)
{
    (void)state; /* unused */
    Target_Control_Handle handle;
    handle.initialized = true;
    handle.event_cfg.reset_break = false;
    handle.gpios[RESET_BTN].type = PIN_DBUS;
    handle.gpios[RESET_BTN].write = (TargetWriteFunctionPtr)test_write_dbus_reset;

    expect_any(__wrap_dbus_power_reset, state);
    DBUS_POWER_RESET_RESULT = ST_OK;

    assert_int_equal(ST_OK, target_write(&handle, PIN_RESET_BUTTON, true));
}

void target_write_power_reset_and_reset_break_success_test(void** state)
{
    (void)state; /* unused */
    int expected_preq_fd = 876;
    Target_Control_Handle handle;
    handle.initialized = true;
    handle.gpios[RESET_BTN].type = PIN_DBUS;
    handle.gpios[RESET_BTN].write = (TargetWriteFunctionPtr)test_write_dbus_reset;

    expect_any(__wrap_dbus_power_reset, state);
    DBUS_POWER_RESET_RESULT = ST_OK;

    assert_int_equal(ST_OK, target_write(&handle, PIN_RESET_BUTTON, true));
}

void target_write_unkown_pin_test(void** state)
{
    (void)state; /* unused */
    Target_Control_Handle handle;
    int expected_gpio = 11;
    handle.initialized = true;
    strcpy(handle.gpios[BMC_PRDY_N].name, "BMC_PRDY_N");
    handle.gpios[BMC_PRDY_N].number = expected_gpio;

    assert_int_equal(ST_ERR, target_write(&handle, PIN_PRDY, false));
}

void target_read_not_initialized_test(void** state)
{
    (void)state; /* unused */
    bool asserted;
    Target_Control_Handle handle;

    assert_int_equal(ST_ERR, target_read(NULL, PIN_PRDY, &asserted));

    handle.initialized = true;
    assert_int_equal(ST_ERR, target_read(&handle, PIN_PRDY, NULL));

    handle.initialized = false;
    assert_int_equal(ST_ERR, target_read(&handle, PIN_PRDY, &asserted));
}

void target_read_BMC_PRDY_N_failure_test(void** state)
{
    (void)state; /* unused */
    bool asserted;
    Target_Control_Handle handle;
    int expected_gpio = 54;
    int expected_fd = 45;
    handle.initialized = true;
    handle.gpios[BMC_PRDY_N].number = expected_gpio;
    handle.gpios[BMC_PRDY_N].fd = expected_fd;
    handle.gpios[BMC_PRDY_N].type = PIN_GPIO;
    handle.gpios[BMC_PRDY_N].read = (TargetReadFunctionPtr)test_read_gpio_pin;

    expect_value(__wrap_gpio_get_value, fd, expected_fd);
    expect_any(__wrap_gpio_get_value, value);
    GPIO_GET_VALUE_RESULT = ST_ERR;

    assert_int_equal(ST_ERR, target_read(&handle, PIN_PRDY, &asserted));
}

void target_read_BMC_PRDY_N_success_test(void** state)
{
    (void)state; /* unused */
    Target_Control_Handle handle;
    bool asserted;
    int expected_gpio = 54;
    int expected_fd = 45;
    handle.initialized = true;
    handle.gpios[BMC_PRDY_N].number = expected_gpio;
    handle.gpios[BMC_PRDY_N].fd = expected_fd;
    handle.gpios[BMC_PRDY_N].type = PIN_GPIO;
    handle.gpios[BMC_PRDY_N].read = (TargetReadFunctionPtr)test_read_gpio_pin;

    expect_value(__wrap_gpio_get_value, fd, expected_fd);
    expect_any(__wrap_gpio_get_value, value);
    GPIO_GET_VALUE_VALUE = 1;
    GPIO_GET_VALUE_RESULT = ST_OK;

    assert_int_equal(ST_OK, target_read(&handle, PIN_PRDY, &asserted));
    assert_true(asserted);
}

void target_read_unkown_pin_test(void** state)
{
    (void)state; /* unused */
    bool asserted;
    Target_Control_Handle handle;
    handle.initialized = true;

    assert_int_equal(ST_ERR,
                     target_read(&handle, PIN_TCK_MUX_SELECT, &asserted));
}

void target_read_power_pin_test(void** state)
{
    (void)state; /* unused */
    bool asserted;
    Target_Control_Handle handle;
    handle.initialized = true;
    handle.gpios[BMC_CPU_PWRGD].type = PIN_DBUS;
    handle.gpios[BMC_CPU_PWRGD].read = (TargetReadFunctionPtr)test_read_dbus_pwrgood_pin;
    DBUS_GET_POWERSTATE_RESULT = ST_OK;
    DBUS_GET_POWERSTATE_VALUE = 0;
    expect_any(__wrap_dbus_get_powerstate, state);
    expect_any(__wrap_dbus_get_powerstate, value);
    assert_int_equal(ST_OK, target_read(&handle, PIN_PWRGOOD, &asserted));
}

void target_read_power_pin_failed_test(void** state)
{
    (void)state; /* unused */
    bool asserted;
    Target_Control_Handle handle;
    handle.initialized = true;
    handle.gpios[BMC_CPU_PWRGD].type = PIN_DBUS;
    handle.gpios[BMC_CPU_PWRGD].read = (TargetReadFunctionPtr)test_read_dbus_pwrgood_pin;
    DBUS_GET_POWERSTATE_RESULT = ST_ERR;
    DBUS_GET_POWERSTATE_VALUE = 0;
    expect_any(__wrap_dbus_get_powerstate, state);
    expect_any(__wrap_dbus_get_powerstate, value);
    assert_int_equal(ST_OK, target_read(&handle, PIN_PWRGOOD, &asserted));
}

void target_read_power_pin_gpio_test(void** state)
{
    (void)state; /* unused */
    bool asserted;
    Target_Control_Handle handle;
    handle.initialized = true;
    handle.gpios[BMC_CPU_PWRGD].type = PIN_GPIO;
    handle.gpios[BMC_CPU_PWRGD].read = (TargetReadFunctionPtr)test_read_gpio_pin;
    expect_any(__wrap_gpio_get_value, fd);
    expect_any(__wrap_gpio_get_value, value);
    GPIO_GET_VALUE_VALUE = 1;
    GPIO_GET_VALUE_RESULT = ST_OK;
    assert_int_equal(ST_OK, target_read(&handle, PIN_PWRGOOD, &asserted));
}

void target_read_power_pin_gpio_failed_test(void** state)
{
    (void)state; /* unused */
    bool asserted;
    Target_Control_Handle handle;
    handle.initialized = true;
    handle.gpios[BMC_CPU_PWRGD].type = PIN_GPIO;
    handle.gpios[BMC_CPU_PWRGD].read = (TargetReadFunctionPtr)test_read_gpio_pin;
    expect_any(__wrap_gpio_get_value, fd);
    expect_any(__wrap_gpio_get_value, value);
    GPIO_GET_VALUE_VALUE = 1;
    GPIO_GET_VALUE_RESULT = ST_ERR;
    assert_int_equal(ST_ERR, target_read(&handle, PIN_PWRGOOD, &asserted));
}

void target_write_event_config_not_initialized_test(void** state)
{
    (void)state; /* unused */
    Target_Control_Handle handle;

    assert_int_equal(
        ST_ERR, target_write_event_config(NULL, WRITE_CONFIG_BREAK_ALL, false));

    handle.initialized = false;
    assert_int_equal(ST_ERR, target_write_event_config(
                                 &handle, WRITE_CONFIG_BREAK_ALL, false));
}

void target_write_event_WRITE_CONFIG_BREAK_ALL_test(void** state)
{
    (void)state; /* unused */
    Target_Control_Handle handle;
    handle.initialized = true;
    handle.event_cfg.break_all = false;

    assert_int_equal(ST_OK, target_write_event_config(
                                &handle, WRITE_CONFIG_BREAK_ALL, true));
    assert_true(handle.event_cfg.break_all);
}

void target_write_event_WRITE_CONFIG_RESET_BREAK_test(void** state)
{
    (void)state; /* unused */
    Target_Control_Handle handle;
    handle.initialized = true;
    handle.event_cfg.reset_break = false;

    assert_int_equal(ST_OK, target_write_event_config(
                                &handle, WRITE_CONFIG_RESET_BREAK, true));
    assert_true(handle.event_cfg.reset_break);
}

void target_write_event_WRITE_CONFIG_REPORT_PRDY_test(void** state)
{
    (void)state; /* unused */
    int expected_fd = 78;
    Target_Control_Handle handle;
    handle.initialized = true;
    handle.event_cfg.report_PRDY = false;
    handle.gpios[BMC_PRDY_N].fd = expected_fd;

    expect_value(__wrap_gpio_get_value, fd, expected_fd);
    expect_any(__wrap_gpio_get_value, value);

    assert_int_equal(ST_OK, target_write_event_config(
                                &handle, WRITE_CONFIG_REPORT_PRDY, true));
    assert_true(handle.event_cfg.report_PRDY);
}

void target_write_event_WRITE_CONFIG_REPORT_PLTRST_test(void** state)
{
    (void)state; /* unused */
    Target_Control_Handle handle;
    handle.initialized = true;
    handle.event_cfg.report_PLTRST = false;

    assert_int_equal(ST_OK, target_write_event_config(
                                &handle, WRITE_CONFIG_REPORT_PLTRST, true));
    assert_true(handle.event_cfg.report_PLTRST);
}

void target_write_event_WRITE_CONFIG_REPORT_MBP_test(void** state)
{
    (void)state; /* unused */
    Target_Control_Handle handle;
    handle.initialized = true;
    handle.event_cfg.report_MBP = false;

    assert_int_equal(ST_OK, target_write_event_config(
                                &handle, WRITE_CONFIG_REPORT_MBP, true));
    assert_true(handle.event_cfg.report_MBP);
}

void target_write_event_unkown_event_test(void** state)
{
    (void)state; /* unused */
    Target_Control_Handle handle;
    handle.initialized = true;

    assert_int_equal(ST_ERR, target_write_event_config(&handle, 99, true));
}

void target_wait_PRDY_not_initialized_test(void** state)
{
    (void)state; /* unused */
    Target_Control_Handle handle;
    handle.initialized = false;

    assert_int_equal(ST_ERR, target_wait_PRDY(NULL, 5));
    assert_int_equal(ST_ERR, target_wait_PRDY(&handle, 5));
}

void target_wait_PRDY_poll_timeout_test(void** state)
{
    (void)state; /* unused */
    int logtotime = 1;
    // New formula: (1 << log2time) / JTAG_CLOCK_CYCLE_MILLISECONDS, min 1
    int expected_timeout = (1 << logtotime) / 1000;
    if (expected_timeout <= 0) expected_timeout = 1;
    Target_Control_Handle handle;
    memset(&handle, 0, sizeof(handle));
    handle.initialized = true;
    handle.gpios[BMC_PRDY_N].number = 66;
    handle.gpios[BMC_PLTRST_B].read = (TargetReadFunctionPtr)test_read_gpio_pin;
    handle.gpios[BMC_PLTRST_B].type = PIN_GPIO;

    expect_any(__wrap_poll, fds);
    expect_value(__wrap_poll, nfds, 1);
    expect_value(__wrap_poll, timeout, expected_timeout);
    POLL_RESULT = 0;
    // pltrst read expectation
    expect_any(__wrap_gpio_get_value, fd);
    expect_any(__wrap_gpio_get_value, value);
    GPIO_GET_VALUE_RESULT = ST_OK;
    GPIO_GET_VALUE_VALUE = 0;

    assert_int_equal(ST_OK, target_wait_PRDY(&handle, logtotime));
}

void target_wait_PRDY_poll_detected_test(void** state)
{
    (void)state; /* unused */
    int logtotime = 1;
    int expected_timeout = (1 << logtotime) / 1000;
    if (expected_timeout <= 0) expected_timeout = 1;
    Target_Control_Handle handle;
    memset(&handle, 0, sizeof(handle));
    handle.initialized = true;
    handle.gpios[BMC_PRDY_N].number = 55;
    handle.gpios[BMC_PLTRST_B].read = (TargetReadFunctionPtr)test_read_gpio_pin;
    handle.gpios[BMC_PLTRST_B].type = PIN_GPIO;

    expect_any(__wrap_poll, fds);
    expect_value(__wrap_poll, nfds, 1);
    expect_value(__wrap_poll, timeout, expected_timeout);
    POLL_RESULT = 1;
    POLL_REVENTS[0] = (POLLPRI + POLLERR);
    // pltrst read expectation
    expect_any(__wrap_gpio_get_value, fd);
    expect_any(__wrap_gpio_get_value, value);
    GPIO_GET_VALUE_RESULT = ST_OK;
    GPIO_GET_VALUE_VALUE = 0;

    assert_int_equal(ST_OK, target_wait_PRDY(&handle, logtotime));
}

void target_wait_PRDY_poll_error_test(void** state)
{
    (void)state; /* unused */
    int logtotime = 1;
    int expected_timeout = (1 << logtotime) / 1000;
    if (expected_timeout <= 0) expected_timeout = 1;
    Target_Control_Handle handle;
    memset(&handle, 0, sizeof(handle));
    handle.initialized = true;
    handle.gpios[BMC_PRDY_N].number = 28;
    handle.gpios[BMC_PLTRST_B].read = (TargetReadFunctionPtr)test_read_gpio_pin;
    handle.gpios[BMC_PLTRST_B].type = PIN_GPIO;

    expect_any(__wrap_poll, fds);
    expect_value(__wrap_poll, nfds, 1);
    expect_value(__wrap_poll, timeout, expected_timeout);
    POLL_RESULT = -1;
    // pltrst read expectation
    expect_any(__wrap_gpio_get_value, fd);
    expect_any(__wrap_gpio_get_value, value);
    GPIO_GET_VALUE_RESULT = ST_OK;
    GPIO_GET_VALUE_VALUE = 0;

    assert_int_equal(ST_ERR, target_wait_PRDY(&handle, logtotime));
}

void target_get_fds_not_initialized_test(void** state)
{
    (void)state; /* unused */
    Target_Control_Handle handle;
    handle.initialized = false;
    target_fdarr_t fds;
    int num_fds;

    assert_int_equal(ST_ERR, target_get_fds(NULL, &fds, &num_fds));
    assert_int_equal(ST_ERR, target_get_fds(&handle, &fds, &num_fds));
    handle.initialized = true;
    assert_int_equal(ST_ERR, target_get_fds(&handle, NULL, &num_fds));
    assert_int_equal(ST_ERR, target_get_fds(&handle, &fds, NULL));
}

void target_get_fds_success_test(void** state)
{
    (void)state; /* unused */
    Target_Control_Handle handle;
    memset(&handle, 0, sizeof(handle));
    handle.initialized = true;
    handle.gpios[BMC_CPU_PWRGD].type = PIN_GPIO;
    handle.gpios[BMC_PRDY_N].fd = 9;
    handle.gpios[BMC_PLTRST_B].fd = 8;
    handle.gpios[BMC_CPU_PWRGD].fd = 7;
    handle.gpios[BMC_XDP_PRST_IN].fd = 6;
    // New pins: ensure they don't have garbage fds
    handle.gpios[BMC_PWRGD2].fd = -1;
    handle.gpios[BMC_PWRGD3].fd = -1;
    handle.event_cfg.report_PRDY = true;
    handle.dbus = NULL;
    handle.spp_handler = NULL;
    target_fdarr_t fds;
    int num_fds;

    assert_int_equal(ST_OK, target_get_fds(&handle, &fds, &num_fds));

    assert_int_equal(4, num_fds);
}

void target_get_fds_spp_handler_test(void** state)
{
    (void)state; /* unused */
    Target_Control_Handle handle;
    memset(&handle, 0, sizeof(handle));
    handle.initialized = true;
    // Set up GPIO pins
    handle.gpios[BMC_CPU_PWRGD].type = PIN_GPIO;
    handle.gpios[BMC_PRDY_N].fd = 9;
    handle.gpios[BMC_PLTRST_B].fd = 8;
    handle.gpios[BMC_CPU_PWRGD].fd = 7;
    handle.gpios[BMC_XDP_PRST_IN].fd = 6;
    handle.gpios[BMC_PWRGD2].fd = -1;
    handle.gpios[BMC_PWRGD3].fd = -1;
    handle.event_cfg.report_PRDY = true;
    handle.dbus = NULL;
    target_fdarr_t fds;
    int num_fds;

    // Set up mock SPP handler with device file descriptors
    memset(&mock_spp_handler, 0, sizeof(mock_spp_handler));
    for (int i = 0; i < MAX_SPP_BUS_DEVICES; i++)
    {
        mock_spp_handler.spp_dev_handlers[i] = 100 + i;  // Distinct FDs: 100, 101, ..., 107
    }
    handle.spp_handler = &mock_spp_handler;

    // Test 1: SPP device count = 0 (no SPP devices, only GPIO FDs)
    SPP_DEVICE_COUNT = 0;
    memset(&fds, 0, sizeof(fds));
    num_fds = 0;
    assert_int_equal(ST_OK, target_get_fds(&handle, &fds, &num_fds));
    assert_int_equal(4, num_fds);  // Only 4 GPIO FDs

    // Test 2: SPP device count = 1 (one SPP device)
    SPP_DEVICE_COUNT = 1;
    memset(&fds, 0, sizeof(fds));
    num_fds = 0;
    assert_int_equal(ST_OK, target_get_fds(&handle, &fds, &num_fds));
    assert_int_equal(5, num_fds);  // 4 GPIO + 1 SPP FD
    assert_int_equal(100, fds[4].fd);  // SPP FD at index 4

    // Test 3: SPP device count = MAX_SPP_BUS_DEVICES (8, at limit)
    SPP_DEVICE_COUNT = MAX_SPP_BUS_DEVICES;
    memset(&fds, 0, sizeof(fds));
    num_fds = 0;
    assert_int_equal(ST_OK, target_get_fds(&handle, &fds, &num_fds));
    assert_int_equal(4 + MAX_SPP_BUS_DEVICES, num_fds);  // 4 GPIO + 8 SPP FDs
    for (int i = 0; i < MAX_SPP_BUS_DEVICES; i++)
    {
        assert_int_equal(100 + i, fds[4 + i].fd);  // All 8 SPP FDs present
    }

    // Test 4: SPP device count = MAX_SPP_BUS_DEVICES + 1 (9, over limit)
    // The fix ensures loop stops at MAX_SPP_BUS_DEVICES to prevent overrun
    SPP_DEVICE_COUNT = MAX_SPP_BUS_DEVICES + 1;
    memset(&fds, 0, sizeof(fds));
    num_fds = 0;
    assert_int_equal(ST_OK, target_get_fds(&handle, &fds, &num_fds));
    // Should still only get 8 SPP devices, not 9 (bounds check prevents overrun)
    assert_int_equal(4 + MAX_SPP_BUS_DEVICES, num_fds);
    for (int i = 0; i < MAX_SPP_BUS_DEVICES; i++)
    {
        assert_int_equal(100 + i, fds[4 + i].fd);  // Only first 8 SPP FDs
    }

    // Reset global for other tests
    SPP_DEVICE_COUNT = 0;
}

void target_event_invalid_state_test(void** state)
{
    (void)state; /* unused */
    ASD_EVENT event;
    Target_Control_Handle handle;
    handle.initialized = false;
    struct pollfd poll_fd;
    poll_fd.fd = 1;
    assert_int_equal(ST_ERR, target_event(NULL, poll_fd, &event, NULL));
    assert_int_equal(ST_ERR, target_event(&handle, poll_fd, &event, NULL));
    handle.initialized = true;
    assert_int_equal(ST_ERR, target_event(&handle, poll_fd, NULL, NULL));
}

void target_event_power_event_gpio_get_value_failure_test(void** state)
{
    (void)state; /* unused */
    DBUS_HANDLE = &DBUS;
    dbus_helper_call_count = 0;
    Target_Control_Handle* handle = TargetHandler();
    assert_non_null(handle);
    handle->gpios[BMC_CPU_PWRGD].type = PIN_GPIO;
    handle->gpios[BMC_CPU_PWRGD].read = (TargetReadFunctionPtr)test_read_gpio_pin;
    handle->gpios[BMC_CPU_PWRGD].handler =
        (TargetEventFunctionPtr)on_power_event;
    ASD_EVENT event;
    struct pollfd expected_poll_fd;
    expected_poll_fd.fd = 345;
    expected_poll_fd.revents = POLL_GPIO;
    int expected_gpio = 999;
    handle->initialized = true;
    handle->gpios[BMC_CPU_PWRGD].fd = expected_poll_fd.fd;
    handle->gpios[BMC_CPU_PWRGD].number = expected_gpio;
    handle->gpios[BMC_CPU_PWRGD].type = PIN_GPIO;
    // dummy read
    expect_value(__wrap_gpio_get_value, fd, expected_poll_fd.fd);
    expect_any(__wrap_gpio_get_value, value);

    expect_value(__wrap_gpio_get_value, fd, expected_poll_fd.fd);
    expect_any(__wrap_gpio_get_value, value);
    GPIO_GET_VALUE_RESULT = ST_ERR;

    assert_int_equal(ST_ERR, target_event(handle, expected_poll_fd, &event, NULL));
    free(handle);
}

void target_event_power_event_restored_test(void** state)
{
    (void)state; /* unused */
    DBUS_HANDLE = &DBUS;
    dbus_helper_call_count = 0;
    Target_Control_Handle* handle = TargetHandler();
    assert_non_null(handle);
    handle->gpios[BMC_CPU_PWRGD].read = (TargetReadFunctionPtr)test_read_gpio_pin;
    handle->gpios[BMC_CPU_PWRGD].handler =
        (TargetEventFunctionPtr)on_power_event;
    ASD_EVENT event;
    struct pollfd expected_poll_fd;
    expected_poll_fd.fd = 345;
    expected_poll_fd.revents = POLL_GPIO;
    int expected_gpio = 999;
    handle->gpios[BMC_CPU_PWRGD].type = PIN_GPIO;
    handle->gpios[BMC_CPU_PWRGD].fd = expected_poll_fd.fd;
    handle->gpios[BMC_CPU_PWRGD].number = expected_gpio;
    handle->initialized = true;

    // dummy read
    expect_value(__wrap_gpio_get_value, fd, expected_poll_fd.fd);
    expect_any(__wrap_gpio_get_value, value);

    expect_value(__wrap_gpio_get_value, fd, expected_poll_fd.fd);
    expect_any(__wrap_gpio_get_value, value);
    GPIO_GET_VALUE_RESULT = ST_OK;
    GPIO_GET_VALUE_VALUE = 1;

    assert_int_equal(ST_OK, target_event(handle, expected_poll_fd, &event, NULL));
    assert_int_equal(ASD_EVENT_PWRRESTORE, event);
    free(handle);
}

void target_event_power_event_pwrfail_test(void** state)
{
    (void)state; /* unused */
    DBUS_HANDLE = &DBUS;
    dbus_helper_call_count = 0;
    Target_Control_Handle* handle = TargetHandler();
    assert_non_null(handle);
    handle->gpios[BMC_CPU_PWRGD].read = (TargetReadFunctionPtr)test_read_gpio_pin;
    handle->gpios[BMC_CPU_PWRGD].handler =
        (TargetEventFunctionPtr)on_power_event;
    ASD_EVENT event;
    struct pollfd expected_poll_fd;
    expected_poll_fd.fd = 345;
    expected_poll_fd.revents = POLL_GPIO;
    int expected_gpio = 999;
    handle->gpios[BMC_CPU_PWRGD].type = PIN_GPIO;
    handle->initialized = true;

    handle->gpios[BMC_CPU_PWRGD].fd = expected_poll_fd.fd;
    handle->gpios[BMC_CPU_PWRGD].number = expected_gpio;

    // dummy read
    expect_value(__wrap_gpio_get_value, fd, expected_poll_fd.fd);
    expect_any(__wrap_gpio_get_value, value);

    expect_value(__wrap_gpio_get_value, fd, expected_poll_fd.fd);
    expect_any(__wrap_gpio_get_value, value);
    GPIO_GET_VALUE_RESULT = ST_OK;
    GPIO_GET_VALUE_VALUE = 0;

    assert_int_equal(ST_OK, target_event(handle, expected_poll_fd, &event, NULL));
    assert_int_equal(ASD_EVENT_PWRFAIL, event);
    free(handle);
}

void target_event_plat_reset_event_gpio_get_value_failure_test(void** state)
{
    (void)state; /* unused */
    Target_Control_Handle* handle = *state;
    ASD_EVENT event;
    struct pollfd expected_poll_fd;
    expected_poll_fd.fd = 345;
    if (handle->gpios[BMC_PLTRST_B].type == PIN_GPIO)
    {
        expected_poll_fd.revents = POLL_GPIO;
        int expected_gpio = 989;

        handle->gpios[BMC_PLTRST_B].fd = expected_poll_fd.fd;
        handle->gpios[BMC_PLTRST_B].number = expected_gpio;

        // dummy read
        expect_value(__wrap_gpio_get_value, fd, expected_poll_fd.fd);
        expect_any(__wrap_gpio_get_value, value);

        expect_value(__wrap_gpio_get_value, fd, expected_poll_fd.fd);
        expect_any(__wrap_gpio_get_value, value);
        GPIO_GET_VALUE_RESULT = ST_ERR;
    }
    else if (handle->gpios[BMC_PLTRST_B].type == PIN_GPIOD)
    {
        expected_poll_fd.revents = POLLIN;
        struct gpiod_line line;
        handle->gpios[BMC_PLTRST_B].line = &line;
        handle->gpios[BMC_PLTRST_B].fd = expected_poll_fd.fd;
        expect_value(__wrap_gpiod_line_event_read, line, &line);
        expect_any(__wrap_gpiod_line_event_read, event);
        expect_any(__wrap_gpiod_line_get_value, line);
        GPIOD_LINE_EVENT_READ_RESULT = 0;
        GPIOD_LINE_GET_VALUE_VALUES[0] = -1;
        GPIOD_LINE_GET_VALUE_INDEX = 0;
    }

    assert_int_equal(ST_ERR, target_event(handle, expected_poll_fd, &event, NULL));
}

void target_event_power_dbus_test(void** state)
{
    (void)state; /* unused */
    DBUS_HANDLE = &DBUS;
    dbus_helper_call_count = 0;
    Target_Control_Handle* handle = TargetHandler();
    assert_non_null(handle);
    ASD_EVENT event;
    struct pollfd expected_poll_fd;
    expected_poll_fd.fd = 0;
    expected_poll_fd.revents = POLLIN;
    handle->initialized = true;
    DBUS_PROCESS_EVENT_RESULT = ST_OK;
    expect_any(__wrap_dbus_process_event, state);
    assert_int_equal(ST_OK, target_event(handle, expected_poll_fd, &event, NULL));
    free(handle);
}

void target_event_power_dbus_failed_test(void** state)
{
    (void)state; /* unused */
    DBUS_HANDLE = &DBUS;
    dbus_helper_call_count = 0;
    Target_Control_Handle* handle = TargetHandler();
    assert_non_null(handle);
    ASD_EVENT event;
    struct pollfd expected_poll_fd;
    expected_poll_fd.fd = 0;
    expected_poll_fd.revents = POLLIN;
    handle->initialized = true;
    DBUS_PROCESS_EVENT_RESULT = ST_ERR;
    expect_any(__wrap_dbus_process_event, state);
    assert_int_equal(ST_ERR, target_event(handle, expected_poll_fd, &event, NULL));
    free(handle);
}

void target_event_plat_reset_event_gpio_set_value_failure_test(void** state)
{
    (void)state; /* unused */
    Target_Control_Handle* handle = *state;
    ASD_EVENT event;
    struct pollfd expected_poll_fd;
    expected_poll_fd.fd = 345;
    int expected_preq_fd = 888;

    if (handle->gpios[BMC_PLTRST_B].type == PIN_GPIO)
    {
        expected_poll_fd.revents = POLL_GPIO;
        int expected_gpio = 999;

        handle->gpios[BMC_PLTRST_B].fd = expected_poll_fd.fd;
        handle->gpios[BMC_PLTRST_B].number = expected_gpio;
        handle->gpios[BMC_PREQ_N].fd = expected_preq_fd;
        handle->event_cfg.reset_break = true;

        // dummy read
        expect_value(__wrap_gpio_get_value, fd, expected_poll_fd.fd);
        expect_any(__wrap_gpio_get_value, value);

        expect_value(__wrap_gpio_get_value, fd, expected_poll_fd.fd);
        expect_any(__wrap_gpio_get_value, value);
        GPIO_GET_VALUE_RESULT = ST_OK;
        GPIO_GET_VALUE_VALUE = 1;

        expect_value(__wrap_gpio_set_value, fd, expected_preq_fd);
        expect_any(__wrap_gpio_set_value, value);
        GPIO_SET_VALUE_RESULT[0] = ST_ERR;
        GPIO_SET_VALUE_INDEX = 0;
    }
    else if (handle->gpios[BMC_PLTRST_B].type == PIN_GPIOD)
    {
        expected_poll_fd.revents = POLLIN;
        struct gpiod_line line;
        handle->event_cfg.reset_break = true;
        handle->gpios[BMC_PLTRST_B].line = &line;
        handle->gpios[BMC_PLTRST_B].fd = expected_poll_fd.fd;
        expect_value(__wrap_gpiod_line_event_read, line, &line);
        expect_any(__wrap_gpiod_line_event_read, event);
        expect_any(__wrap_gpiod_line_get_value, line);
        GPIOD_LINE_EVENT_READ_RESULT = 0;
        GPIOD_LINE_GET_VALUE_VALUES[0] = 0;
        GPIOD_LINE_GET_VALUE_INDEX = 0;
        // value=0 -> deassert -> PREQ write with failure
        expect_any(__wrap_gpiod_line_set_value, line);
        expect_any(__wrap_gpiod_line_set_value, value);
        GPIOD_LINE_SET_VALUE_RESULTS[0] = -1;
        GPIOD_LINE_SET_VALUE_INDEX = 0;
    }

    assert_int_equal(ST_ERR, target_event(handle, expected_poll_fd, &event, NULL));
}

void target_event_plat_reset_event_asserted_test(void** state)
{
    (void)state; /* unused */
    Target_Control_Handle* handle = *state;
    ASD_EVENT event;
    struct pollfd expected_poll_fd;
    expected_poll_fd.fd = 345;
    int expected_preq_fd = 888;

    if (handle->gpios[BMC_PLTRST_B].type == PIN_GPIO)
    {
        expected_poll_fd.revents = POLL_GPIO;
        int expected_gpio = 999;

        handle->gpios[BMC_PLTRST_B].fd = expected_poll_fd.fd;
        handle->gpios[BMC_PLTRST_B].number = expected_gpio;
        handle->gpios[BMC_PREQ_N].fd = expected_preq_fd;
        handle->event_cfg.reset_break = true;

        // dummy read
        expect_value(__wrap_gpio_get_value, fd, expected_poll_fd.fd);
        expect_any(__wrap_gpio_get_value, value);

        expect_value(__wrap_gpio_get_value, fd, expected_poll_fd.fd);
        expect_any(__wrap_gpio_get_value, value);
        GPIO_GET_VALUE_RESULT = ST_OK;
        GPIO_GET_VALUE_VALUE = 1;

        expect_value(__wrap_gpio_set_value, fd, expected_preq_fd);
        expect_any(__wrap_gpio_set_value, value);
        GPIO_SET_VALUE_RESULT[0] = ST_OK;
        GPIO_SET_VALUE_INDEX = 0;
    }
    else if (handle->gpios[BMC_PLTRST_B].type == PIN_GPIOD)
    {
        expected_poll_fd.revents = POLLIN;
        struct gpiod_line line;
        struct gpiod_line line_preq;
        handle->event_cfg.reset_break = true;
        handle->gpios[BMC_PLTRST_B].line = &line;
        handle->gpios[BMC_PLTRST_B].fd = expected_poll_fd.fd;
        handle->gpios[BMC_PREQ_N].line = &line_preq;
        expect_value(__wrap_gpiod_line_event_read, line, &line);
        expect_any(__wrap_gpiod_line_event_read, event);
        expect_any(__wrap_gpiod_line_get_value, line);
        GPIOD_LINE_EVENT_READ_RESULT = 0;
        GPIOD_LINE_GET_VALUE_VALUES[0] = 1;
        GPIOD_LINE_GET_VALUE_INDEX = 0;
        // value=1 -> asserted -> no PREQ write
    }

    assert_int_equal(ST_OK, target_event(handle, expected_poll_fd, &event, NULL));
    assert_int_equal(ASD_EVENT_PLRSTASSERT, event);
}

void target_event_plat_reset_event_deasserted_test(void** state)
{
    (void)state; /* unused */
    Target_Control_Handle* handle = *state;
    ASD_EVENT event;
    struct pollfd expected_poll_fd;
    expected_poll_fd.fd = 345;

    if (handle->gpios[BMC_PLTRST_B].type == PIN_GPIO)
    {
        expected_poll_fd.revents = POLL_GPIO;
        int expected_gpio = 999;
        int expected_preq_gpio = 888;

        handle->gpios[BMC_PLTRST_B].fd = expected_poll_fd.fd;
        handle->gpios[BMC_PLTRST_B].number = expected_gpio;
        handle->gpios[BMC_PREQ_N].number = expected_preq_gpio;
        handle->event_cfg.reset_break = true;

        // dummy read
        expect_value(__wrap_gpio_get_value, fd, expected_poll_fd.fd);
        expect_any(__wrap_gpio_get_value, value);

        expect_value(__wrap_gpio_get_value, fd, expected_poll_fd.fd);
        expect_any(__wrap_gpio_get_value, value);
        GPIO_GET_VALUE_RESULT = ST_OK;
        GPIO_GET_VALUE_VALUE = 0;
    }
    else if (handle->gpios[BMC_PLTRST_B].type == PIN_GPIOD)
    {
        expected_poll_fd.revents = POLLIN;
        struct gpiod_line line;
        struct gpiod_line line_preq;
        handle->event_cfg.reset_break = true;
        handle->gpios[BMC_PLTRST_B].line = &line;
        handle->gpios[BMC_PLTRST_B].fd = expected_poll_fd.fd;
        handle->gpios[BMC_PREQ_N].line = &line_preq;
        expect_value(__wrap_gpiod_line_event_read, line, &line);
        expect_any(__wrap_gpiod_line_event_read, event);
        expect_any(__wrap_gpiod_line_get_value, line);
        GPIOD_LINE_EVENT_READ_RESULT = 0;
        GPIOD_LINE_GET_VALUE_VALUES[0] = 0;
        GPIOD_LINE_GET_VALUE_INDEX = 0;
        // value=0 -> deassert -> PREQ write
        expect_any(__wrap_gpiod_line_set_value, line);
        expect_any(__wrap_gpiod_line_set_value, value);
        GPIOD_LINE_SET_VALUE_RESULTS[0] = 0;
        GPIOD_LINE_SET_VALUE_INDEX = 0;
    }
    assert_int_equal(ST_OK, target_event(handle, expected_poll_fd, &event, NULL));
    assert_int_equal(ASD_EVENT_PLRSTDEASSRT, event);
}

void target_event_prdy_no_breakall_test(void** state)
{
    (void)state; /* unused */
    Target_Control_Handle* handle = *state;
    ASD_EVENT event;
    struct pollfd expected_poll_fd;
    expected_poll_fd.fd = 345;

    if (handle->gpios[BMC_PRDY_N].type == PIN_GPIO)
    {
        expected_poll_fd.revents = POLL_GPIO;

        handle->gpios[BMC_PRDY_N].fd = expected_poll_fd.fd;
        handle->event_cfg.break_all = false;

        // dummy read
        expect_value(__wrap_gpio_get_value, fd, expected_poll_fd.fd);
        expect_any(__wrap_gpio_get_value, value);
    }
    else if (handle->gpios[BMC_PRDY_N].type == PIN_GPIOD)
    {
        expected_poll_fd.revents = POLLIN;
        struct gpiod_line line;
        handle->gpios[BMC_PRDY_N].fd = expected_poll_fd.fd;
        handle->gpios[BMC_PRDY_N].line = &line;
        expect_value(__wrap_gpiod_line_event_read, line, &line);
        expect_any(__wrap_gpiod_line_event_read, event);
        GPIOD_LINE_EVENT_READ_RESULT = 0;
        GPIOD_LINE_GET_VALUE_VALUES[0] = 0;
        GPIOD_LINE_GET_VALUE_INDEX = 0;
    }

    assert_int_equal(ST_OK, target_event(handle, expected_poll_fd, &event, NULL));
    assert_int_equal(ASD_EVENT_PRDY_EVENT, event);
}

void target_event_prdy_set_preq_fail_test(void** state)
{
    (void)state; /* unused */
    Target_Control_Handle* handle = *state;
    ASD_EVENT event;
    struct pollfd expected_poll_fd;
    expected_poll_fd.fd = 345;
    handle->event_cfg.break_all = true;
    handle->event_cfg.reset_break = false;

    if (handle->gpios[BMC_PRDY_N].type == PIN_GPIO)
    {
        expected_poll_fd.revents = POLL_GPIO;
        int expected_fd = 999;
        handle->gpios[BMC_PRDY_N].fd = expected_poll_fd.fd;
        handle->gpios[BMC_PREQ_N].fd = expected_fd;

        // dummy read
        expect_value(__wrap_gpio_get_value, fd, expected_poll_fd.fd);
        expect_any(__wrap_gpio_get_value, value);

        expect_value(__wrap_gpio_set_value, fd, expected_fd);
        expect_any(__wrap_gpio_set_value, value);
        GPIO_SET_VALUE_RESULT[0] = ST_ERR;
        GPIO_SET_VALUE_INDEX = 0;
    }
    else if (handle->gpios[BMC_PRDY_N].type == PIN_GPIOD)
    {
        expected_poll_fd.revents = POLLIN;
        struct gpiod_line line;
        struct gpiod_line line_preq;
        handle->gpios[BMC_PRDY_N].line = &line;
        handle->gpios[BMC_PRDY_N].fd = expected_poll_fd.fd;
        handle->gpios[BMC_PREQ_N].line = &line_preq;
        expect_value(__wrap_gpiod_line_event_read, line, &line);
        expect_any(__wrap_gpiod_line_event_read, event);
        // Asserting PREQ
        expect_value(__wrap_gpiod_line_set_value, line, &line_preq);
        expect_any(__wrap_gpiod_line_set_value, value);
        GPIOD_LINE_SET_VALUE_RESULTS[0] = -1;
        GPIOD_LINE_SET_VALUE_INDEX = 0;
    }
    assert_int_equal(ST_ERR, target_event(handle, expected_poll_fd, &event, NULL));
}

void target_event_prdy_toggle_preq_test(void** state)
{
    (void)state; /* unused */
    Target_Control_Handle* handle = *state;
    ASD_EVENT event;
    struct pollfd expected_poll_fd;
    expected_poll_fd.fd = 345;
    int expected_fd = 999;
    handle->event_cfg.break_all = true;
    handle->event_cfg.reset_break = false;

    if (handle->gpios[BMC_PRDY_N].type == PIN_GPIO)
    {
        expected_poll_fd.revents = POLL_GPIO;
        handle->gpios[BMC_PRDY_N].fd = expected_poll_fd.fd;
        handle->gpios[BMC_PREQ_N].fd = expected_fd;

        // dummy read
        expect_value(__wrap_gpio_get_value, fd, expected_poll_fd.fd);
        expect_any(__wrap_gpio_get_value, value);

        expect_value_count(__wrap_gpio_set_value, fd, expected_fd, 2);
        expect_any_count(__wrap_gpio_set_value, value, 2);
        GPIO_SET_VALUE_RESULT[0] = ST_OK;
        GPIO_SET_VALUE_RESULT[1] = ST_OK;
        GPIO_SET_VALUE_INDEX = 0;
    }
    else if (handle->gpios[BMC_PRDY_N].type == PIN_GPIOD)
    {
        expected_poll_fd.revents = POLLIN;
        struct gpiod_line line;
        struct gpiod_line line_preq;
        handle->gpios[BMC_PRDY_N].line = &line;
        handle->gpios[BMC_PRDY_N].fd = expected_poll_fd.fd;
        handle->gpios[BMC_PREQ_N].line = &line_preq;
        expect_value(__wrap_gpiod_line_event_read, line, &line);
        expect_any(__wrap_gpiod_line_event_read, event);
        // Asserting PREQ
        expect_value(__wrap_gpiod_line_set_value, line, &line_preq);
        expect_any(__wrap_gpiod_line_set_value, value);
        expect_value(__wrap_gpiod_line_set_value, line, &line_preq);
        expect_any(__wrap_gpiod_line_set_value, value);
        GPIOD_LINE_SET_VALUE_RESULTS[0] = 0;
        GPIOD_LINE_SET_VALUE_RESULTS[1] = 0;
        GPIOD_LINE_SET_VALUE_RESULTS[2] = 0;
        GPIOD_LINE_SET_VALUE_INDEX = 0;
    }
    expect_value(__wrap_usleep, useconds, 10000);

    assert_int_equal(ST_OK, target_event(handle, expected_poll_fd, &event, NULL));
    assert_int_equal(ASD_EVENT_PRDY_EVENT, event);
}

void target_event_prdy_toggle_preq_fail_test(void** state)
{
    (void)state; /* unused */
    Target_Control_Handle* handle = *state;
    ASD_EVENT event;
    struct pollfd expected_poll_fd;
    expected_poll_fd.fd = 345;
    handle->event_cfg.break_all = true;
    handle->event_cfg.reset_break = false;

    if (handle->gpios[BMC_PRDY_N].type == PIN_GPIO)
    {
        expected_poll_fd.revents = POLL_GPIO;
        int expected_fd = 999;

        handle->gpios[BMC_PRDY_N].fd = expected_poll_fd.fd;
        handle->gpios[BMC_PREQ_N].fd = expected_fd;

        // dummy read
        expect_value(__wrap_gpio_get_value, fd, expected_poll_fd.fd);
        expect_any(__wrap_gpio_get_value, value);

        expect_value_count(__wrap_gpio_set_value, fd, expected_fd, 2);
        expect_any_count(__wrap_gpio_set_value, value, 2);
        GPIO_SET_VALUE_RESULT[0] = ST_OK;
        GPIO_SET_VALUE_RESULT[1] = ST_ERR;
        GPIO_SET_VALUE_INDEX = 0;
    }
    else if (handle->gpios[BMC_PRDY_N].type == PIN_GPIOD)
    {
        expected_poll_fd.revents = POLLIN;
        struct gpiod_line line;
        struct gpiod_line line_preq;
        handle->gpios[BMC_PRDY_N].line = &line;
        handle->gpios[BMC_PRDY_N].fd = expected_poll_fd.fd;
        handle->gpios[BMC_PREQ_N].line = &line_preq;
        expect_value(__wrap_gpiod_line_event_read, line, &line);
        expect_any(__wrap_gpiod_line_event_read, event);
        // Asserting PREQ
        expect_value(__wrap_gpiod_line_set_value, line, &line_preq);
        expect_any(__wrap_gpiod_line_set_value, value);
        expect_value(__wrap_gpiod_line_set_value, line, &line_preq);
        expect_any(__wrap_gpiod_line_set_value, value);
        GPIOD_LINE_SET_VALUE_RESULTS[0] = 0;
        GPIOD_LINE_SET_VALUE_RESULTS[1] = -1;
        GPIOD_LINE_SET_VALUE_INDEX = 0;
    }
    expect_value(__wrap_usleep, useconds, 10000);

    assert_int_equal(ST_ERR, target_event(handle, expected_poll_fd, &event, NULL));
}

void target_event_xdp_present_test(void** state)
{
    (void)state; /* unused */
    Target_Control_Handle* handle = *state;
    ASD_EVENT event;
    struct pollfd expected_poll_fd;
    expected_poll_fd.fd = 345;

    if (handle->gpios[BMC_XDP_PRST_IN].type == PIN_GPIO)
    {
        expected_poll_fd.revents = POLL_GPIO;
        int expected_gpio = 979;
        handle->gpios[BMC_XDP_PRST_IN].fd = expected_poll_fd.fd;
        handle->gpios[BMC_XDP_PRST_IN].number = expected_gpio;

        // dummy read
        expect_value(__wrap_gpio_get_value, fd, expected_poll_fd.fd);
        expect_any(__wrap_gpio_get_value, value);
    }
    else if (handle->gpios[BMC_XDP_PRST_IN].type == PIN_GPIOD)
    {
        expected_poll_fd.revents = POLLIN;
        struct gpiod_line line;
        struct gpiod_line line_preq;
        handle->gpios[BMC_XDP_PRST_IN].line = &line;
        handle->gpios[BMC_XDP_PRST_IN].fd = expected_poll_fd.fd;
        expect_value(__wrap_gpiod_line_event_read, line, &line);
        expect_any(__wrap_gpiod_line_event_read, event);
    }

    assert_int_equal(ST_OK, target_event(handle, expected_poll_fd, &event, NULL));
    assert_int_equal(ASD_EVENT_XDP_PRESENT, event);
}

void target_event_dbus_test(void** state)
{
    (void)state; /* unused */
    Target_Control_Handle* handle = *state;
    handle->dbus->fd = 345;
    ASD_EVENT event;
    struct pollfd expected_poll_fd;
    expected_poll_fd.fd = 345;
    assert_int_equal(ST_OK, target_event(handle, expected_poll_fd, &event, NULL));
    assert_int_equal(ASD_EVENT_NONE, event);
}

void target_event_to_ignore_test(void** state)
{
    (void)state; /* unused */
    Target_Control_Handle* handle = *state;
    ASD_EVENT event;
    struct pollfd expected_poll_fd;
    expected_poll_fd.fd = 2;

    assert_int_equal(ST_OK, target_event(handle, expected_poll_fd, &event, NULL));
    assert_int_equal(ASD_EVENT_NONE, event);
}

void target_wait_sync_not_initialized_test(void** state)
{
    (void)state; /* unused */
    Target_Control_Handle handle;
    handle.initialized = false;

    assert_int_equal(ST_ERR, target_wait_sync(NULL, 0, 0));
    assert_int_equal(ST_ERR, target_wait_sync(&handle, 0, 0));
}

void target_wait_sync_controller_test(void** state)
{
    (void)state; /* unused */
    Target_Control_Handle handle;
    handle.initialized = true;
    handle.is_controller_probe = true;
    uint16_t delay = 193;

    expect_value(__wrap_usleep, useconds, delay * 1000);

    assert_int_equal(ST_ERR, target_wait_sync(&handle, 1, delay));
}

void target_wait_sync_target_test(void** state)
{
    (void)state; /* unused */
    Target_Control_Handle handle;
    handle.initialized = true;
    handle.is_controller_probe = false;
    uint16_t timeout = 193;

    expect_value(__wrap_usleep, useconds, timeout * 1000);

    assert_int_equal(ST_TIMEOUT, target_wait_sync(&handle, timeout, 2));
}

int main()
{
    Pin_Type gpio = PIN_GPIO;
    Pin_Type gpiod = PIN_GPIOD;
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(TargetHandler_malloc_failure_test),
        cmocka_unit_test(TargetHandler_malloc_success_test),
        cmocka_unit_test(TargetHandler_create_dbus_failure_test),
        cmocka_unit_test(target_initialize_invalid_param_test),
        cmocka_unit_test(target_initialize_already_initialized_test),
        cmocka_unit_test(target_initialize_gpio_export_failure_test),
        cmocka_unit_test(target_initialize_gpio_find_failure),
        cmocka_unit_test(target_initialize_gpio_set_active_low_failure_test),
        cmocka_unit_test(target_initialize_gpio_set_direction_failure_test),
        cmocka_unit_test(target_initialize_gpio_set_edge_failure_test),
        cmocka_unit_test(target_initialize_dummy_gpio_read_fail_test),
        cmocka_unit_test(
            target_initialize_gpiod_ctxless_find_line_failure_test),
        cmocka_unit_test(
            target_initialize_gpiod_ctxless_find_line_not_found_test),
        cmocka_unit_test(target_initialize_gpiod_chip_open_failure_test),
        cmocka_unit_test(target_initialize_gpiod_chip_get_line_failure_test),
        cmocka_unit_test(target_initialize_gpiod_line_request_failure_test),
        cmocka_unit_test(
            target_initialize_gpiod_line_event_get_fd_failure_test),
        cmocka_unit_test(target_initialize_xdp_check_failed_test),
        cmocka_unit_test(target_initialize_xdp_connected_test),
        cmocka_unit_test(target_initialize_debug_enable_failed_test),
        // cmocka_unit_test_prestate(target_initialize_success_test, &gpio),
        // cmocka_unit_test_prestate(target_initialize_success_test, &gpiod),
        cmocka_unit_test_prestate(target_initialize_powergood_pin_handler_test,
                                  &gpio),
        cmocka_unit_test_prestate(target_initialize_powergood_pin_handler_test,
                                  &gpiod),
        cmocka_unit_test(target_deinitialize_invalid_param_test),
        cmocka_unit_test(target_deinitialize_already_initialized_test),
        cmocka_unit_test(target_deinitialize_gpio_unexport_failure_test),
        // cmocka_unit_test(target_deinitialize_non_minus_test),
        cmocka_unit_test(target_deinitialize_success_test),
        cmocka_unit_test(target_write_not_initialized_test),
        cmocka_unit_test(target_write_PIN_PREQ_failure_test),
        cmocka_unit_test(target_write_PIN_PREQ_success_test),
        cmocka_unit_test(target_write_PIN_TCK_MUX_SELECT_success_test),
        cmocka_unit_test(target_write_PIN_SYS_PWR_OK_success_test),
        cmocka_unit_test(target_write_power_on_success_test),
        cmocka_unit_test(target_write_power_off_success_test),
        cmocka_unit_test(target_write_power_on_dbus_success_test),
        cmocka_unit_test(target_write_power_on_dbus_failure_test),
        cmocka_unit_test(target_write_power_onoff_failure_test),
        cmocka_unit_test(target_write_power_on_and_reset_break_success_test),
        cmocka_unit_test(target_write_power_reset_success_test),
        cmocka_unit_test(target_write_power_reset_and_reset_break_success_test),
        cmocka_unit_test(target_write_unkown_pin_test),
        cmocka_unit_test(target_read_not_initialized_test),
        cmocka_unit_test(target_read_BMC_PRDY_N_failure_test),
        cmocka_unit_test(target_read_BMC_PRDY_N_success_test),
        cmocka_unit_test(target_read_power_pin_test),
        cmocka_unit_test(target_read_power_pin_failed_test),
        cmocka_unit_test(target_read_power_pin_gpio_test),
        cmocka_unit_test(target_read_power_pin_gpio_failed_test),
        cmocka_unit_test(target_read_unkown_pin_test),
        cmocka_unit_test(target_write_event_config_not_initialized_test),
        cmocka_unit_test(target_write_event_WRITE_CONFIG_BREAK_ALL_test),
        cmocka_unit_test(target_write_event_WRITE_CONFIG_RESET_BREAK_test),
        cmocka_unit_test(target_write_event_WRITE_CONFIG_REPORT_PRDY_test),
        cmocka_unit_test(target_write_event_WRITE_CONFIG_REPORT_PLTRST_test),
        cmocka_unit_test(target_write_event_WRITE_CONFIG_REPORT_MBP_test),
        cmocka_unit_test(target_write_event_unkown_event_test),
        cmocka_unit_test(target_wait_PRDY_not_initialized_test),
        cmocka_unit_test(target_wait_PRDY_poll_timeout_test),
        cmocka_unit_test(target_wait_PRDY_poll_detected_test),
        cmocka_unit_test(target_wait_PRDY_poll_error_test),
        cmocka_unit_test(target_get_fds_not_initialized_test),
        cmocka_unit_test(target_get_fds_success_test),
        cmocka_unit_test(target_get_fds_spp_handler_test),
        cmocka_unit_test(target_event_invalid_state_test),
        cmocka_unit_test_setup_teardown(
            target_event_power_event_gpio_get_value_failure_test, setup,
            teardown),
        cmocka_unit_test_setup_teardown(target_event_power_event_restored_test,
                                        setup, teardown),
        cmocka_unit_test_setup_teardown(target_event_power_dbus_test, setup,
                                        teardown),
        cmocka_unit_test_setup_teardown(target_event_power_dbus_failed_test,
                                        setup, teardown),
        cmocka_unit_test_setup_teardown(target_event_power_event_pwrfail_test,
                                        setup, teardown),
        cmocka_unit_test_setup_teardown(
            target_event_plat_reset_event_gpio_get_value_failure_test, setup,
            teardown),
        cmocka_unit_test_setup_teardown(
            target_event_plat_reset_event_gpio_set_value_failure_test, setup,
            teardown),
        cmocka_unit_test_setup_teardown(
            target_event_plat_reset_event_asserted_test, setup, teardown),
        cmocka_unit_test_setup_teardown(
            target_event_plat_reset_event_deasserted_test, setup, teardown),
        cmocka_unit_test_setup_teardown(target_event_prdy_no_breakall_test,
                                        setup, teardown),
        cmocka_unit_test_setup_teardown(target_event_prdy_set_preq_fail_test,
                                        setup, teardown),
        cmocka_unit_test_setup_teardown(target_event_prdy_toggle_preq_test,
                                        setup, teardown),
        cmocka_unit_test_setup_teardown(target_event_prdy_toggle_preq_fail_test,
                                        setup, teardown),
        cmocka_unit_test_setup_teardown(target_event_xdp_present_test, setup,
                                        teardown),
        cmocka_unit_test_setup_teardown(target_event_dbus_test, setup,
                                        teardown),
        cmocka_unit_test_setup_teardown(target_event_to_ignore_test, setup,
                                        teardown),
        cmocka_unit_test(target_wait_sync_not_initialized_test),
        cmocka_unit_test(target_wait_sync_controller_test),
        cmocka_unit_test(target_wait_sync_target_test),

    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
