// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2018-2020 Intel Corporation

#include "ipmi-handler.h"

/* IPMI network function codes */
#define NETFN_APP			0x06

/* IPMI command codes */
#define CMD_GET_DEV_ID			0x01
#define CMD_GET_SELF_TEST_RESULTS	0x04

typedef u16 (*fun_handler)(u8 *req, u16 req_len, u8 *res);

struct get_dev_id {
	u8 completion_code;
	u8 dev_id;
	u8 dev_rev;
	u8 fw_rev1;
	u8 fw_rev2;
	u8 ipmi_ver;
	u8 dev_support;
	u8 mfg_id[3];
	u8 product_id[2];
	u8 aux_fw_rev[4];
};
struct self_test_res {
	u8 completion_code;
	u8 res_byte[2];
};

struct ipmi_cmd_table {
	u8 net_fun;
	u8 cmd;
	fun_handler process_cmd;
};

static u16 get_device_id(u8 *req, u16 req_len, u8 *res)
{
	/* Get Device ID */
	bool operation = 0; /* normal operation */
	u8 manu_id[3] = { 0x6B, 0xCD, 0x00 }; /* edge-core, iana org, enterprise number */
	u8 prod_id[2] = { 0x53, 0x41 }; /* follow Emerson BMC */
	u8 fw_rev[3] = { 0x00, 0x00, 0x99};
	u8 aux_fw_rev[4] = { 0x99, 0x00, 0x00, 0x00 };
	struct get_dev_id *result = (struct get_dev_id *)res;

	if (req_len != 0) {
		result->completion_code = IPMI_CC_INVALID_DATA_LENGTH;
		return sizeof(result->completion_code);
	}

	result->completion_code = IPMI_CC_OK;
	result->dev_id = 0x20; /* follow Emerson BMC */
	result->dev_rev = 0x01; /* Not provides dev SDR */

	/* TODO: Get Firmware version from flash(PFM Header) */
	result->fw_rev1 = ((operation << 7) | (fw_rev[0] & 0x7F));
	result->fw_rev2 = fw_rev[1];

	result->ipmi_ver = 0x02; /* IPMI 2.0 */
	result->dev_support = 0x00; /* No dev support in this mode */
	memcpy(result->mfg_id, manu_id, sizeof(result->mfg_id));

	/* TODO: Read Platform ID from GPIO */
	memcpy(result->product_id, prod_id, sizeof(result->product_id));

	aux_fw_rev[0] = fw_rev[2];
	memcpy(result->aux_fw_rev, aux_fw_rev, sizeof(result->aux_fw_rev));

	return sizeof(struct get_dev_id);
}

static u16 get_self_test_result(u8 *req, u16 req_len, u8 *res)
{
	/* Get Self Test Results */
	struct self_test_res *result = (struct self_test_res *)res;

	if (req_len != 0) {
		result->completion_code = IPMI_CC_INVALID_DATA_LENGTH;
		return sizeof(result->completion_code);
	}

	result->completion_code = IPMI_CC_OK;
	// result->res_byte[0] = 0x56; /* Self test function not implemented. */
	result->res_byte[0] = 0x55; /* Self test OK */
	result->res_byte[1] = 0x00;

	return sizeof(struct self_test_res);
}

const struct ipmi_cmd_table cmd_info[] = {
	{ NETFN_APP,	CMD_GET_DEV_ID,			get_device_id },
	{ NETFN_APP,	CMD_GET_SELF_TEST_RESULTS,	get_self_test_result }
};

#define CMD_TABLE_SIZE ARRAY_SIZE(cmd_info)

void ipmi_cmd_handler(struct ipmi_cmd_data *ipmi_data)
{
	int i = 0;
	for (i = 0; i < CMD_TABLE_SIZE; i++) {
		if ((cmd_info[i].net_fun == ipmi_data->net_fun) &&
		    (cmd_info[i].cmd == ipmi_data->cmd)) {
			break;
		}
	}

	if (i == CMD_TABLE_SIZE) {
		/* Invalid or not supported. */
		ipmi_data->res_data[0] = IPMI_CC_INVALID_CMD;
		ipmi_data->res_len = 1;
		return;
	}

	/* Call the appropriate function handler */
	ipmi_data->res_len =
		cmd_info[i].process_cmd(ipmi_data->req_data, ipmi_data->req_len,
					&ipmi_data->res_data[0]);

	return;
}
