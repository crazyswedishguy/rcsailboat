#!/usr/bin/env bash
# Sets up the Raspberry Pi 5 as a Wi-Fi access point.
# Run once on the Pi. Requires root (sudo).
#
# After running, devices connect to SSID "rcsailboat" and the base station
# is reachable at http://10.0.0.1:8000/
#
# Tested on Raspberry Pi OS (Bookworm, 64-bit).

set -euo pipefail

SSID="rcsailboat"
PASSWORD="sailboat"       # change before going to a public venue
IFACE="wlan0"
IP="10.0.0.1"
DHCP_RANGE="10.0.0.10,10.0.0.50,24h"

apt-get update -qq
apt-get install -y hostapd dnsmasq

# Stop services while we configure
systemctl stop hostapd dnsmasq || true

# --- Static IP for wlan0 ---
cat >> /etc/dhcpcd.conf <<EOF

interface ${IFACE}
    static ip_address=${IP}/24
    nohook wpa_supplicant
EOF

# --- hostapd (AP config) ---
cat > /etc/hostapd/hostapd.conf <<EOF
interface=${IFACE}
driver=nl80211
ssid=${SSID}
hw_mode=g
channel=6
wmm_enabled=0
macaddr_acl=0
auth_algs=1
ignore_broadcast_ssid=0
wpa=2
wpa_passphrase=${PASSWORD}
wpa_key_mgmt=WPA-PSK
wpa_pairwise=TKIP
rsn_pairwise=CCMP
EOF

sed -i 's|#DAEMON_CONF=""|DAEMON_CONF="/etc/hostapd/hostapd.conf"|' /etc/default/hostapd

# --- dnsmasq (DHCP) ---
mv /etc/dnsmasq.conf /etc/dnsmasq.conf.bak 2>/dev/null || true
cat > /etc/dnsmasq.conf <<EOF
interface=${IFACE}
dhcp-range=${DHCP_RANGE}
# Redirect all DNS queries to the Pi so any hostname opens the UI
address=/#/${IP}
EOF

# --- Enable and start ---
systemctl unmask hostapd
systemctl enable hostapd dnsmasq
systemctl start hostapd dnsmasq

echo ""
echo "Done. SSID: ${SSID}  Password: ${PASSWORD}  Base station: http://${IP}:8000/"
