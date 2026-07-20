# SPEC — epsont20iv CUPS thermal driver (design)

> Ground-truth facts (endpoint, image format, TLS, reuse) live in **FACTS.md**.
> This file is the design. The runbook is **BUILD.md**.

## Goal
A CUPS queue `TMT20IV-ttp` on the print-server Mint that behaves
like a normal 80 mm thermal printer. Any app (the client's Chrome web app, or plain
Ctrl+P / `window.print`) prints an 80 mm-formatted page to it; the job is rasterized and
sent to the TM-T20IV via ePOS `<image>` over HTTPS. Multiple Mint hosts print to it.

## Why raster → ePOS (proof in FACTS.md)
The printer only accepts ePOS over HTTPS; `<image>` works, raw ESC/POS / port 9100 do
not. So the "driver" = **rasterize the page → pack as ePOS `<image>` → POST**.

## Architecture — central CUPS server
```
Chrome (Ctrl+P / window.print, on any Mint)
      │  IPP   (a normal shared printer — NO cert, NO browser flags)
      ▼
CUPS server (one Mint) : queue "TMT20IV-ttp"
      │  cups pdftoraster  →  CUPS raster
      ▼
  filter  rastertotmt20iv :  CUPS raster → ePOS-Print XML (chunked <image> strips + <cut>)
      │  stdout
      ▼
  backend  epos://<printer-ip> :  HTTPS POST → ePOS endpoint (verify off),
      │                           parse success="true", map to CUPS exit codes
      ▼
  Printer  (ePOS / 443)
```
Why central-server (not each Chrome talking to the printer directly):
- **Cert problem disappears** — only the server does the verify-off POST. Browsers speak
  plain IPP to CUPS: no per-Chrome cert trust, no SPKI flags, no Private-Network issues.
- **CUPS serializes jobs** — avoids the concurrency wedge (simultaneous ePOS POSTs →
  `EX_TIMEOUT`/lockup).
- **Install once** on the server; other Mints just add a shared printer.

## Components (contracts)

### 1. PPD `tmt20iv.ppd`
- Custom page: 80 mm paper, printable **576 dots @ 203 dpi** (72 mm; confirm in T2),
  continuous/roll length, zero margins.
- `*DefaultResolution: 203dpi`; mono 1-bit color model so the filter receives a mono
  raster.
- `*cupsFilter: "application/vnd.cups-raster 100 rastertotmt20iv"`.

### 2. Filter `rastertotmt20iv` (C, libcupsimage)
- Input: CUPS raster on stdin. Read with `cupsRasterOpen(0, CUPS_RASTER_READ)`,
  `cupsRasterReadHeader2`, `cupsRasterReadPixels`.
- Convert each page to a 1-bpp bitmap at `header.cupsWidth` (threshold if 8-bit gray;
  verify polarity so 1 = black per FACTS).
- Emit ePOS body: split the bitmap into vertical strips (≤ CHUNK lines) → one
  `<image width=W height=n align="center" color="color_1" mode="mono">base64</image>`
  per strip, then `<feed line="…"/>` and `<cut type="feed"/>`. Wrap in SOAP + `epos-print`.
  Write to stdout.
- Reuse tmbridge `buffer.*`.

### 3. Backend `epos` (C, libcurl)
- Device URI `epos://<printer-ip>[:443]` (from `$DEVICE_URI`).
- CUPS backend contract: `argc == 1` → print one discovery line and exit 0; otherwise the
  job data (the filter's XML) arrives on stdin.
- POST it to the ePOS endpoint, TLS verify off; **parse `success="true"`**; map:
  success → `CUPS_BACKEND_OK`; `EX_TIMEOUT` → `CUPS_BACKEND_RETRY`; else →
  `CUPS_BACKEND_FAILED`. Log the ePOS `code=` to stderr.
- Reuse tmbridge `http.c`/`epos.c` (with the success-check fix).

### 4. `install.sh`
- `make`; install filter → `/usr/lib/cups/filter/` (0755 root), backend →
  `/usr/lib/cups/backend/epos` (0700 root — CUPS rejects world-writable backends), PPD →
  `/usr/share/ppd/epsont20iv/`.
- `lpadmin -p TMT20IV-ttp -E -v epos://<printer-ip> -P …/tmt20iv.ppd -o printer-is-shared=true`
- `cupsctl _share_printers=1`; `systemctl restart cups`.

### 5. Client Mints
- Add the shared queue (DNS-SD/CUPS browsing auto, or
  `lpadmin -p tickets -E -v ipp://<server>/printers/TMT20IV-ttp`).
  No local driver — rendering happens on the server.

## Confirmed decisions
- **Language: C** (filter + backend).
- **Server:** the single Mint for now (role-based; a dedicated box is identical).
- **IPs are test-net and parameterized** — printer IP lives only in the queue's device
  URI; server is whatever host runs CUPS. Redeploy = change the URI + point clients at the
  server. See README "Portability".

## Deferred / to determine during build
- Exact printable width in dots (expect 576) — test T2.
- Max `<image>` height per request → CHUNK size — test T4.
- CUPS 1-bit raster polarity (may need inversion) — test T2.
