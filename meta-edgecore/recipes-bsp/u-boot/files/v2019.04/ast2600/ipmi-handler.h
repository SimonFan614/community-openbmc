/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2018-2020 Intel Corporation */

#include "ast-kcs.h"

/* IPMI completion codes */
#define IPMI_CC_OK			0x00
#define IPMI_CC_NODE_BUSY		0xC0
#define IPMI_CC_INVALID_CMD		0xC1
#define IPMI_CC_INVALID_CMD_LUN		0xC2
#define IPMI_CC_OUT_OF_SPACE		0xC4
#define IPMI_CC_INVALID_DATA_LENGTH	0xC7
#define IPMI_CC_INVALID_DATA_FIELD	0xCC
#define IPMI_CC_UNSPECIFIED		0xFF

/* BMC IPMB LUNs */
#define LUN_BMC				0x00
#define LUN_OEM1			0x01
#define LUN_SMS				0x02
#define LUN_OEM2			0x01


#define MAX_IPMI_REQ_DATA_SIZE		MAX_KCS_PKT_SIZE
#define MAX_IPMI_RES_DATA_SIZE		64

/* Response netFn[7:2], Lun[1:0] */
#define GET_RESP_NETFN_LUN(lun, netfn)                                         \
	((lun & 0x03) | (((netfn + 1) << 2) & 0xFD))

struct ipmi_cmd_data {
	u8 net_fun;
	u8 cmd;
	u16 req_len;
	u16 res_len;
	u8 req_data[MAX_IPMI_REQ_DATA_SIZE];
	u8 res_data[MAX_IPMI_RES_DATA_SIZE];
};

void ipmi_cmd_handler(struct ipmi_cmd_data *ipmi_data);
