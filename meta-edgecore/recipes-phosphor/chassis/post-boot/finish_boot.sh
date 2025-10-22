#!/bin/bash
#
# Copyright 2020-present Accton. All Rights Reserved.
#
# This program file is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the
# Free Software Foundation; version 2 of the License.
#
# This program is distributed in the hope that it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
# FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
# for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program in a file named COPYING; if not, write to the
# Free Software Foundation, Inc.,
# 51 Franklin Street, Fifth Floor,
# Boston, MA 02110-1301 USA
#

PATH=/sbin:/bin:/usr/sbin:/usr/bin:/usr/local/bin

/lib/systemd/systemd-boot-check-no-failures 2>&1 | tee /tmp/"$HOSTNAME"_health.check

DEVMEM=/sbin/devmem

devmem_set_bit() {
    local addr
    local val
    addr=$1
    val=$($DEVMEM "$addr")
    val=$((val | (0x1 << $2)))
    $DEVMEM "$addr" 32 $val
}

devmem_clear_bit() {
    local addr
    local val
    addr=$1
    val=$($DEVMEM "$addr")
    val=$((val & ~(0x1 << $2)))
    $DEVMEM "$addr" 32 $val
}

AST26XXA0=0x05000000
AST26XXA1A2=0x05010000
AST26XXA3=0x05030000

# Get BMC CPU information and SOC ID
ID=$($DEVMEM 0x1e6e2004)
ID=$((ID & 0xffff0000))

CPU_INFO=$(cat /proc/cpuinfo)
if [[ $CPU_INFO = *ARMv7* ]] &&
   [ $((ID)) -eq $((AST26XXA0)) ] ||
   [ $((ID)) -eq $((AST26XXA1A2)) ] ||
   [ $((ID)) -eq $((AST26XXA3)) ]; then
    # Disable FMC watchdog 2
    devmem_clear_bit 0x1e620064 0
    # Reset FMC watchdog 2 counter to default
    $DEVMEM 0x1e620068 32 0xe0
else
    # Disable watchdog 2
    devmem_clear_bit 0x1e78502c 0
    # Reset watchdog 2 counter to default
    $DEVMEM 0x1e785020 32 0x14fb180
fi
