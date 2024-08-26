/*
Copyright (c) 2024, Intel Corporation

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

#ifndef AT_SCALE_DEBUG_DEBUG_OVER_I3C_H
#define AT_SCALE_DEBUG_DEBUG_OVER_I3C_H

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcpp"
#include <linux/ioctl.h>
#include <linux/types.h>
#pragma GCC diagnostic pop



struct i3c_debug_opcode_ccc {
    __u8 opcode;
    __u16 write_len;
    __u64 write_ptr;
    __u16 read_len;
    __u64 read_ptr;
};

struct i3c_debug_action_ccc {
    __u8 action;
};

struct i3c_get_event_data {
    __u16 data_len;
    __u64 data_ptr;
};

#define I3C_DEBUG_IOCTL_BASE    0x79

#define I3C_DEBUG_IOCTL_DEBUG_OPCODE_CCC \
	_IOWR(I3C_DEBUG_IOCTL_BASE, 0x41, struct i3c_debug_opcode_ccc)

#define I3C_DEBUG_IOCTL_DEBUG_ACTION_CCC \
	_IOW(I3C_DEBUG_IOCTL_BASE, 0x42, struct i3c_debug_action_ccc)

#define I3C_DEBUG_IOCTL_GET_EVENT_DATA \
	_IOWR(I3C_DEBUG_IOCTL_BASE, 0x43, struct i3c_get_event_data)
#endif // AT_SCALE_DEBUG_DEBUG_OVER_I3C_H
