/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2018 Mellanox Technologies. All rights reserved. */
/* Copyright (c) 2018 Oleksandr Shamray <oleksandrs@mellanox.com> */
/* Copyright (c) 2019 Intel Corporation */

#ifndef __UAPI_LINUX_JTAG_H
#define __UAPI_LINUX_JTAG_H

#include <linux/types.h>
#include <linux/ioctl.h>

/*
 * JTAG_XFER_MODE: JTAG transfer mode. Used to set JTAG controller transfer mode
 * This is bitmask for feature param in jtag_mode for ioctl JTAG_SIOCMODE
 */
#define  JTAG_XFER_MODE 0
/*
 * JTAG_CONTROL_MODE: JTAG controller mode. Used to set JTAG controller mode
 * This is bitmask for feature param in jtag_mode for ioctl JTAG_SIOCMODE
 */
#define  JTAG_CONTROL_MODE 1
/*
 * JTAG_TCK_CYCLE_DELAY_COUNT: JTAG delay counter for aspeed_jtag_tck_cycle. Used
 * set the number of jtag_tck_cycle delays repetitions.
 * This is bitmask for feature param in jtag_mode for ioctl JTAG_SIOCMODE
 */
#define  JTAG_TCK_CYCLE_DELAY_COUNT 2
/*
 * JTAG_CONTROLLER_OUTPUT_DISABLE: JTAG controller mode output disable, it is
 * used to enable other devices to own the JTAG bus.
 * This is bitmask for mode param in jtag_mode for ioctl JTAG_SIOCMODE
 */
#define  JTAG_CONTROLLER_OUTPUT_DISABLE 0
/*
 * JTAG_CONTROLLER_MODE: JTAG controller mode. Used to set JTAG controller in
 * host mode.
 * This is bitmask for mode param in jtag_mode for ioctl JTAG_SIOCMODE
 */
#define  JTAG_CONTROLLER_MODE 1
/*
 * JTAG_XFER_HW_MODE: JTAG hardware mode. Used to set HW drived or bitbang
 * mode. This is bitmask for mode param in jtag_mode for ioctl JTAG_SIOCMODE
 */
#define  JTAG_XFER_HW_MODE 1
/*
 * JTAG_XFER_SW_MODE: JTAG software mode. Used to set SW drived or bitbang
 * mode. This is bitmask for mode param in jtag_mode for ioctl JTAG_SIOCMODE
 */
#define  JTAG_XFER_SW_MODE 0

/**
 * enum jtag_tapstate:
 *
 * @JTAG_STATE_TLRESET: JTAG state machine Test Logic Reset state
 * @JTAG_STATE_IDLE: JTAG state machine IDLE state
 * @JTAG_STATE_SELECTDR: JTAG state machine SELECT_DR state
 * @JTAG_STATE_CAPTUREDR: JTAG state machine CAPTURE_DR state
 * @JTAG_STATE_SHIFTDR: JTAG state machine SHIFT_DR state
 * @JTAG_STATE_EXIT1DR: JTAG state machine EXIT-1 DR state
 * @JTAG_STATE_PAUSEDR: JTAG state machine PAUSE_DR state
 * @JTAG_STATE_EXIT2DR: JTAG state machine EXIT-2 DR state
 * @JTAG_STATE_UPDATEDR: JTAG state machine UPDATE DR state
 * @JTAG_STATE_SELECTIR: JTAG state machine SELECT_IR state
 * @JTAG_STATE_CAPTUREIR: JTAG state machine CAPTURE_IR state
 * @JTAG_STATE_SHIFTIR: JTAG state machine SHIFT_IR state
 * @JTAG_STATE_EXIT1IR: JTAG state machine EXIT-1 IR state
 * @JTAG_STATE_PAUSEIR: JTAG state machine PAUSE_IR state
 * @JTAG_STATE_EXIT2IR: JTAG state machine EXIT-2 IR state
 * @JTAG_STATE_UPDATEIR: JTAG state machine UPDATE IR state
 * @JTAG_STATE_CURRENT: JTAG current state, saved by driver
 */
enum jtag_tapstate {
	JTAG_STATE_TLRESET,
	JTAG_STATE_IDLE,
	JTAG_STATE_SELECTDR,
	JTAG_STATE_CAPTUREDR,
	JTAG_STATE_SHIFTDR,
	JTAG_STATE_EXIT1DR,
	JTAG_STATE_PAUSEDR,
	JTAG_STATE_EXIT2DR,
	JTAG_STATE_UPDATEDR,
	JTAG_STATE_SELECTIR,
	JTAG_STATE_CAPTUREIR,
	JTAG_STATE_SHIFTIR,
	JTAG_STATE_EXIT1IR,
	JTAG_STATE_PAUSEIR,
	JTAG_STATE_EXIT2IR,
	JTAG_STATE_UPDATEIR,
	JTAG_STATE_CURRENT
};

/**
 * enum jtag_reset:
 *
 * @JTAG_NO_RESET: JTAG run TAP from current state
 * @JTAG_FORCE_RESET: JTAG force TAP to reset state
 */
enum jtag_reset {
	JTAG_NO_RESET = 0,
	JTAG_FORCE_RESET = 1,
};

/**
 * enum jtag_xfer_type:
 *
 * @JTAG_SIR_XFER: SIR transfer
 * @JTAG_SDR_XFER: SDR transfer
 */
enum jtag_xfer_type {
	JTAG_SIR_XFER = 0,
	JTAG_SDR_XFER = 1,
};

/**
 * enum jtag_xfer_direction:
 *
 * @JTAG_READ_XFER: read transfer
 * @JTAG_WRITE_XFER: write transfer
 * @JTAG_READ_WRITE_XFER: read & write transfer
 */
enum jtag_xfer_direction {
	JTAG_READ_XFER = 1,
	JTAG_WRITE_XFER = 2,
	JTAG_READ_WRITE_XFER = 3,
};

/**
 * struct jtag_tap_state - forces JTAG state machine to go into a TAPC
 * state
 *
 * @reset: 0 - run IDLE/PAUSE from current state
 *         1 - go through TEST_LOGIC/RESET state before  IDLE/PAUSE
 * @from: initital jtag state
 * @endstate: jtag end state
 * @tck: clock counter
 *
 * Structure provide interface to JTAG device for JTAG set state execution.
 */
struct jtag_tap_state {
	__u8	reset;
	__u8	from;
	__u8	endstate;
	__u32	tck;
};

/**
 * union pad_config - Padding Configuration:
 *
 * @type: transfer type
 * @pre_pad_number: Number of prepadding bits bit[11:0]
 * @post_pad_number: Number of prepadding bits bit[23:12]
 * @pad_data : Bit value to be used by pre and post padding bit[24]
 * @int_value: unsigned int packed padding configuration value bit[32:0]
 *
 * Structure provide pre and post padding configuration in a single __u32
 */
union pad_config {
	struct {
		__u32 pre_pad_number	: 12;
		__u32 post_pad_number	: 12;
		__u32 pad_data		: 1;
		__u32 rsvd		: 7;
	};
	__u32 int_value;
};

/**
 * struct jtag_xfer - jtag xfer:
 *
 * @type: transfer type
 * @direction: xfer direction
 * @from: xfer current state
 * @endstate: xfer end state
 * @padding: xfer padding
 * @length: xfer bits length
 * @tdio : xfer data array
 *
 * Structure provide interface to JTAG device for JTAG SDR/SIR xfer execution.
 */
struct jtag_xfer {
	__u8	type;
	__u8	direction;
	__u8	from;
	__u8	endstate;
	__u32	padding;
	__u32	length;
	__u64	tdio;
};

/**
 * struct bitbang_packet - jtag bitbang array packet:
 *
 * @data:   JTAG Bitbang struct array pointer(input/output)
 * @length: array size (input)
 *
 * Structure provide interface to JTAG device for JTAG bitbang bundle execution
 */
struct bitbang_packet {
	struct tck_bitbang *data;
	__u32	length;
} __attribute__((__packed__));

/**
 * struct jtag_bitbang - jtag bitbang:
 *
 * @tms: JTAG TMS
 * @tdi: JTAG TDI (input)
 * @tdo: JTAG TDO (output)
 *
 * Structure provide interface to JTAG device for JTAG bitbang execution.
 */
struct tck_bitbang {
	__u8	tms;
	__u8	tdi;
	__u8	tdo;
} __attribute__((__packed__));

/**
 * struct jtag_mode - jtag mode:
 *
 * @feature: 0 - JTAG feature setting selector for JTAG controller HW/SW
 *           1 - JTAG feature setting selector for controller bus mode
 *               output (enable / disable).
 * @mode:    (0 - SW / 1 - HW) for JTAG_XFER_MODE feature(0)
 *           (0 - output disable / 1 - output enable) for JTAG_CONTROL_MODE
 *                                                    feature(1)
 *
 * Structure provide configuration modes to JTAG device.
 */
struct jtag_mode {
	__u32	feature;
	__u32	mode;
};

/* ioctl interface */
#define __JTAG_IOCTL_MAGIC	0xb2

#define JTAG_SIOCSTATE	_IOW(__JTAG_IOCTL_MAGIC, 0, struct jtag_tap_state)
#define JTAG_SIOCFREQ	_IOW(__JTAG_IOCTL_MAGIC, 1, unsigned int)
#define JTAG_GIOCFREQ	_IOR(__JTAG_IOCTL_MAGIC, 2, unsigned int)
#define JTAG_IOCXFER	_IOWR(__JTAG_IOCTL_MAGIC, 3, struct jtag_xfer)
#define JTAG_GIOCSTATUS _IOWR(__JTAG_IOCTL_MAGIC, 4, enum jtag_tapstate)
#define JTAG_SIOCMODE	_IOW(__JTAG_IOCTL_MAGIC, 5, unsigned int)
#define JTAG_IOCBITBANG	_IOW(__JTAG_IOCTL_MAGIC, 6, unsigned int)
#define JTAG_SIOCTRST	_IOW(__JTAG_IOCTL_MAGIC, 7, unsigned int)

/**
 * struct tms_cycle - This structure represents a tms cycle state.
 *
 * @tmsbits: is the bitwise representation of the needed tms transitions to
 *           move from one state to another.
 * @count:   number of jumps needed to move to the needed state.
 *
 */
struct tms_cycle {
	unsigned char tmsbits;
	unsigned char count;
};

/*
 * This is the complete set TMS cycles for going from any TAP state to any
 * other TAP state, following a "shortest path" rule.
 */
static const struct tms_cycle _tms_cycle_lookup[][16] = {
/*	    TLR        RTI        SelDR      CapDR      SDR        Ex1DR*/
/* TLR  */{{0x00, 0}, {0x00, 1}, {0x02, 2}, {0x02, 3}, {0x02, 4}, {0x0a, 4},
/*	    PDR        Ex2DR      UpdDR      SelIR      CapIR      SIR*/
	    {0x0a, 5}, {0x2a, 6}, {0x1a, 5}, {0x06, 3}, {0x06, 4}, {0x06, 5},
/*	    Ex1IR      PIR        Ex2IR      UpdIR*/
	    {0x16, 5}, {0x16, 6}, {0x56, 7}, {0x36, 6} },

/*	    TLR        RTI        SelDR      CapDR      SDR        Ex1DR*/
/* RTI  */{{0x07, 3}, {0x00, 0}, {0x01, 1}, {0x01, 2}, {0x01, 3}, {0x05, 3},
/*	    PDR        Ex2DR      UpdDR      SelIR      CapIR      SIR*/
	    {0x05, 4}, {0x15, 5}, {0x0d, 4}, {0x03, 2}, {0x03, 3}, {0x03, 4},
/*	    Ex1IR      PIR        Ex2IR      UpdIR*/
	    {0x0b, 4}, {0x0b, 5}, {0x2b, 6}, {0x1b, 5} },

/*	    TLR        RTI        SelDR      CapDR      SDR        Ex1DR*/
/* SelDR*/{{0x03, 2}, {0x03, 3}, {0x00, 0}, {0x00, 1}, {0x00, 2}, {0x02, 2},
/*	    PDR        Ex2DR      UpdDR      SelIR      CapIR      SIR*/
	    {0x02, 3}, {0x0a, 4}, {0x06, 3}, {0x01, 1}, {0x01, 2}, {0x01, 3},
/*	    Ex1IR      PIR        Ex2IR      UpdIR*/
	    {0x05, 3}, {0x05, 4}, {0x15, 5}, {0x0d, 4} },

/*	    TLR        RTI        SelDR      CapDR      SDR        Ex1DR*/
/* CapDR*/{{0x1f, 5}, {0x03, 3}, {0x07, 3}, {0x00, 0}, {0x00, 1}, {0x01, 1},
/*	    PDR        Ex2DR      UpdDR      SelIR      CapIR      SIR*/
	    {0x01, 2}, {0x05, 3}, {0x03, 2}, {0x0f, 4}, {0x0f, 5}, {0x0f, 6},
/*	    Ex1IR      PIR        Ex2IR      UpdIR*/
	    {0x2f, 6}, {0x2f, 7}, {0xaf, 8}, {0x6f, 7} },

/*	    TLR        RTI        SelDR      CapDR      SDR        Ex1DR*/
/* SDR  */{{0x1f, 5}, {0x03, 3}, {0x07, 3}, {0x07, 4}, {0x00, 0}, {0x01, 1},
/*	    PDR        Ex2DR      UpdDR      SelIR      CapIR      SIR*/
	    {0x01, 2}, {0x05, 3}, {0x03, 2}, {0x0f, 4}, {0x0f, 5}, {0x0f, 6},
/*	    Ex1IR      PIR        Ex2IR      UpdIR*/
	    {0x2f, 6}, {0x2f, 7}, {0xaf, 8}, {0x6f, 7} },

/*	    TLR        RTI        SelDR      CapDR      SDR        Ex1DR*/
/* Ex1DR*/{{0x0f, 4}, {0x01, 2}, {0x03, 2}, {0x03, 3}, {0x02, 3}, {0x00, 0},
/*	    PDR        Ex2DR      UpdDR      SelIR      CapIR      SIR*/
	    {0x00, 1}, {0x02, 2}, {0x01, 1}, {0x07, 3}, {0x07, 4}, {0x07, 5},
/*	    Ex1IR      PIR        Ex2IR      UpdIR*/
	    {0x17, 5}, {0x17, 6}, {0x57, 7}, {0x37, 6} },

/*	    TLR        RTI        SelDR      CapDR      SDR        Ex1DR*/
/* PDR  */{{0x1f, 5}, {0x03, 3}, {0x07, 3}, {0x07, 4}, {0x01, 2}, {0x05, 3},
/*	    PDR        Ex2DR      UpdDR      SelIR      CapIR      SIR*/
	    {0x00, 0}, {0x01, 1}, {0x03, 2}, {0x0f, 4}, {0x0f, 5}, {0x0f, 6},
/*	    Ex1IR      PIR        Ex2IR      UpdIR*/
	    {0x2f, 6}, {0x2f, 7}, {0xaf, 8}, {0x6f, 7} },

/*	    TLR        RTI        SelDR      CapDR      SDR        Ex1DR*/
/* Ex2DR*/{{0x0f, 4}, {0x01, 2}, {0x03, 2}, {0x03, 3}, {0x00, 1}, {0x02, 2},
/*	    PDR        Ex2DR      UpdDR      SelIR      CapIR      SIR*/
	    {0x02, 3}, {0x00, 0}, {0x01, 1}, {0x07, 3}, {0x07, 4}, {0x07, 5},
/*	    Ex1IR      PIR        Ex2IR      UpdIR*/
	    {0x17, 5}, {0x17, 6}, {0x57, 7}, {0x37, 6} },

/*	    TLR        RTI        SelDR      CapDR      SDR        Ex1DR*/
/* UpdDR*/{{0x07, 3}, {0x00, 1}, {0x01, 1}, {0x01, 2}, {0x01, 3}, {0x05, 3},
/*	    PDR        Ex2DR      UpdDR      SelIR      CapIR      SIR*/
	    {0x05, 4}, {0x15, 5}, {0x00, 0}, {0x03, 2}, {0x03, 3}, {0x03, 4},
/*	    Ex1IR      PIR        Ex2IR      UpdIR*/
	    {0x0b, 4}, {0x0b, 5}, {0x2b, 6}, {0x1b, 5} },

/*	    TLR        RTI        SelDR      CapDR      SDR        Ex1DR*/
/* SelIR*/{{0x01, 1}, {0x01, 2}, {0x05, 3}, {0x05, 4}, {0x05, 5}, {0x15, 5},
/*	    PDR        Ex2DR      UpdDR      SelIR      CapIR      SIR*/
	    {0x15, 6}, {0x55, 7}, {0x35, 6}, {0x00, 0}, {0x00, 1}, {0x00, 2},
/*	    Ex1IR      PIR        Ex2IR      UpdIR*/
	    {0x02, 2}, {0x02, 3}, {0x0a, 4}, {0x06, 3} },

/*	    TLR        RTI        SelDR      CapDR      SDR        Ex1DR*/
/* CapIR*/{{0x1f, 5}, {0x03, 3}, {0x07, 3}, {0x07, 4}, {0x07, 5}, {0x17, 5},
/*	    PDR        Ex2DR      UpdDR      SelIR      CapIR      SIR*/
	    {0x17, 6}, {0x57, 7}, {0x37, 6}, {0x0f, 4}, {0x00, 0}, {0x00, 1},
/*	    Ex1IR      PIR        Ex2IR      UpdIR*/
	    {0x01, 1}, {0x01, 2}, {0x05, 3}, {0x03, 2} },

/*	    TLR        RTI        SelDR      CapDR      SDR        Ex1DR*/
/* SIR  */{{0x1f, 5}, {0x03, 3}, {0x07, 3}, {0x07, 4}, {0x07, 5}, {0x17, 5},
/*	    PDR        Ex2DR      UpdDR      SelIR      CapIR      SIR*/
	    {0x17, 6}, {0x57, 7}, {0x37, 6}, {0x0f, 4}, {0x0f, 5}, {0x00, 0},
/*	    Ex1IR      PIR        Ex2IR      UpdIR*/
	    {0x01, 1}, {0x01, 2}, {0x05, 3}, {0x03, 2} },

/*	    TLR        RTI        SelDR      CapDR      SDR        Ex1DR*/
/* Ex1IR*/{{0x0f, 4}, {0x01, 2}, {0x03, 2}, {0x03, 3}, {0x03, 4}, {0x0b, 4},
/*	    PDR        Ex2DR      UpdDR      SelIR      CapIR      SIR*/
	    {0x0b, 5}, {0x2b, 6}, {0x1b, 5}, {0x07, 3}, {0x07, 4}, {0x02, 3},
/*	    Ex1IR      PIR        Ex2IR      UpdIR*/
	    {0x00, 0}, {0x00, 1}, {0x02, 2}, {0x01, 1} },

/*	    TLR        RTI        SelDR      CapDR      SDR        Ex1DR*/
/* PIR  */{{0x1f, 5}, {0x03, 3}, {0x07, 3}, {0x07, 4}, {0x07, 5}, {0x17, 5},
/*	    PDR        Ex2DR      UpdDR      SelIR      CapIR      SIR*/
	    {0x17, 6}, {0x57, 7}, {0x37, 6}, {0x0f, 4}, {0x0f, 5}, {0x01, 2},
/*	    Ex1IR      PIR        Ex2IR      UpdIR*/
	    {0x05, 3}, {0x00, 0}, {0x01, 1}, {0x03, 2} },

/*	    TLR        RTI        SelDR      CapDR      SDR        Ex1DR*/
/* Ex2IR*/{{0x0f, 4}, {0x01, 2}, {0x03, 2}, {0x03, 3}, {0x03, 4}, {0x0b, 4},
/*	    PDR        Ex2DR      UpdDR      SelIR      CapIR      SIR*/
	    {0x0b, 5}, {0x2b, 6}, {0x1b, 5}, {0x07, 3}, {0x07, 4}, {0x00, 1},
/*	    Ex1IR      PIR        Ex2IR      UpdIR*/
	    {0x02, 2}, {0x02, 3}, {0x00, 0}, {0x01, 1} },

/*	    TLR        RTI        SelDR      CapDR      SDR        Ex1DR*/
/* UpdIR*/{{0x07, 3}, {0x00, 1}, {0x01, 1}, {0x01, 2}, {0x01, 3}, {0x05, 3},
/*	    PDR        Ex2DR      UpdDR      SelIR      CapIR      SIR*/
	    {0x05, 4}, {0x15, 5}, {0x0d, 4}, {0x03, 2}, {0x03, 3}, {0x03, 4},
/*	    Ex1IR      PIR        Ex2IR      UpdIR*/
	    {0x0b, 4}, {0x0b, 5}, {0x2b, 6}, {0x00, 0} },
};

#endif /* __UAPI_LINUX_JTAG_H */
