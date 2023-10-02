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

#define UNINITIALIZED_I3C_DEBUG_DRIVER_HANDLE -1

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
    int spp_buses[MAX_SPP_BUSES];
    int i3c_debug_driver_handle;
} SPP_Handler;

SPP_Handler* SPPHandler(bus_config* config);
STATUS SPP_initialize(SPP_Handler* state);
STATUS spp_deinitialize(SPP_Handler* state);
STATUS spp_bus_flock(SPP_Handler* state, uint8_t bus, int op);
STATUS spp_bus_select(SPP_Handler* state, uint8_t bus);
STATUS spp_set_sclk(SPP_Handler* state, uint16_t sclk);
STATUS spp_send(SPP_Handler* state, uint16_t size, uint8_t * write_buffer);
STATUS spp_receive(SPP_Handler* state, uint16_t * size, uint8_t * read_buffer);
STATUS spp_send_cmd(SPP_Handler* state, spp_command_t cmd, uint16_t size, 
                    uint8_t * write_buffer);
STATUS spp_send_receive_cmd(SPP_Handler* state, spp_command_t cmd,
                            uint16_t wsize, uint8_t * write_buffer,
                            uint16_t * rsize, uint8_t * read_buffer);
STATUS spp_set_sim_data_cmd(SPP_Handler* state, uint16_t size, uint8_t * read_buffer);
#endif // _SPP_HANDLER_H_