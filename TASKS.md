# TASKS — road from "works for this client" to a complete driver

The driver currently **works end to end**: Chrome → CUPS queue → filter → backend →
printer, correct width, correct cut. What follows is what separates it from a
*complete* driver. Nothing here is required to keep the current client running.

## How to use this file
Pick the **lowest-numbered open task**, read its section, implement it, run its
"Verify" step, then tick it in the table. One task per change/commit. Each section is
self-contained: it says *why*, *where in the code*, *what to do*, and *how to prove it
worked*. Read `FACTS.md` first — it holds the printer ground truth and stops you
re-testing things already proven.

## Priority key
| Pri | Meaning |
|-----|---------|
| **1** | Very important — a real bug, or something that will hurt in daily shop use |
| **2** | Important — expected driver behaviour / deployment quality |
| **3** | Not that important — robustness, maintainability, tidiness |
| **4** | Optional — productisation, only if this ever ships beyond this client |

## Summary
| ID | Name | Pri | One-liner | Done |
|----|------|-----|-----------|------|
| T01 | PAPER-STATUS | 1 | Report paper-out / cover-open to CUPS instead of a generic failure | ☐ |
| T02 | COPIES | 1 | `-n 3` currently prints one ticket, silently | ☐ |
| T03 | WIDTH-GUARD | 1 | Warn when raster width ≠ 576 so tiny-print can't silently return | ☐ |
| T04 | CUT-AND-DRAWER | 2 | Expose cut mode + cash-drawer kick as PPD options | ☐ |
| T05 | PAPER-WIDTH | 2 | Support 58 mm rolls, not just 80 mm | ☐ |
| T06 | RASTER-HEADER | 2 | Fix the PPD sample-header error → true 1-bit rendering (8× less data) | ☐ |
| T07 | CHUNK-LIMIT | 2 | Measure the printer's real max `<image>` height instead of guessing 256 | ☐ |
| T08 | DEBUG-GEOMETRY | 2 | Log resolution + page height in the filter's DEBUG line | ☐ |
| T09 | DISCOVERY | 3 | Real network discovery so the printer appears in "Add Printer" | ☐ |
| T10 | DITHER | 3 | Halftoning so photos/logos aren't blotchy | ☐ |
| T11 | TUNABLES | 3 | Move compile-time `#define`s into PPD options | ☐ |
| T12 | TESTS | 3 | Golden-file tests for the raster→XML core | ☐ |
| T13 | PAGE-SIZES | 3 | Offer several named page lengths | ☐ |
| T14 | STREAMING | 3 | Stream strips instead of buffering the whole page | ☐ |
| T15 | REPO-HYGIENE | 3 | Stop tracking build artifacts; ignore `.claude/` | ☐ |
| T16 | PACKAGING | 4 | `.deb`, driver-database entry, man page, versioning | ☐ |
| T17 | TLS-TRUST | 4 | Optional real certificate validation | ☐ |
| T18 | I18N | 4 | Spanish PPD option translations | ☐ |
| T19 | DRIVERLESS | 4 | IPP Everywhere shim so clients need no queue setup | ☐ |

## Already done — do not redo
- `print-scaling-default=none` is set in `install.sh`. **Never remove it** — without it
  pdftopdf shrinks every ticket to ~25%. See FACTS.md.
- The cut is emitted **once per job** in `main()` (the per-page cut in `process_page()`
  is commented out), so a paginated receipt prints continuously with one cut.
- PPD page height is 300 mm (`850.39 pt`), which keeps Chrome's print preview sane.

---

# Priority 1

## T01 — PAPER-STATUS · Report printer state to CUPS
**Why.** A POS printer runs out of paper constantly. Today a paper-out looks like a
generic failure: staff see nothing useful and the job may be lost. The printer already
tells us — the ePOS reply carries `status="N"`, an Epson ASB (Automatic Status Back)
bitmask. This is the single highest-value improvement in the list.

**Where.** `src/epos_backend.c` — the result mapping block near the end of `main()`
(currently: success → `CUPS_BACKEND_OK`, `EX_TIMEOUT` → `RETRY`, else `FAILED`). The
response body is already captured in `response`.

**What to do.**
1. Parse the `status="..."` attribute out of the response body (plain `strstr` +
   `strtoul` is fine) into a `uint32_t`.
2. Decode the ASB bits. Per Epson's ePOS-Print SDK these are the documented values —
   **confirm empirically** (see Verify) before relying on any single one:

   | Bit | Meaning |
   |-----|---------|
   | `0x00000001` | no response from mechanism |
   | `0x00000002` | print success |
   | `0x00000004` | drawer kick state |
   | `0x00000008` | offline |
   | `0x00000020` | cover open |
   | `0x00000040` | paper being fed by button |
   | `0x00000100` | waiting for online recovery |
   | `0x00000400` | mechanical error |
   | `0x00000800` | autocutter error |
   | `0x00002000` | unrecoverable error |
   | `0x00004000` | auto-recoverable error |
   | `0x00020000` | receipt paper **near end** |
   | `0x00080000` | receipt paper **end** |

   Anchor: a healthy print returns `251658262` = `0x0F000016` (print-success + drawer
   state + fixed high bits; no error bits). A wedged mechanism returns `1`.
3. Emit CUPS state reasons by writing to **stderr** — CUPS reads these:
   ```
   STATE: +media-empty-error      /* paper end   */
   STATE: -media-empty-error      /* cleared     */
   STATE: +media-low-warning      /* near end    */
   STATE: +cover-open-error       /* cover open  */
   STATE: +other-error            /* cutter / mechanical / unrecoverable */
   ```
   Always emit the matching `-reason` when the condition is absent, or the queue will
   stay stuck showing a stale error.
4. Choose exit codes deliberately: paper-out should **not** discard the ticket. Use
   `CUPS_BACKEND_HOLD` (or `RETRY`) so the job survives until paper is reloaded.

**Verify.** Induce each condition on the real printer and log the raw `status` value
first (`fprintf(stderr, "DEBUG: epos: status=0x%08X\n", st);`): (a) remove the paper
roll, (b) open the cover, (c) print normally. Confirm the decoded bits match the table,
that `lpstat -p TMT20IV-ttp` and the GNOME printer UI show "Out of paper", and that the
held job prints when paper is restored.

**References.** FACTS.md → "ePOS endpoint" (response shape). `cups/backend.h` for exit
codes.

---

## T02 — COPIES · Honour the requested copy count
**Why.** A real bug producing silently wrong output. `ppd/tmt20iv.ppd` declares
`*cupsManualCopies: True`, which tells CUPS *"the driver will produce copies itself"* —
but `src/rastertotmt20iv.c` never reads `argv[4]` (the copies count). `lp -n 3` prints
one ticket and reports success.

**Where.** `src/rastertotmt20iv.c` `main()` (argument handling + the emit at the end);
`ppd/tmt20iv.ppd` line with `*cupsManualCopies`.

**What to do.** Either:
- **(a) Implement it (preferred).** Parse `argv[4]` as an unsigned count (default 1,
  clamp to something sane like 1–20). Emit the whole per-job body **including its
  `<feed>`/`<cut>`** once per copy, so each ticket is separately cut. Note the cut now
  lives in `main()` — repeat the body+cut together, not just the images.
- **(b) Delegate it.** Set `*cupsManualCopies: False` and let CUPS run the chain per
  copy. Simpler, but N× the rasterising and N× the HTTPS POSTs.

**Verify.** `lp -n 3 -d TMT20IV-ttp sample_receipt_80mm.pdf` → exactly three identical
tickets, three clean cuts, one job in `lpstat`.

---

## T03 — WIDTH-GUARD · Fail loudly on unexpected raster width
**Why.** `process_page()` warns only when the raster is *wider* than 576 dots and
clamps. When it arrives **narrower** it is emitted as-is, and the printer centres a
small image on the paper — silently. That is exactly how the "prints too small" bug hid
for a day; it should never be able to hide again.

**Where.** `src/rastertotmt20iv.c` `process_page()`, the existing
`if (width > PRINTER_WIDTH_DOTS)` block (~line 179).

**What to do.** Add an `else if (width != PRINTER_WIDTH_DOTS)` branch that logs a loud
`WARN:` (or `ERROR:` — your call whether a wrong-size ticket should fail rather than
print small) including both the received width and the expected 576, plus a hint that
the usual cause is page-size/resolution/`print-scaling` misconfiguration.

**Verify.** Temporarily create a queue whose PPD says 150 dpi, print, and confirm the
warning appears in `/var/log/cups/error_log`. Restore afterwards.

---

# Priority 2

## T04 — CUT-AND-DRAWER · Cut mode and cash-drawer options
**Why.** The two features every POS deployment asks for next. Both are already
supported by the ePOS protocol; we simply never expose them.

**Where.** `ppd/tmt20iv.ppd` (new `*OpenUI` groups); `src/rastertotmt20iv.c` where the
`<feed>`/`<cut>` is emitted in `main()`; options arrive in `argv[5]` and can be parsed
with `cupsParseOptions()`.

**What to do.** Add PPD options and honour them:
- `CutMode`: `Feed` (current, `<cut type="feed"/>`), `NoCut`, and partial/full variants
  the firmware accepts.
- `DrawerKick`: `None` (default), `Before`, `After` — emit
  `<pulse drawer="1" time="pulse_100"/>` accordingly.
- `FeedLines`: how many `<feed line="N"/>` before the cut (currently hard-coded 3).

**Verify.** `lp -o CutMode=NoCut`, `lp -o DrawerKick=After` etc. produce the expected
physical behaviour; defaults must reproduce today's output exactly.

---

## T05 — PAPER-WIDTH · Support 58 mm as well as 80 mm
**Why.** The TM-T20IV takes both roll widths; `PRINTER_WIDTH_DOTS 576` is a
compile-time constant, so a 58 mm roll would misprint.

**Where.** `src/rastertotmt20iv.c` (`PRINTER_WIDTH_DOTS`); `ppd/tmt20iv.ppd`
(page sizes + a media/width option).

**What to do.** Add a second page size (58 mm ≈ `164.41 pt` wide, 48 mm/384-dot
printable — confirm on the unit) and derive the expected width from the raster header /
selected media instead of the constant. Keep 80 mm the default. Note this interacts
with T03 — the guard must compare against the *selected* width, not a fixed 576.

**Verify.** Load a 58 mm roll, select the 58 mm size, print the sample: full width, no
clipping, no warning.

---

## T06 — RASTER-HEADER · Fix the PPD sample-header error
**Why.** Every job logs `E ppdFilterLoadPPD: Unable to generate CUPS Raster sample
header.` The consequence is that the PPD's `ColorModel` (1-bit, `cupsColorSpace 3`) is
ignored and Ghostscript renders 8-bit sGray (`colorspace=18`) instead — 576 bytes/line
where 72 would do, for byte-identical output. Output is correct today; this is pure
waste plus an error in the log that masks real ones.

**Where.** `ppd/tmt20iv.ppd`.

**What to do.** Note `cupstestppd -v` reports **no errors** — the PPD is conformant, so
this is something `cupsRasterInterpretPPD()` specifically chokes on. Bisect: copy the
PPD and remove suspects one at a time, reinstalling and printing after each —
`*VariablePaperSize`/`*CustomPageSize`/`*ParamCustomPageSize` (note `WidthOffset` and
`HeightOffset` declare zero-width ranges `0 0`, and `Min/MaxMediaWidth` are both
`226.77` while `ParamCustomPageSize Width` allows `220 230` — inconsistent), the very
large `*MaxMediaHeight: 14400`, then `*ColorModel`. Also confirm the installed PPD has
no CRLF line endings (see FACTS.md).

**Verify.** The `E ... sample header` line disappears from `/var/log/cups/error_log`,
and the filter's own DEBUG line reports `1 bpp, colorspace=3` instead of
`8 bpp, colorspace=18`. Output must be visually identical.

---

## T07 — CHUNK-LIMIT · Measure the real max `<image>` height
**Why.** `CHUNK_ROWS 256` was a guess. Too large and long receipts could fail on some
firmware; unnecessarily small wastes XML overhead and round-trips.

**Where.** `src/rastertotmt20iv.c` `#define CHUNK_ROWS`.

**What to do.** Write a throwaway script that POSTs a single `<image>` of increasing
height (256, 512, 1024, 2048, …) directly to the ePOS endpoint until it stops returning
`success="true"`, then set `CHUNK_ROWS` to a comfortable margin below the limit.
Document the measured ceiling in FACTS.md.

**Verify.** A very long receipt (200+ lines) prints as one continuous ticket with one
cut, and the chosen chunk size is recorded in FACTS.md.

---

## T08 — DEBUG-GEOMETRY · Log resolution and page height
**Why.** When the tiny-print bug struck, the one number that would have identified it
instantly — the render resolution — wasn't logged. Cheap insurance.

**Where.** `src/rastertotmt20iv.c`, the existing `DEBUG: rastertotmt20iv: page ...`
`fprintf` in `process_page()`.

**What to do.** Add `header->HWResolution[0]`/`[1]` and the page height in points/mm to
that line.

**Verify.** Print anything; the DEBUG line shows `203x203 dpi`.

---

# Priority 3

## T09 — DISCOVERY · Real backend device discovery
**Why.** `src/epos_backend.c` `argc == 1` prints one hard-coded line, so the printer
never appears in CUPS "Add Printer" — you must hand-type `epos://<ip>`. A complete
backend enumerates devices.

**What to do.** On the no-arguments path, discover TM printers (mDNS/DNS-SD, or an SNMP
sweep, or Epson's discovery protocol) and print one line per printer:
`network epos://<ip> "Epson TM-T20IV" "<friendly name>" "<device-id>"`. Keep it fast
and bounded (CUPS gives backends a short timeout).

**Verify.** `/usr/lib/cups/backend/epos` with no args lists the real printer; it shows
up in the CUPS web UI's Add Printer list.

---

## T10 — DITHER · Halftoning for photos and logos
**Why.** `pack_row()` hard-thresholds 8-bit grey at 128. Perfect for text, barcodes and
QR; blotchy for photographs or gradient logos.

**Where.** `src/rastertotmt20iv.c` `pack_row()` (the `cupsBitsPerPixel == 8` branch).

**What to do.** Add an ordered (Bayer) or Floyd–Steinberg dither, selectable via a PPD
option (`Halftone: Threshold|Dither`), default **Threshold** so current output is
unchanged. Floyd–Steinberg needs an error-diffusion row buffer — mind that `pack_row()`
is currently stateless per row.

**Verify.** Print a page with a photo: visibly smoother in Dither mode; a text/QR ticket
in Threshold mode is byte-identical to today.

---

## T11 — TUNABLES · Move `#define`s into PPD options
**Why.** `CHUNK_ROWS`, `TRIM_PAD_ROWS`, `LEAD_PAD_ROWS`, `INVERT_BITS`, the HTTP/ePOS
timeouts and the feed count are compile-time constants. Changing any of them means a
rebuild and redeploy.

**What to do.** Promote the ones that a site might reasonably tune into PPD options
(parsed from `argv[5]` via `cupsParseOptions()`), keeping current values as defaults.

**Verify.** `lp -o TrimPad=48` changes the trailing whitespace with no rebuild.

---

## T12 — TESTS · Golden-file tests for the raster→XML core
**Why.** The bit-packing, base64, trimming and chunking are pure functions with no I/O —
easy to test, and exactly the kind of code where the `tmbridge` bugs hid until we ran it.

**What to do.** Add a `tests/` target that feeds known CUPS raster files (or synthetic
buffers) through the packing/trim/chunk path and diffs the emitted XML against checked-in
golden files. Include: all-blank page, single ink row, content at both edges, a page
needing exactly N chunks, and a width ≠ 576 case (pairs with T03).

**Verify.** `make test` passes; deliberately breaking a bit-shift makes it fail.

---

## T13 — PAGE-SIZES · Offer several named page lengths
**Why.** One named size (300 mm) means Chrome shows no paper-size choice. Handy lengths
would let an operator match a long ticket and get a tighter preview.

**What to do.** Add `80x150mm` (`425.20 pt`), `80x200mm` (`566.93 pt`), `80x300mm`
(`850.39 pt`, default), `80x500mm` (`1417.32 pt`). Because the cut is per-job (see
"Already done"), a receipt overflowing the chosen page paginates harmlessly.

**Verify.** Chrome's *Más ajustes* shows a paper-size selector; each choice previews and
prints correctly.

---

## T14 — STREAMING · Stream strips instead of buffering the page
**Why.** `process_page()` allocates the whole page bitmap plus the whole base64 body
before emitting. Fine at 300 mm; wasteful, and it scales badly with page length.

**What to do.** Emit each `<image>` strip as it is completed. Note this conflicts with
the current blank-trim strategy, which needs to see the whole page to find the last ink
row — a streaming version needs a lookahead window or a two-pass read of a temp file.
Only worth doing if page sizes grow.

**Verify.** Memory use flat across page lengths; output byte-identical.

---

## T15 — REPO-HYGIENE · Stop tracking build artifacts
**Why.** The compiled `epos` and `rastertotmt20iv` binaries are committed to the public
repo. `.gitignore` covers `*.o`/`*.exe` but not extensionless ELF binaries, and there is
no `.claude/` entry (a `.claude/` directory exists in the working copy and is currently
untracked only by luck).

**What to do.** Add `/epos`, `/rastertotmt20iv`, `.claude/` to `.gitignore`;
`git rm --cached` the two binaries. Add a `.gitattributes` with `* text=auto eol=lf` —
this repo round-trips through Windows and CRLF already broke `install.sh` once (FACTS.md).

**Verify.** `git status` clean after a build; a fresh clone on Linux runs
`./install.sh` without the CRLF failure.

---

# Priority 4 — only if this ships beyond this client

## T16 — PACKAGING
Build a `.deb` (filter, backend, PPD, postinst running `lpadmin`), add a `.drv`/PPD
database entry so the model appears in the CUPS Make/Model list instead of needing a
PPD file path, add a man page and a version string (currently none anywhere).

## T17 — TLS-TRUST
TLS verification is unconditionally off (correct today — the cert's SAN carries the
factory IP, see FACTS.md). Add an option to supply a CA/pinned cert and enable
verification, for sites that re-issue the printer's certificate.

## T18 — I18N
The PPD's option and choice names are English-only. Add translations (at minimum
`es`) so the print dialog reads correctly for the Spanish site.

## T19 — DRIVERLESS
Expose the printer as an IPP Everywhere / driverless device (e.g. via
`ippeveprinter`) so phones, macOS and other clients can print with no queue setup at
all, instead of every host adding the shared CUPS queue.
