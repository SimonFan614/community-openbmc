// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2021 (C) Edge-Core Networking Inc.
 * Copyright 2021 (C) Accton Technology Inc.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <common.h>

#define BUF2STR_MAXIMUM_OUTPUT_SIZE  (3*1024 + 1)
#define  EEPROM_BMC_READ_LEN    80
static unsigned char *eeprom_bmc_buf = NULL;
static char * proc_str = NULL;
static unsigned char  mac_addr_ec[6];

static const char *
buf2str_extended(const uint8_t *buf, int len, const char *sep)
{
    char *cur;
    int i;
    int sz;
    int left;
    int sep_len;

    if (buf == NULL) {
        snprintf(proc_str, BUF2STR_MAXIMUM_OUTPUT_SIZE-1, "<NULL>");
        return (const char *)proc_str;
    }
    cur = proc_str;
    left = BUF2STR_MAXIMUM_OUTPUT_SIZE-1;
    if (sep) {
        sep_len = strlen(sep);
    } else {
        sep_len = 0;
    }
    for (i = 0; i < len; i++) {
        /* may return more than 2, depending on locale */
        sz = snprintf(cur, left, "%2.2x", buf[i]);
        if (sz >= left) {
            /* buffer overflow, truncate */
            break;
        }
        cur += sz;
        left -= sz;
        /* do not write separator after last byte */
        if (sep && i != (len - 1)) {
            if (sep_len >= left) {
                break;
            }
            strncpy(cur, sep, left - sz);
            cur += sep_len;
            left -= sep_len;
        }
    }
    *cur = '\0';

    return (const char *)proc_str;
}

static const char *
buf2str(const uint8_t *buf, int len)
{
  return buf2str_extended(buf, len, NULL);
}

/* get_fru_area_str  -  Parse FRU area string from raw data
*
* @data:   raw FRU data
* @offset: offset into data for area
*
* returns pointer to FRU area string
*/
static char * get_fru_area_str(uint8_t * data, uint32_t * offset)
{
    static const char bcd_plus[] = "0123456789 -.:,_";
    char * str;
    int len, off, size, i, j, k, typecode;
    union {
        uint32_t bits;
        char chars[4];
    } u;

    size = 0;
    off = *offset;

    /* bits 6:7 contain format */
    typecode = ((data[off] & 0xC0) >> 6);

    // printf("Typecode:%i\n", typecode);
    /* bits 0:5 contain length */
    len = data[off++];
    len &= 0x3f;

    switch (typecode) {
    case 0:           /* 00b: binary/unspecified */
        /* hex dump -> 2x length */
        size = (len*2);
        break;
    case 2:           /* 10b: 6-bit ASCII */
        /* 4 chars per group of 1-3 bytes */
        size = ((((len+2)*4)/3) & ~3);
        break;
    case 3:           /* 11b: 8-bit ASCII */
    case 1:           /* 01b: BCD plus */
        /* no length adjustment */
        size = len;
        break;
    }

    if (size < 1) {
        *offset = off;
        return NULL;
    }
    str = malloc(size+1);
    if (str == NULL){
        return NULL;
    }
    memset(str, 0, size+1);

    if (len == 0) {
        str[0] = '\0';
        *offset = off;
        return str;
    }

    switch (typecode) {
    case 0:        /* Binary */
        strncpy(str, buf2str(&data[off], len), len*2);
        break;

    case 1:        /* BCD plus */
        for (k=0; k<len; k++){
            str[k] = bcd_plus[(data[off+k] & 0x0f)];
        }
        str[k] = '\0';
        break;

    case 2:        /* 6-bit ASCII */
        for (i=j=0; i<len; i+=3) {
            u.bits = 0;
            k = ((len-i) < 3 ? (len-i) : 3);
#if WORDS_BIGENDIAN
            u.chars[3] = data[off+i];
            u.chars[2] = (k > 1 ? data[off+i+1] : 0);
            u.chars[1] = (k > 2 ? data[off+i+2] : 0);
#define CHAR_IDX 3
#else
            memcpy((void *)&u.bits, &data[off+i], k);
#define CHAR_IDX 0
#endif
            for (k=0; k<4; k++) {
                str[j++] = ((u.chars[CHAR_IDX] & 0x3f) + 0x20);
                u.bits >>= 6;
            }
        }
        str[j] = '\0';
        break;

    case 3:
        memcpy(str, &data[off], len);
        str[len] = '\0';
        break;
    }

    off += len;
    *offset = off;

    return str;
}

int extract_mac_addr(char *str, unsigned char *mac)
{
    const char delim[2] = ":";
    char *token;
    int i = 0, var = 1;

    token = strtok(str, delim);
    while( token != NULL ){
        mac[i] = (unsigned char)simple_strtol(token, NULL, 16) & 0xff;
        i++;
        if( i >= 6 ){
            break;
        }
        token = strtok(NULL, delim);
    }

    var = 0;
FXIT:
    return var;
}

int get_eeprom_fru(void)
{
    int var = 1;
    uint8_t * board_data = NULL;
    uint32_t i = 0;
    eeprom_bmc_buf = malloc(EEPROM_BMC_READ_LEN + 1); 
    if( eeprom_bmc_buf == NULL ){
        goto FXIT;
    }
    proc_str = malloc(EEPROM_BMC_READ_LEN + 1);
    if( proc_str == NULL ){
        goto FXIT;
    }
    board_data = &eeprom_bmc_buf[8];
    eeprom_read(CONFIG_SYS_I2C_EEPROM_ADDR, 0, eeprom_bmc_buf, EEPROM_BMC_READ_LEN);
    char * fru_area;
    i = 3;
    i += 3; // skip date info.
    fru_area = get_fru_area_str(board_data, &i);
    if (fru_area != NULL) {
        /* Board Mfg */
        free(fru_area);
        fru_area = NULL;
    }
    fru_area = get_fru_area_str(board_data, &i);
    if (fru_area != NULL) {
        /* Board Product */
        free(fru_area);
        fru_area = NULL;
    }

    fru_area = get_fru_area_str(board_data, &i);
    if (fru_area != NULL) {
        /* Board Serial Number */
        free(fru_area);
        fru_area = NULL;
    }

    fru_area = get_fru_area_str(board_data, &i);
    if (fru_area != NULL) {
        /* Board Part Number */
        free(fru_area);
        fru_area = NULL;
    }
    fru_area = get_fru_area_str(board_data, &i);
    if (fru_area != NULL) {
        /* Board FRU ID */
        free(fru_area);
        fru_area = NULL;
    }

    uint8_t custom_num = 0;
    /* read any extra fields */
    while ((board_data[i] != 0xc1) && (i < EEPROM_BMC_READ_LEN)){
        int j = i;
        fru_area = get_fru_area_str(board_data, &i);
        if (fru_area != NULL) {
            if (strlen(fru_area) > 0) {
                if( custom_num == 0 ){
                    printf("EEPROM mac addr : %s\n", fru_area);
                    extract_mac_addr(fru_area, mac_addr_ec);
                }
                custom_num++;
            }
            free(fru_area);
            fru_area = NULL;
        }
        if (i == j){
            break;
        }
    }

    var = 0;
FXIT:
    if( proc_str ){
        free(proc_str);
    }
    if( eeprom_bmc_buf ){
        free(eeprom_bmc_buf);
    }
    return var;
}

unsigned char *get_mac_addr_from_eeprom(void)
{
    return mac_addr_ec;
}
