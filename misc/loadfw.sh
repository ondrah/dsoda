#!/bin/sh

FNAME=`lsusb -d 04b4:2250 | awk '{print $2"/"substr($4,1,3);}'`

if [ ! "$FNAME" ]; then
	echo "Device not found."
	exit 1
fi

/sbin/fxload -t fx2 -I dso2250_firmware.hex -s dso2250_loader.hex -D /proc/bus/usb/$FNAME
