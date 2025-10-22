#!/bin/sh

/sbin/devmem 0x1E789098 32 0x00000A00
/sbin/devmem 0x1E78909C 32 0x00000000

#usb0 set mac-address
ifconfig usb0 down
ip link set dev usb0 address 02:00:00:00:00:01
ifconfig usb0 up
