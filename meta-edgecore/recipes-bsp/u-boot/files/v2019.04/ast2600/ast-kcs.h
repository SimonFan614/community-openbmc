/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2018-2020 Intel Corporation */

#include <asm/io.h>
#include <common.h>

#define KCS_CHANNEL_MAX		4
#define IRQ_SRC_KCS_BASE	170 /* IRQ 170 */
#define MAX_KCS_PKT_SIZE	(64 * 1024)
/* KCS channel addresses */
#define KCS_CHANNEL1_ADDR	0xCA0
#define KCS_CHANNEL2_ADDR	0xCA8
#define KCS_CHANNEL3_ADDR	0xCA2 /* KCS SMS */
#define KCS_CHANNEL4_ADDR	0xCA4 /* KCS SMM */

#define ZERO_DATA		0x00

#define AST_LPC_BASE		0x1e789000

/* Aspeed KCS control registers */
#define LPC_HICR0	0x00 /* Host Interface Control Register 0 */
#define LPC_HICR1	0x04 /* Host Interface Control Register 1 */
#define LPC_HICR2	0x08 /* Host Interface Control Register 2 */
#define LPC_HICR3	0x0C /* Host Interface Control Register 3 */
#define LPC_HICR4	0x10 /* Host Interface Control Register 4 */
#define LPC_LADR3H	0x14 /* LPC channel #3 Address Register H */
#define LPC_LADR3L	0x18 /* LPC channel #3 Address Register H */
#define LPC_LADR12H	0x1C /* LPC channel #1#2 Address Register H */
#define LPC_LADR12L	0x20 /* LPC channel #1#2 Address Register L */
#define LPC_IDR1	0x24 /* Input Data Register 1 */
#define LPC_IDR2	0x28 /* Input Data Register 2 */
#define LPC_IDR3	0x2C /* Input Data Register 3 */
#define LPC_ODR1	0x30 /* Output Data Register 1 */
#define LPC_ODR2	0x34 /* Output Data Register 2 */
#define LPC_ODR3	0x38 /* Output Data Register 3 */
#define LPC_STR1	0x3C /* Status Register 1 */
#define LPC_STR2	0x40 /* Status Register 2 */
#define LPC_STR3	0x44 /* Status Register 3 */
#define LPC_HICRB	0x100 /* Host Interface Control Register B */
#define LPC_LADR4	0x110 /* LPC channel #4 Address Register */
#define LPC_IDR4	0x114 /* Input Data Register 4 */
#define LPC_ODR4	0x118 /* Output Data Register 4 */
#define LPC_STR4	0x11C /* Status Data Register 4 */

/* LPC Bits */
#define BIT_LADR12AS	BIT(7) /* Channel Address selection */
#define BIT_IBFIE1	BIT(1) /* Enable IDR1 Recv completion interrupt */
#define BIT_IBFIE2	BIT(2) /* Enable IDR2 Recv completion interrupt */
#define BIT_IBFIE3	BIT(3) /* Enable IBF13 interrupt */
#define BIT_LPC1E	BIT(5) /* Enable LPC channel #1 */
#define BIT_LPC2E	BIT(6) /* Enable LPC channel #2 */
#define BIT_LPC3E	BIT(7) /* Enable LPC channel #2 */
#define BIT_KCSENBL	BIT(2) /* Enable KCS interface in Channel #3 */
#define BIT_IBFIE4	BIT(1)
#define BIT_LPC4E	BIT(0)

#define BIT_STATUS_OBF	BIT(0) /* Output Data Register full #1/#2/#3 */
#define BIT_STATUS_IBF	BIT(1) /* Input Data Register full #1/#2/#3 */
#define BIT_STATUS_COD	BIT(3) /* Command/Data - (1=command,0=data) */

#define KCS_STATE_MASK		0xC0 /* BIT[6:7] of status register */
#define KCS_STATE(state)	((state) << 6)

/* IPMI2.0(section 9.7) - KCS interface State Bits */
#define KCS_STATE_IDLE		0x00
#define KCS_STATE_READ		0x01
#define KCS_STATE_WRITE		0x02
#define KCS_STATE_ERROR		0x03

/* IPMI2.0(section 9.10) - KCS interface control codes */
#define KCS_CTRL_CODE_GET_STATUS_ABORT	0x60
#define KCS_CTRL_CODE_WRITE_START	0x61
#define KCS_CTRL_CODE_WRITE_END		0x62
#define KCS_CTRL_CODE_READ		0x68

struct kcs_io_reg {
	u32 idr;
	u32 odr;
	u32 str;
};

enum kcs_phase {
	KCS_PHASE_IDLE = 0,
	KCS_PHASE_WRITE_START = 1,
	KCS_PHASE_WRITE_DATA = 2,
	KCS_PHASE_WRITE_END = 3,
	KCS_PHASE_READ_WAIT = 4,
	KCS_PHASE_READ = 5,
	KCS_PHASE_ABORT_1 = 6,
	KCS_PHASE_ABORT_2 = 7,
	KCS_PHASE_ERROR = 8
};

enum kcs_error {
	KCS_NO_ERROR = 0x00,
	KCS_ABORT_BY_CMD = 0x01,
	KCS_ILLEGAL_CTRL_CMD = 0x02,
	KCS_LENGTH_ERROR = 0x06,
	KCS_UNSPECIFIED_ERROR = 0xFF,
};

struct kcs_packet {
	enum kcs_phase phase;
	enum kcs_error error;
	u16 channel;
	bool read_req_done;
	u16 data_in_idx;
	u8 data_in[MAX_KCS_PKT_SIZE];
	u16 data_out_len;
	u16 data_out_idx;
	u8 data_out[MAX_KCS_PKT_SIZE];
};
