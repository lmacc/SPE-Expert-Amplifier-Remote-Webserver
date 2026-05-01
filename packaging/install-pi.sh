#!/usr/bin/env bash
# One-shot installer for the SPE remote daemon on a Raspberry Pi
# (Raspberry Pi OS, bookworm or later — Qt 6.4+).
#
# Usage:
#     sudo bash packaging/install-pi.sh
#
# Run this from the root of the qt6-port directory.

set -euo pipefail

if [[ $EUID -ne 0 ]]; then
    echo "install-pi.sh must be run as root (use sudo)." >&2
    exit 1
fi

SOURCE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${SOURCE_DIR}/build"
SERVICE_SRC="${SOURCE_DIR}/packaging/spe-remoted.service"
SERVICE_DST="/etc/systemd/system/spe-remoted.service"
BIN_DST="/usr/local/bin/spe-remoted"

TARGET_USER="${SUDO_USER:-pi}"

echo ">>> Installing build dependencies (apt)"
apt-get update
apt-get install -y --no-install-recommends \
    cmake ninja-build build-essential \
    qt6-base-dev qt6-serialport-dev qt6-websockets-dev qt6-httpserver-dev \
    pkg-config

echo ">>> Building spe-remoted"
sudo -u "${TARGET_USER}" cmake -B "${BUILD_DIR}" -S "${SOURCE_DIR}" \
    -G Ninja -DCMAKE_BUILD_TYPE=Release
sudo -u "${TARGET_USER}" cmake --build "${BUILD_DIR}" --target spe-remoted -j

echo ">>> Installing binary to ${BIN_DST}"
install -m 0755 "${BUILD_DIR}/spe-remoted" "${BIN_DST}"

echo ">>> Adding ${TARGET_USER} to the dialout group (serial access)"
usermod -a -G dialout "${TARGET_USER}" || true

echo ">>> Installing systemd unit"
# Patch the User= line to match whoever ran sudo.
sed "s|^User=pi$|User=${TARGET_USER}|" "${SERVICE_SRC}" > "${SERVICE_DST}"
chmod 0644 "${SERVICE_DST}"

systemctl daemon-reload
systemctl enable --now spe-remoted.service

echo
echo "===================================================================="
echo " spe-remoted installed and running."
echo "   systemctl status spe-remoted      # check health"
echo "   journalctl -u spe-remoted -f       # live logs"
echo
echo " Browser UI:   http://$(hostname -I | awk '{print $1}'):8080/"
echo " WebSocket:    ws://$(hostname -I | awk '{print $1}'):8888/ws"
echo
echo " Edit /etc/systemd/system/spe-remoted.service to change the serial"
echo " device or ports, then:  systemctl daemon-reload && systemctl restart spe-remoted"
echo "===================================================================="
