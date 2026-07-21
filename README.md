# epsontmt20iv — CUPS thermal driver for Epson TM-T20IV

Build a CUPS print queue on Linux Mint so a Chrome web app can print **80 mm tickets**
to an Epson **TM-T20IV** — a printer that only speaks **ePOS-Print over HTTPS**, not
raw ESC/POS. This is "route two": the app has an A4 printer for *albaranes* and this
80 mm thermal queue for tickets.

## Status
**Working on the print-server Mint (`.51`).** Filter + backend build clean, queue
`TMT20IV-ttp` is installed and shared, and **Chrome → Ctrl+P → printer works end to
end** — verified with an 80 mm PDF carrying a logo, item table, barcode and QR: full
576-dot width, correct polarity, one clean cut.

Tests T1–T4 (BUILD.md) passed against the real printer, including a 40-item receipt
exercising `<image>` chunking. Sharing is enabled and same-host concurrent jobs
serialize without wedging the printer; T5's cross-host leg (adding the queue from an
actual second Mint) is deferred until a second machine exists — see
**CLIENT-INSTALL.md** for the instructions to hand to whoever sets one up.

Two problems were found and fixed during the Chrome bring-up. Both are load-bearing:
- **`print-scaling` must be `none`** — `install.sh` sets it via
  `lpadmin -o print-scaling-default=none`. With CUPS' default (`auto`) every ticket
  printed at roughly **25% size**. Never remove that line; see FACTS.md for the
  mechanism.
- **The page is 300 mm and the cut is emitted once per job**, not per page. The old
  1000 mm page turned Chrome's print preview into an absurd ribbon, and a per-page cut
  would slice any receipt long enough to paginate.

The filter trims blank leading/trailing raster rows, so any ticket length prints
correctly with a single clean cut and the calling app never has to choose a page or
media size — it just prints like any other driver. The PPD's nominal page width is the
full 80 mm roll (not 72 mm), with margins carving out the true 576-dot printable strip,
matching how any commercial 80 mm thermal PPD works — client HTML/CSS should keep
content inside that ~72 mm effective width (leave ~4 mm each side).

**Known gaps are catalogued in `TASKS.md`.** The driver serves this client correctly;
that file lists, in priority order, what separates it from a *complete* driver. The
three that matter most: paper-out is not reported to CUPS (**T01**), copies are
silently ignored (**T02**), and an unexpected raster width fails silently (**T03**).

## Read in this order:
1. **FACTS.md** — verified ground truth about the printer. Trust it; do **not** re-test
   the printer to rediscover it (it was proven over two days of diagnosis).
2. **SPEC.md** — the design (architecture + the C components to build).
3. **BUILD.md** — the runbook: dependencies, project layout, per-component notes,
   install, and an escalating test plan with exact commands.
4. **TASKS.md** — prioritised outstanding work. Each task says why it matters, where in
   the code it lives, what to do and how to verify it. Pick the lowest-numbered open one.
5. **CLIENT-INSTALL.md** — instructions for adding the shared queue on other Mints.

`tmbridge` (a superseded sibling project, on GitHub at `mrjimmybob/tmbridge`) has the
reusable C: the libcurl POST + dynamic buffer. Those were reused here with their known
bug fixed (see FACTS.md); its `escpos.c` is irrelevant to a raster driver.

## Locked decisions
- **Language: C.** (Filter + backend.)
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
- `README.md` — this file (orientation + status + decisions + portability).
- `FACTS.md` — verified ground truth + reference values + reusable-code notes.
- `SPEC.md` — design & architecture & component contracts.
- `BUILD.md` — build/install/test runbook (dev-facing; has the full T1-T5 test plan).
- `TASKS.md` — prioritised roadmap of outstanding work (T01–T19).
- `SERVER-INSTALL.md` — hand-off instructions for installing on the print-server.
- `CLIENT-INSTALL.md` — hand-off instructions for adding the shared queue on other Mints.
