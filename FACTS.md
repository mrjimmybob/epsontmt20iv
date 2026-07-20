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

## TLS
- Cert is **self-signed**, `CN=EPSOND400E6`. Its SAN lists the **factory** IP
  `192.168.192.128` (and `EPSOND400E6` / `.local`), **not** the real IP. So validating
  by IP fails. The backend must **skip TLS verification** (libcurl
  `CURLOPT_SSL_VERIFYPEER=0`, `CURLOPT_SSL_VERIFYHOST=0`; curl `-k`). Because only the
  server talks to the printer, this is acceptable and invisible to clients.

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
- Multiple `<image>` elements **stack vertically** — use this to send a long receipt as
  several strips. There is a practical max height per request (memory); chunk into
  strips (start at 256 lines, tune in test T4).
- **Printable width for 80 mm @ 203 dpi ≈ 576 dots** (72 mm). CONFIRM in test T2 — some
  units are 512.

## Reuse from tmbridge (`mrjimmybob/tmbridge` / `D:\Data\Projects\Development\tmbridge`)
- `buffer.c/.h` — dynamic byte buffer with append/format. Reuse as-is (has base64-friendly
  append). Good, no known bugs.
- `http.c` — libcurl POST pattern. Reuse, **but fix the known bug**: it treats any HTTP
  200 as success. **Add a `success="true"` body check** (see endpoint note above) and map
  failure to CUPS backend exit codes.
- `epos.c` — SOAP-envelope wrapping (`<s:Envelope>…<epos-print>…`). Reuse.
- **Do NOT reuse `escpos.c`** — that's the ESC/POS→ePOS translator from the old bridge;
  irrelevant to a raster driver. (For the record it also had two bugs found in review: a
  `parse_barcode` off-by-one that dropped the barcode + everything after it, and a
  double-XML-escape that mangled accents/`&`. Not our code path here.)

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
