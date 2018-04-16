apPath=/etc/NetworkManager/system-connections/APHost
if [ ! -d "$apPath"]; then
    nmcli con up APHost
fi
#rm $apPath
#nmcli con add con-name APHost type wifi ifname wlan0 ssid SmartHomeAP mode ap -- ipv4.method shared
#nmcli con up APHost
#不存在
nmcli con add con-name APHost type wifi ifname wlan0 ssid SmartHomeAP mode ap -- ipv4.method shared
nmcli con up APHost