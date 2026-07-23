# Installing the ticket printer queue on the print-server Mint

This sets up **one** Linux Mint machine as the CUPS print server for the
Epson TM-T20IV thermal printer. Every other machine just adds this queue as
a shared printer - see **CLIENT-INSTALL.md** for that (no driver, no C code,
nothing printer-specific on clients).

Pick one machine to be the server - a dedicated box or any PC that's on
whenever tickets need printing; there's nothing special about it other than
running CUPS. Don't install this on more than one machine talking to the
same printer at once - see note at the end.

## Requirements

- Linux Mint (or any Ubuntu/Debian-based distro with CUPS).
- Network reachability from this machine to the printer on port 443
  (`ping <printer-ip>` first; if that works, printing should too).
- sudo access on this machine.
- The printer's IP address should be stable (static IP or a DHCP
  reservation on the router) - if it changes, the queue must be updated to
  match (one command, see "Changing the printer's IP" below).

## Install

Two ways. **Option A (.deb) is preferred** for a real deployment - proper
dependency handling, versioning and a clean `apt remove`. Option B is the
from-source route, handy while developing.

### Option A - install the .deb (recommended)

If you were handed `epsontmt20iv_<version>_amd64.deb`:

```bash
sudo apt install /path/to/epsontmt20iv_1.0.0_amd64.deb
```
apt pulls in `cups`, `cups-filters` and `ghostscript` automatically. (Keep the
file somewhere like `/tmp` to avoid a harmless "not accessible by user _apt"
note when installing from your home directory.)

The package installs the driver **only** - it deliberately does not create the
queue, because the queue carries the printer's IP. Create it once:

```bash
sudo lpadmin -p TMT20IV-ttp -E \
     -v epos://<printer-ip> \
     -P /usr/share/ppd/epsont20iv/tmt20iv.ppd \
     -o printer-is-shared=true \
     -o print-scaling-default=none
sudo cupsctl _share_printers=1
```

`print-scaling-default=none` is **not optional** - without it every ticket
prints at roughly 25% size.

To build the .deb yourself from a checkout:
```bash
sudo apt install -y build-essential debhelper devscripts dpkg-dev \
                    libcups2-dev libcupsimage2-dev libcurl4-openssl-dev pkg-config
dpkg-buildpackage -us -uc -b        # produces ../epsontmt20iv_*.deb
```
The build runs the unit tests (`make test`) and fails if they don't pass.

Remove with `sudo apt remove epsontmt20iv` (this deletes the filter/backend/PPD,
so the queue stops working until you reinstall).

### Option B - build and install from source

1. Copy this whole `epsontmt20iv/` folder onto the server machine.
2. Install build dependencies:
   ```bash
   sudo apt update
   sudo apt install -y cups cups-filters libcups2-dev libcupsimage2-dev \
                        libcurl4-openssl-dev build-essential ghostscript
   ```
3. Run the installer, passing the printer's IP address:
   ```bash
   cd epsontmt20iv
   sudo ./install.sh <printer-ip>
   ```
   Example: `sudo ./install.sh 192.168.1.50`

   This builds the filter/backend, installs them under `/usr/lib/cups/`,
   installs the PPD, creates the CUPS queue `TMT20IV-ttp` pointed at the
   printer, turns on printer sharing, and restarts CUPS.

## Verify

```bash
lp -d TMT20IV-ttp /usr/share/cups/data/testprint
```

The printer should print a test page and cut. If nothing happens, check:

```bash
lpstat -p TMT20IV-ttp -l
tail -f /var/log/cups/error_log
```

## Changing the printer's IP

If the printer moves to a different address later, update the queue in one
place (no reinstall needed):

```bash
sudo lpadmin -p TMT20IV-ttp -v epos://<new-printer-ip>
```

## Re-running the installer

`install.sh` is safe to re-run (e.g. after a code update) - it rebuilds,
reinstalls the filter/backend/PPD, and recreates the queue with the same
name and printer IP you pass it.

## Why only one server

Firing print requests at this printer back-to-back can wedge its print
mechanism, recoverable only by a power-cycle. CUPS avoids this by
processing one job at a time *within a single queue* - so exactly one
machine should hold the real queue talking to `epos://<printer-ip>`. Every
other machine should add this queue as a *shared* printer (CLIENT-INSTALL.md),
never install its own local copy pointed at the printer directly.
