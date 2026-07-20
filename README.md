# epsont20iv — CUPS thermal driver for Epson TM-T20IV

Build a CUPS print queue on Linux Mint so a Chrome web app can print **80 mm tickets**
to an Epson **TM-T20IV** — a printer that only speaks **ePOS-Print over HTTPS**, not
raw ESC/POS. This is "route two": the app has an A4 printer for *albaranes* and this
80 mm thermal queue for tickets.

## Status
**Built and tested on the print-server Mint (`.51`).** Filter + backend compiled
clean, installed, queue `TMT20IV-ttp` created and printing.
Tests T1-T4 (BUILD.md) passed against the real printer: correct 576-dot width, no
inversion, a real HTML ticket with QR (scans cleanly, no clipping once content
respects the ~72mm printable strip), and a 40-item receipt exercising `<image>`
chunking with one clean cut. Sharing is enabled and same-host concurrent jobs are
confirmed to serialize without wedging the printer; T5's cross-host leg (adding the
queue from an actual second Mint) is deferred until a second machine exists - see
**CLIENT-INSTALL.md** for the instructions to hand to whoever sets one up.

Since the build session: the filter also trims blank leading/trailing raster rows
so any ticket length prints correctly with a single clean cut, with no need for the
calling app to specify a page/media size - it just prints like any other driver.
The PPD's nominal page width is the full 80mm roll (not 72mm) with margins
carving out the true 576-dot printable strip, matching how any commercial 80mm
thermal printer PPD works - client HTML/CSS should keep content inside that
72mm-effective width (leave ~4mm on each side).

## If you are a Claude session picking this up on the Mint
Read in this order:
1. **FACTS.md** — verified ground truth about the printer. Trust it; do **not** re-test
   the printer to rediscover it (it was proven over two days of diagnosis).
2. **SPEC.md** — the design (architecture + the C components to build).
3. **BUILD.md** — the runbook: dependencies, project layout, per-component notes,
   install, and an escalating test plan with exact commands.
4. **CLIENT-INSTALL.md** — instructions for adding the shared queue on other Mints.

`tmbridge/` (a superseded sibling project, on GitHub `mrjimmybob/tmbridge` and at
`D:\Data\Projects\Development\tmbridge`) has reusable C: the libcurl POST + dynamic
buffer. Reuse those (fixing the two bugs noted in FACTS.md); ignore its `escpos.c`.

## Locked decisions
- **Language: C.** (Filter + backend. No Python.)
- **Topology:** one **print-server** Mint owns the queue; every other Mint adds it as a
  shared printer. Dedicated server box or a client PC running Chrome — identical; the
  "server" is just whichever host runs CUPS. Currently the only Mint, at `.51` (test).
- **Transport:** rasterize the page → ePOS `<image>` → HTTPS POST. No 9100, no ESC/POS.

## Portability — these are TEST-network IPs
Current test net: printer `192.168.1.50`, Mint/server `192.168.1.51`. **The client
network will be different.** Nothing hardcodes IPs:
- **Printer IP** = the CUPS **device URI** `epos://<printer-ip>`, set once at queue
  creation (`lpadmin -v`). To move networks, change it in that one place.
- **Server** = whichever host runs CUPS; clients point at it by its address.
- Printer IP stability (static / DHCP reservation / router rule) is handled by the human.

## Environment / permissions the build needs (on the Mint)
- **sudo** — install into `/usr/lib/cups/{filter,backend}`, run `lpadmin`, restart CUPS.
- Network reachability to `<printer-ip>:443` (confirmed working from `.51`).
- Packages: `cups cups-filters libcups2-dev libcupsimage2-dev libcurl4-openssl-dev build-essential ghostscript`.

## Files here
- `README.md` — this file (orientation + decisions + portability).
- `FACTS.md` — verified ground truth + reference values + reusable-code notes.
- `SPEC.md` — design & architecture & component contracts.
- `BUILD.md` — build/install/test runbook (dev-facing; has the full T1-T5 test plan).
- `SERVER-INSTALL.md` — hand-off instructions for installing on the print-server.
- `CLIENT-INSTALL.md` — hand-off instructions for adding the shared queue on other Mints.
