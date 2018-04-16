#!/bin/bash
ssid=$1
skey=$2
wifi_path=/etc/wpa_supplicant/wpa_supplicant.conf
if [ ! -n "$ssid" -o ! -n "$skey" ]; then
        echo "SSID and Key can not be null"
else
        echo "network={" > $wifi_path
        echo "  ssid=\"$ssid\" " >> $wifi_path
        echo "  psk=\"$skey\" " >> $wifi_path
        echo "}" >> $wifi_path
fi

ifdown wlan0
sleep 1s
ifup wlan0
