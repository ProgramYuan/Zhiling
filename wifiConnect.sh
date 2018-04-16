ssid=$1
skey=$2
if [ ! -n "$ssid" -o ! -n "$skey" ]; then
        echo "SSID and Key can not be null"
else
        nmcli con del default
        nmcli con add con-name default ifname wlan0 type wifi autoconnect yes ssid $ssid
        nmcli con mod default wifi-sec.key-mgmt wpa-psk
        nmcli con mod default wifi-sec.psk $skey
        nmcli con up default
fi