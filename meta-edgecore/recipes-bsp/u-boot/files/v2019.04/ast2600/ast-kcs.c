// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2018-2020 Intel Corporation

#include "ipmi-handler.h"

#ifdef DEBUG_KCS_ENABLED
#define DBG_KCS			printf
#else
#define DBG_KCS(...)
#endif

#define KCS_CHANNEL_NO_3	3

static const u16 enabled_kcs_channel[] = { KCS_CHANNEL_NO_3 };

static const struct kcs_io_reg ast_kcs_bmc_ioregs[KCS_CHANNEL_MAX] = {
	{ .idr = LPC_IDR1, .odr = LPC_ODR1, .str = LPC_STR1 },
	{ .idr = LPC_IDR2, .odr = LPC_ODR2, .str = LPC_STR2 },
	{ .idr = LPC_IDR3, .odr = LPC_ODR3, .str = LPC_STR3 },
	{ .idr = LPC_IDR4, .odr = LPC_ODR4, .str = LPC_STR4 }
};

#define NO_OF_ENABLED_KCS_CHANNELS ARRAY_SIZE(enabled_kcs_channel)

static struct kcs_packet m_kcs_pkt[NO_OF_ENABLED_KCS_CHANNELS];

static u16 read_status(u16 channel_num)
{
	return readl(AST_LPC_BASE + ast_kcs_bmc_ioregs[channel_num - 1].str);
}

static void write_status(u16 channel_num, u16 value)
{
	writel(value, AST_LPC_BASE + ast_kcs_bmc_ioregs[channel_num - 1].str);
}

static u16 read_data(u16 channel_num)
{
	return readl(AST_LPC_BASE + ast_kcs_bmc_ioregs[channel_num - 1].idr);
}

static void write_data(u16 channel_num, u16 value)
{
	writel(value, AST_LPC_BASE + ast_kcs_bmc_ioregs[channel_num - 1].odr);
}

static void set_kcs_state(u16 channel_num, u16 state)
{
	u16 status = read_status(channel_num);

	status &= ~KCS_STATE_MASK;
	status |= KCS_STATE(state) & KCS_STATE_MASK;
	write_status(channel_num, status);
}

static struct kcs_packet *get_kcs_packet(u16 channel_num)
{
	for (u16 idx = 0; idx < NO_OF_ENABLED_KCS_CHANNELS; idx++) {
		if (channel_num == enabled_kcs_channel[idx])
			return &m_kcs_pkt[idx];
	}

	/* very unlike code hits here. */
	DBG_KCS("ERROR: %s error. ChannelNo: %d\n", __func__, channel_num);
	BUG();
}

static void kcs_force_abort(u16 channel_num)
{
	struct kcs_packet *kcs_pkt = NULL;

	kcs_pkt = get_kcs_packet(channel_num);
	DBG_KCS("ERROR: KCS communication aborted (Channel:%d, Error:%d)\n",
	       channel_num, kcs_pkt->error);
	set_kcs_state(channel_num, KCS_STATE_ERROR);
	read_data(channel_num);
	write_data(channel_num, ZERO_DATA);

	kcs_pkt->phase = KCS_PHASE_ERROR;
	kcs_pkt->read_req_done = false;
	kcs_pkt->data_in_idx = 0;
}

static void init_kcs_packet(u16 channel_num)
{
	struct kcs_packet *kcs_pkt = NULL;

	kcs_pkt = get_kcs_packet(channel_num);
	kcs_pkt->channel = channel_num;
	kcs_pkt->read_req_done = false;
	kcs_pkt->phase = KCS_PHASE_IDLE;
	kcs_pkt->error = KCS_NO_ERROR;
	kcs_pkt->data_in_idx = 0;
	kcs_pkt->data_out_idx = 0;
	kcs_pkt->data_out_len = 0;
}

static void process_kcs_request(u16 channel_num)
{
	struct kcs_packet *kcs_pkt = NULL;
	struct ipmi_cmd_data ipmi_data;
	int i;

	kcs_pkt = get_kcs_packet(channel_num);
	if (!kcs_pkt->read_req_done)
		return;

	DBG_KCS("%s:- chan:%d\n", __func__, channel_num);

#ifdef DEBUG_KCS_ENABLED
	DBG_KCS("Request data(Len:%d): ", kcs_pkt->data_in_idx);
	for (i = 0; i < kcs_pkt->data_in_idx; i++)
		DBG_KCS(" 0x%02x", kcs_pkt->data_in[i]);
	DBG_KCS("\n");
#endif

	u8 req_lun = kcs_pkt->data_in[0] & 0x03; /* LUN[1:0]  */
	ipmi_data.net_fun = (kcs_pkt->data_in[0] >> 2); /* netfn[7:2] */
	ipmi_data.cmd = kcs_pkt->data_in[1]; /* cmd */
	/* We support only BMC LUN 00h */
	if (req_lun != LUN_BMC) {
		kcs_pkt->data_out[0] =
			GET_RESP_NETFN_LUN(req_lun, ipmi_data.net_fun);
		kcs_pkt->data_out[1] = ipmi_data.cmd; /* cmd */
		kcs_pkt->data_out[2] = IPMI_CC_INVALID_CMD_LUN; /* CC code */
		kcs_pkt->data_out_len = 3;
		goto done;
	}

	/* Boundary check */
	if ((kcs_pkt->data_in_idx - 2) > sizeof(ipmi_data.req_data)) {
		kcs_pkt->data_out[0] =
			GET_RESP_NETFN_LUN(req_lun, ipmi_data.net_fun);
		kcs_pkt->data_out[1] = ipmi_data.cmd; /* cmd */
		kcs_pkt->data_out[2] = IPMI_CC_OUT_OF_SPACE; /* CC code */
		kcs_pkt->data_out_len = 3;
		goto done;
	}

	/* Fill in IPMI request data */
	ipmi_data.req_len = kcs_pkt->data_in_idx - 2;
	for (i = 0; i < kcs_pkt->data_in_idx - 2; i++)
		ipmi_data.req_data[i] = kcs_pkt->data_in[i + 2];

	/* Call IPMI command handler */
	ipmi_cmd_handler(&ipmi_data);

	/* Get IPMI response and fill KCS out data  */
	/* First 2 bytes in KCS response are netFn, Cmd */
	kcs_pkt->data_out[0] = GET_RESP_NETFN_LUN(req_lun, ipmi_data.net_fun);
	kcs_pkt->data_out[1] = ipmi_data.cmd;
	if ((ipmi_data.res_len + 2) > sizeof(kcs_pkt->data_out)) {
		kcs_pkt->data_out[2] = IPMI_CC_UNSPECIFIED; /* CC code */
		kcs_pkt->data_out_len = 3;
		goto done;
	}
	for (i = 0; i < ipmi_data.res_len; i++)
		kcs_pkt->data_out[i + 2] = ipmi_data.res_data[i];

	kcs_pkt->data_out_len = ipmi_data.res_len + 2;

#ifdef DEBUG_KCS_ENABLED
	DBG_KCS("Response data(Len:%d): ", kcs_pkt->data_out_len);
	for (i = 0; i < kcs_pkt->data_out_len; i++)
		DBG_KCS(" 0x%02x", kcs_pkt->data_out[i]);
	DBG_KCS("\n");
#endif

done:
	kcs_pkt->phase = KCS_PHASE_READ;
	write_data(channel_num, kcs_pkt->data_out[kcs_pkt->data_out_idx++]);
	kcs_pkt->read_req_done = false;
}

static void read_kcs_data(u16 channel_num)
{
	struct kcs_packet *kcs_pkt = NULL;

	kcs_pkt = get_kcs_packet(channel_num);

	switch (kcs_pkt->phase) {
	case KCS_PHASE_WRITE_START:
		kcs_pkt->phase = KCS_PHASE_WRITE_DATA;
		/* fall through */

	case KCS_PHASE_WRITE_DATA:
		if (kcs_pkt->data_in_idx >= MAX_KCS_PKT_SIZE) {
			kcs_pkt->error = KCS_LENGTH_ERROR;
			kcs_force_abort(channel_num);
			return;
		}
		set_kcs_state(channel_num, KCS_STATE_WRITE);
		write_data(channel_num, ZERO_DATA);
		kcs_pkt->data_in[kcs_pkt->data_in_idx++] =
			read_data(channel_num);
		break;

	case KCS_PHASE_WRITE_END:
		if (kcs_pkt->data_in_idx >= MAX_KCS_PKT_SIZE) {
			kcs_pkt->error = KCS_LENGTH_ERROR;
			kcs_force_abort(channel_num);
			return;
		}
		set_kcs_state(channel_num, KCS_STATE_READ);
		kcs_pkt->data_in[kcs_pkt->data_in_idx++] =
			read_data(channel_num);
		kcs_pkt->phase = KCS_PHASE_READ_WAIT;
		kcs_pkt->read_req_done = true;

		process_kcs_request(channel_num);
		break;

	case KCS_PHASE_READ:
		if (kcs_pkt->data_out_idx == kcs_pkt->data_out_len)
			set_kcs_state(channel_num, KCS_STATE_IDLE);

		u8 data = read_data(channel_num);
		if (data != KCS_CTRL_CODE_READ) {
			DBG_KCS("Invalid Read data. Phase:%d, Data:0x%02x\n",
				kcs_pkt->phase, data);
			set_kcs_state(channel_num, KCS_STATE_ERROR);
			write_data(channel_num, ZERO_DATA);
			break;
		}

		if (kcs_pkt->data_out_idx == kcs_pkt->data_out_len) {
			write_data(channel_num, ZERO_DATA);
			kcs_pkt->phase = KCS_PHASE_IDLE;
			break;
		}
		write_data(channel_num,
			   kcs_pkt->data_out[kcs_pkt->data_out_idx++]);
		break;

	case KCS_PHASE_ABORT_1:
		set_kcs_state(channel_num, KCS_STATE_READ);
		read_data(channel_num);
		write_data(channel_num, kcs_pkt->error);
		kcs_pkt->phase = KCS_PHASE_ABORT_2;
		break;

	case KCS_PHASE_ABORT_2:
		set_kcs_state(channel_num, KCS_STATE_IDLE);
		read_data(channel_num);
		write_data(channel_num, ZERO_DATA);
		kcs_pkt->phase = KCS_PHASE_IDLE;
		break;

	default:
		kcs_force_abort(channel_num);
	}
}

static void read_kcs_cmd(u16 channel_num)
{
	struct kcs_packet *kcs_pkt = NULL;

	kcs_pkt = get_kcs_packet(channel_num);

	set_kcs_state(channel_num, KCS_STATE_WRITE);
	write_data(channel_num, ZERO_DATA);

	u16 cmd = read_data(channel_num);
	switch (cmd) {
	case KCS_CTRL_CODE_WRITE_START:
		init_kcs_packet(channel_num);
		kcs_pkt->phase = KCS_PHASE_WRITE_START;
		break;

	case KCS_CTRL_CODE_WRITE_END:
		if (kcs_pkt->error != KCS_NO_ERROR) {
			kcs_force_abort(channel_num);
			return;
		}

		kcs_pkt->phase = KCS_PHASE_WRITE_END;
		break;

	case KCS_CTRL_CODE_GET_STATUS_ABORT:
		kcs_pkt->phase = KCS_PHASE_ABORT_1;
		kcs_pkt->error = KCS_ABORT_BY_CMD;
		break;

	default:
		kcs_pkt->error = KCS_ILLEGAL_CTRL_CMD;
		kcs_force_abort(channel_num);
	}
}

static void kcs_irq_handler(void *cookie)
{
	u32 channel_num = (u32)cookie;
	/* Look-up the interrupted KCS channel */
	u16 status = read_status(channel_num);
	if (status & BIT_STATUS_IBF) {
		if (status & BIT_STATUS_COD)
			read_kcs_cmd(channel_num);
		else
			read_kcs_data(channel_num);
	}

	DBG_KCS("%s: chan_no: %d\n", __FUNC__, channel_num);
}

static void set_kcs_channel_addr(u16 channel_num)
{
	u32 val;

	switch (channel_num) {
	case 1:
		val = readl(AST_LPC_BASE + LPC_HICR4) & ~BIT_LADR12AS;
		writel(val, AST_LPC_BASE + LPC_HICR4);
		val = (KCS_CHANNEL1_ADDR >> 8);
		writel(val, AST_LPC_BASE + LPC_LADR12H);
		val = (KCS_CHANNEL1_ADDR & 0xFF);
		writel(val, AST_LPC_BASE + LPC_LADR12L);
		break;

	case 2:
		val = readl(AST_LPC_BASE + LPC_HICR4) | BIT_LADR12AS;
		writel(val, AST_LPC_BASE + LPC_HICR4);
		val = (KCS_CHANNEL2_ADDR >> 8);
		writel(val, AST_LPC_BASE + LPC_LADR12H);
		val = (KCS_CHANNEL2_ADDR & 0xFF);
		writel(val, AST_LPC_BASE + LPC_LADR12L);
		break;

	case 3:
		val = (KCS_CHANNEL3_ADDR >> 8);
		writel(val, AST_LPC_BASE + LPC_LADR3H);
		val = (KCS_CHANNEL3_ADDR & 0xFF);
		writel(val, AST_LPC_BASE + LPC_LADR3L);
		break;

	case 4:
		val = (((KCS_CHANNEL4_ADDR + 1) << 16) | KCS_CHANNEL4_ADDR);
		writel(val, AST_LPC_BASE + LPC_LADR4);
		break;

	default:
		DBG_KCS("Invalid channel (%d) specified\n", channel_num);
		break;
	}
}

static void enable_kcs_channel(u16 channel_num, u16 enable)
{
	u32 val;

	switch (channel_num) {
	case 1:
		if (enable) {
			val = readl(AST_LPC_BASE + LPC_HICR2) | BIT_IBFIE1;
			writel(val, AST_LPC_BASE + LPC_HICR2);
			val = readl(AST_LPC_BASE + LPC_HICR0) | BIT_LPC1E;
			writel(val, AST_LPC_BASE + LPC_HICR0);
		} else {
			val = readl(AST_LPC_BASE + LPC_HICR0) & ~BIT_LPC1E;
			writel(val, AST_LPC_BASE + LPC_HICR0);
			val = readl(AST_LPC_BASE + LPC_HICR2) & ~BIT_IBFIE1;
			writel(val, AST_LPC_BASE + LPC_HICR2);
		}
		break;

	case 2:
		if (enable) {
			val = readl(AST_LPC_BASE + LPC_HICR2) | BIT_IBFIE2;
			writel(val, AST_LPC_BASE + LPC_HICR2);
			val = readl(AST_LPC_BASE + LPC_HICR0) | BIT_LPC2E;
			writel(val, AST_LPC_BASE + LPC_HICR0);
		} else {
			val = readl(AST_LPC_BASE + LPC_HICR0) & ~BIT_LPC2E;
			writel(val, AST_LPC_BASE + LPC_HICR0);
			val = readl(AST_LPC_BASE + LPC_HICR2) & ~BIT_IBFIE2;
			writel(val, AST_LPC_BASE + LPC_HICR2);
		}
		break;

	case 3:
		if (enable) {
			val = readl(AST_LPC_BASE + LPC_HICR2) | BIT_IBFIE3;
			writel(val, AST_LPC_BASE + LPC_HICR2);
			val = readl(AST_LPC_BASE + LPC_HICR0) | BIT_LPC3E;
			writel(val, AST_LPC_BASE + LPC_HICR0);
			val = readl(AST_LPC_BASE + LPC_HICR4) | BIT_KCSENBL;
			writel(val, AST_LPC_BASE + LPC_HICR4);
		} else {
			val = readl(AST_LPC_BASE + LPC_HICR0) & ~BIT_LPC3E;
			writel(val, AST_LPC_BASE + LPC_HICR0);
			val = readl(AST_LPC_BASE + LPC_HICR4) & ~BIT_KCSENBL;
			writel(val, AST_LPC_BASE + LPC_HICR4);
			val = readl(AST_LPC_BASE + LPC_HICR2) & ~BIT_IBFIE3;
			writel(val, AST_LPC_BASE + LPC_HICR2);
		}
		break;

	case 4:
		if (enable) {
			val = readl(AST_LPC_BASE + LPC_HICRB) | BIT_IBFIE4 |
			      BIT_LPC4E;
			writel(val, AST_LPC_BASE + LPC_HICRB);
		} else {
			val = readl(AST_LPC_BASE + LPC_HICRB) &
			      ~(BIT_IBFIE4 | BIT_LPC4E);
			writel(val, AST_LPC_BASE + LPC_HICRB);
		}
		break;

	default:
		DBG_KCS("Invalid channel (%d) specified\n", channel_num);
	}
}

void kcs_init(void)
{
	/* Initialize the KCS channels. */
	for (u16 idx = 0; idx < NO_OF_ENABLED_KCS_CHANNELS; idx++) {
		uint channel_num = (uint)enabled_kcs_channel[idx];
		DBG_KCS("%s Channel: %d\n", __func__, channel_num);
		set_kcs_channel_addr(channel_num);
		enable_kcs_channel(channel_num, 1);

		/* Set KCS channel state to idle */
		set_kcs_state(channel_num, KCS_STATE_IDLE);
		/* KCS interrupt */
		irq_install_handler(IRQ_SRC_KCS_BASE + channel_num - 1,
				    kcs_irq_handler, (void *)channel_num);
	}
}
