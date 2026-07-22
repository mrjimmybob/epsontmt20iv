# FACTS — verified ground truth for the TM-T20IV (do NOT re-test)

Everything here was proven empirically during diagnosis. A build session should trust
these and not spend print-tests rediscovering them.

## Printing path
- The printer prints **only** via **ePOS-Print over HTTPS:443**.
- Raw **port 9100 is dead**: it accepts a TCP connection but never prints and never even
  answers a real-time `DLE EOT` status query; it is **not** a TLS port either. So stock
  **Epson / gutenprint / escpr** CUPS drivers (which target 9100 or USB raw) **cannot**
  work over this network. (Windows "worked" only because Epson's driver negotiates to
  port 443 — `SelectPrintProtocol → 443` in its own logs — never 9100.)
- **ePOS `<image>` (raster) printing WORKS** — verified: a 240×80 mono image returned
  `success="true"` and printed. `<text>`, `<feed>`, `<cut>` also work.
- **Raw ESC/POS passthrough via `<command>` is BLOCKED** — returns `SchemaError` in
  base64 and hex. ⇒ the driver must send **rasterized images**, never ESC/POS bytes.

## ePOS endpoint
- URL: `https://<printer-ip>/cgi-bin/epos/service.cgi?devid=local_printer&timeout=10000`
- POST. Headers: `Content-Type: text/xml; charset=utf-8` and `SOAPAction: ""`.
- **HTTP status is ALWAYS 200**, even on failure. You MUST parse the response body for
  `success="true"`. Failure codes seen: `EX_TIMEOUT` (printer busy / mechanism not
  ready), `SchemaError` (invalid/unsupported XML).
- A healthy print reply looks like:
  `<response success="true" code="" status="251658262" battery="0" .../>`
- The `status` attribute is the Epson **ASB bitmask**. Decoding it into CUPS
  printer-state reasons is implemented in `src/status.c` (**TASKS.md → T01, done**).
- **Confirmed status values (measured on the unit by direct `curl`):**

  | Condition | `success` | `code` | `status` (dec / hex) |
  |-----------|-----------|--------|----------------------|
  | healthy (paper in) | true  | *(empty)*        | 251658262 / `0x0F000016` (print-success `0x02` set) |
  | paper out          | false | `EPTR_REC_EMPTY` | 252444700 / `0x0F0C001C` (**paper-end `0x00080000` set**, print-success cleared, offline `0x08` set) |
  | wedged mechanism   | false | `EX_TIMEOUT`     | `1` (`ASB_NO_RESPONSE`) |

  So `0x00080000` = receipt paper end is the confirmed paper-out bit. Cover-open
  (`0x20`) and cutter/mechanical bits are from Epson's SDK but **not yet physically
  confirmed on this unit**. The printer also gives a clean `code` string on failure
  (`EPTR_REC_EMPTY`, and per Epson `EPTR_COVER_OPEN`, `EPTR_AUTOMATICAL`,
  `EPTR_UNRECOVERABLE`, …) — more explicit than bit-decoding (see T01-followup).

## TLS
- Cert is **self-signed**, `CN=EPSOND400E6`. Its SAN lists the **factory** IP
  `192.168.192.128` (and `EPSOND400E6` / `.local`), **not** the real IP. So validating
  by IP fails. The backend must **skip TLS verification** (libcurl
  `CURLOPT_SSL_VERIFYPEER=0`, `CURLOPT_SSL_VERIFYHOST=0`; curl `-k`). Because only the
  server talks to the printer, this is acceptable and invisible to clients.
- The printer also returns `Access-Control-Allow-Origin: *` **and**
  `Access-Control-Allow-Private-Network: true`, so a browser on a public origin *could*
  call it directly. Not used by this driver — relevant only if an app ever prints from
  JavaScript instead of via CUPS.

## Known-good request (this exact shape printed successfully)
```xml
<?xml version="1.0" encoding="utf-8"?>
<s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/"><s:Body>
<epos-print xmlns="http://www.epson-pos.com/schemas/2011/03/epos-print">
<image width="240" height="80" align="center" color="color_1" mode="mono">BASE64…</image>
<feed line="3"/><cut type="feed"/>
</epos-print></s:Body></s:Envelope>
```

## ePOS `<image>` raster format (verified by the working test)
- **1 bit per pixel**, packed **MSB-first**, each row padded to a whole byte:
  `row_bytes = (width + 7) / 8`.
- **A set bit (1) = a black dot.** (If output prints inverted, flip this.)
- `width` = dots per line (≤ printable width). `height` = number of raster lines.
- Element text = **base64** of the packed buffer.
- Multiple `<image>` elements **stack vertically** — a long receipt is sent as several
  strips. There is a practical max height per request; currently chunked at 256 lines,
  which was a **guess and has never been measured** — see TASKS.md → T07.
- **Printable width for 80 mm @ 203 dpi = 576 dots** (72 mm) — CONFIRMED in production.

## CUPS pipeline facts (confirmed during the Chrome bring-up)
- **`print-scaling` MUST be `none`.** `install.sh` sets
  `lpadmin -o print-scaling-default=none`. **Never remove it.** With the default
  (`auto`), `cfFilterPDFToPDF` sees the 80 mm page (`226.77 pt`) as wider than the
  72 mm printable strip (`204.29 pt`), logs *"Page 1 too large for output page size,
  scaling pages to fit"*, and — compounded by a US-Letter fallback — shrank tickets to
  roughly **25%**. The symptom is a perfectly correct but tiny receipt. This cost a day
  to find.
- **Observed raster at the filter (current):** `576 x 1439, 1 bpp, colorspace=3,
  bytes/line=72` — the true 1-bit K the PPD declares. The 576 confirms the 80 mm /
  203 dpi geometry; 1439 rows is the sample PDF's own 510 pt (180 mm) page at 203 dpi.
  (Earlier it was `8 bpp, colorspace=18, 576 bytes/line` — see next bullet.)
- `colorspace=3` is `CUPS_CSPACE_K` (1 = black), matching what ePOS `<image>` wants, so
  the filter's 1-bit path copies rows straight through (no inversion). For the legacy
  8-bit case, `colorspace=18` is `CUPS_CSPACE_SW` (sGray, 0 = black) and the
  `line[x] < 128 ⇒ black` threshold handles it. Both polarities are correct.
- **T06 RESOLVED — the PPD's `ColorModel` is now honoured.** The
  `E ppdFilterLoadPPD: Unable to generate CUPS Raster sample header.` error is gone
  (`grep -c "sample header" /var/log/cups/error_log` = 0) and jobs render 1-bit as
  above (8× less data). It cleared alongside the page-geometry work (jobs now render at
  their own media size). `cupstestppd -v ppd/tmt20iv.ppd` reports NO ERRORS (only a
  cosmetic size-name warning), so if the error ever returns it's a
  `cupsRasterInterpretPPD()` quirk, not a conformance problem — TASKS.md T06 has the
  bisection plan.
- **Chrome's print preview shows the PPD's declared page height**, not the trimmed
  output. The page was shortened from 1000 mm to **300 mm (`850.39 pt`)** so the preview
  isn't an absurd ribbon. Blank-trimming still means short tickets waste no paper.
- **The cut is emitted once per JOB** (in `main()`; the per-page cut in `process_page()`
  is commented out), so a receipt that paginates prints continuously with a single cut.
  Do not move it back.
- **End-to-end verified:** Chrome → Ctrl+P → queue `TMT20IV-ttp` prints
  `sample_receipt_80mm.pdf` (logo, table, barcode, QR) at full width with a clean cut.

## Environment gotcha — CRLF
This repo round-trips through Windows. If `./install.sh` fails with
*"no such file or directory"* **even though the file exists**, the shebang has a
trailing `\r` and the kernel is looking for `/bin/sh\r`. Fix:
```bash
sed -i 's/\r$//' install.sh && chmod +x install.sh
```
A `.gitattributes` containing `* text=auto eol=lf` prevents recurrence — TASKS.md → T15.

## Reuse from tmbridge (`mrjimmybob/tmbridge`)
- `buffer.c/.h` — dynamic byte buffer with append/format. Reused as-is.
- `http.c` — libcurl POST pattern. Reused, **with its bug fixed**: it treated any HTTP
  200 as success; the backend now checks the body for `success="true"`.
- `epos.c` — SOAP-envelope wrapping (`<s:Envelope>…<epos-print>…`). Reused.
- **Do NOT reuse `escpos.c`** — the ESC/POS→ePOS translator from the old bridge;
  irrelevant to a raster driver. (For the record it had two bugs found in review: a
  `parse_barcode` off-by-one that dropped the barcode *and everything after it*, and a
  double-XML-escape that mangled accents and `&`. Not our code path here.)

## Misc
- Printer MAC `68:55:D4:2E:DA:71`.
- **Concurrency:** firing ePOS POSTs back-to-back can wedge the print mechanism
  (`EX_TIMEOUT`, `status="1"`), recoverable only by a **power-cycle**. The central CUPS
  server serializes jobs, which avoids this — do not add parallelism in the backend.
- Sanity command that prints a line + cut from any host (verify reachability on a new
  network):
  ```bash
  curl -k -X POST "https://<printer-ip>/cgi-bin/epos/service.cgi?devid=local_printer&timeout=10000" \
    -H "Content-Type: text/xml; charset=utf-8" -H 'SOAPAction: ""' \
    --data-binary '<?xml version="1.0" encoding="utf-8"?><s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/"><s:Body><epos-print xmlns="http://www.epson-pos.com/schemas/2011/03/epos-print"><text align="center">reachable</text><feed line="3"/><cut type="feed"/></epos-print></s:Body></s:Envelope>'
  ```
