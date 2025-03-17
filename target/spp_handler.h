/*
Copyright (c) 2023, Intel Corporation

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

#ifndef _SPP_HANDLER_H_
#define _SPP_HANDLER_H_

#include <stdbool.h>
#include <stdint.h>

#include "asd_common.h"
#include "config.h"

#define UNINITIALIZED_SPP_DEBUG_DRIVER_HANDLE -1
/*the i3c-3 might change in future platforms and will need to get updated.*/
#define BROADCASTACTIONFILE "/sys/bus/i3c/devices/i3c-3/dbgaction_broadcast"
#define SPASENCLEAR_CMD {0x52, 0x30, 0x04, 0x00, 0xcc, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff}
#define CLEAR_ERROR_ACTION 0xfd
#define SPP_IBI_DATA_READY 0xAD
#define SPP_IBI_STATUS_CHANGED 0x5C
#define SPP_IBI_SUBREASON_PRDY 0x83
#define SPP_IBI_SUBREASON_OVERFLOW 0xFF
#define SPP_IBI_PRDY_WAIT_TIMEOUT_MS 10

typedef uint16_t __u16;
typedef uint8_t __u8;
typedef uint32_t __u32;
typedef uint64_t ___u64;

typedef enum {
    BroadcastResetAction    = 0x2A,
    DirectResetAction       = 0x9A,
    BpkOpcode               = 0xD7,
    DebugAction             = 0xD8,
    BroadcastDebugAction    = 0x58 
} spp_command_t;

typedef struct SPP_Handler
{
    uint8_t spp_bus;
    int spp_buses[MAX_SPP_BUSES];
    bus_config* config;
    int spp_dev_handlers[MAX_SPP_BUS_DEVICES];
    int spp_device_count;
    uint8_t device_index;
    int spp_driver_handle;
    bool ibi_handled;
} SPP_Handler;

SPP_Handler* SPPHandler(bus_config* config);
STATUS spp_initialize(SPP_Handler* state);
STATUS spp_deinitialize(SPP_Handler* state);
STATUS disconnect(SPP_Handler* state);
STATUS spp_bus_flock(SPP_Handler* state, uint8_t bus, int op);
STATUS spp_bus_select(SPP_Handler* state, uint8_t bus);
STATUS spp_set_sclk(SPP_Handler* state, uint16_t sclk);
STATUS spp_bus_device_count(SPP_Handler* state, uint8_t * count);
STATUS spp_bus_get_device_map(SPP_Handler* state, uint32_t * device_mask);
STATUS spp_device_select(SPP_Handler* state, uint8_t device);
STATUS spp_send(SPP_Handler* state, uint16_t size, uint8_t * write_buffer);
STATUS spp_receive(SPP_Handler* state, uint16_t * size, uint8_t * read_buffer);
STATUS spp_send_cmd(SPP_Handler* state, spp_command_t cmd, uint16_t size, 
                    uint8_t * write_buffer);
STATUS send_reset_rx(SPP_Handler* state);
STATUS spp_send_receive_cmd(SPP_Handler* state, spp_command_t cmd,
                            uint16_t wsize, uint8_t * write_buffer,
                            const uint16_t * rsize, uint8_t * read_buffer);
STATUS spp_set_sim_data_cmd(SPP_Handler* state, uint16_t size, uint8_t * read_buffer);
bool check_spp_prdy_event(ASD_EVENT event, ASD_EVENT_DATA event_data);
#endif // _SPP_HANDLER_H_
