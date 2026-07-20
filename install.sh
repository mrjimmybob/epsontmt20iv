#!/bin/bash
# install.sh - build and install the TM-T20IV CUPS queue.
# Run with sudo. Usage: sudo ./install.sh <printer-ip>
set -euo pipefail

PRINTER_IP="${1:-192.168.1.50}"
QUEUE="TMT20IV-ttp"
OLD_QUEUE="TMT20IV80mm-thermal-ticket-printer" # pre-rename name; remove if present

if [[ $EUID -ne 0 ]]; then
    echo "Run this with sudo: sudo ./install.sh <printer-ip>" >&2
    exit 1
fi

cd "$(dirname "$0")"

echo "==> Building filter and backend"
make

echo "==> Installing filter -> /usr/lib/cups/filter/rastertotmt20iv"
install -m 0755 -o root -g root rastertotmt20iv /usr/lib/cups/filter/rastertotmt20iv

echo "==> Installing backend -> /usr/lib/cups/backend/epos"
install -m 0700 -o root -g root epos /usr/lib/cups/backend/epos

echo "==> Installing PPD -> /usr/share/ppd/epsont20iv/tmt20iv.ppd"
install -Dm 0644 ppd/tmt20iv.ppd /usr/share/ppd/epsont20iv/tmt20iv.ppd

if lpstat -p "$OLD_QUEUE" >/dev/null 2>&1; then
    echo "==> Removing pre-rename queue '$OLD_QUEUE'"
    lpadmin -x "$OLD_QUEUE"
fi

echo "==> Creating queue '$QUEUE' (device epos://$PRINTER_IP)"
lpadmin -p "$QUEUE" -E \
        -v "epos://${PRINTER_IP}" \
        -P /usr/share/ppd/epsont20iv/tmt20iv.ppd \
        -o printer-is-shared=true

echo "==> Enabling printer sharing"
cupsctl _share_printers=1

echo "==> Restarting CUPS"
systemctl restart cups

echo "==> Done. Queue: $QUEUE -> epos://${PRINTER_IP}"
lpstat -p "$QUEUE"
