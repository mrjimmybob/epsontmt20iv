# BUILD — runbook (run on the print-server Mint)

Prereq context: **FACTS.md** (ground truth) and **SPEC.md** (design). Language: C.

## 0. Dependencies
```bash
sudo apt update
sudo apt install cups cups-filters libcups2-dev libcupsimage2-dev \
                 libcurl4-openssl-dev build-essential ghostscript
```

## 1. Project layout (create under this folder on the Mint)
```
epsont20iv/
  src/
    rastertotmt20iv.c   # filter : CUPS raster -> ePOS <image> XML on stdout
    epos_backend.c      # backend: read XML on stdin -> HTTPS POST -> success check
    buffer.c  buffer.h  # copied from tmbridge (as-is)
    http.c    http.h    # copied from tmbridge (FIX: add success="true" check)
    epos.c    epos.h    # copied from tmbridge (SOAP wrapping)
  ppd/tmt20iv.ppd
  Makefile
  install.sh
```
Copy the reusable tmbridge sources first:
```bash
cp ../tmbridge/src/{buffer.c,buffer.h,http.c,http.h,epos.c,epos.h} src/
```

## 2. Filter `rastertotmt20iv.c` (libcupsimage)
- CUPS calls: `rastertotmt20iv job-id user title copies options [file]`; raster is on fd 0.
- Skeleton flow:
  ```c
  cups_raster_t *ras = cupsRasterOpen(0, CUPS_RASTER_READ);
  cups_page_header2_t h;
  while (cupsRasterReadHeader2(ras, &h)) {
      unsigned W = h.cupsWidth;             // dots per line (expect ~576)
      size_t rb = (W + 7) / 8;              // bytes per output row
      // read h.cupsHeight lines of h.cupsBytesPerLine each
      // build 1-bpp rows: bit=1 => black (see FACTS; invert if needed)
      // every <= CHUNK rows -> emit <image W x n> base64 (reuse buffer.c base64)
  }
  // append <feed line="2"/><cut type="feed"/>, wrap SOAP+epos-print, write stdout
  ```
- If `h.cupsBitsPerPixel == 8` (gray) threshold at ~128; if already 1-bit, mind polarity.
- CHUNK: start 256 rows; tune in T4.

## 3. Backend `epos_backend.c` (installed as `/usr/lib/cups/backend/epos`)
- `if (argc == 1) { puts("network epos \"Unknown\" \"Epson TM-T20IV ePOS\""); return 0; }`
- Else: read job (the filter's XML) from stdin; get `epos://host[:port]` from
  `getenv("DEVICE_URI")`; POST to
  `https://host/cgi-bin/epos/service.cgi?devid=local_printer&timeout=10000`.
- libcurl: `VERIFYPEER=0`, `VERIFYHOST=0`, header `Content-Type: text/xml; charset=utf-8`,
  `SOAPAction: ""`. Capture response body.
- Result mapping (CUPS backend exit codes):
  - body has `success="true"` → `return CUPS_BACKEND_OK;` (0)
  - body has `EX_TIMEOUT` → `return CUPS_BACKEND_RETRY;` (printer busy — CUPS re-queues)
  - else (incl. `SchemaError`, curl error) → `fprintf(stderr,"ERROR: …\n"); return CUPS_BACKEND_FAILED;`
- Reuse tmbridge `http.c` for the POST, applying the success-check fix.

## 4. PPD `ppd/tmt20iv.ppd` (essentials)
- `*ModelName`, `*NickName: "Epson TM-T20IV 80mm ePOS"`, `*cupsVersion`.
- `*DefaultResolution: 203dpi`; `*ColorModel Gray/Grayscale: "…"` 1-bit.
- Custom page size 576 dots wide, continuous length (large `*MaxMediaHeight`), 0 margins.
- `*cupsFilter: "application/vnd.cups-raster 100 rastertotmt20iv"`.
- (Simplest: start from a generic CUPS raster PPD and trim to these.)

## 5. Makefile + install
```make
CFLAGS = -O2 -Wall -Wextra $(shell pkg-config --cflags libcurl) $(shell cups-config --cflags)
LDLIBS = $(shell pkg-config --libs libcurl) $(shell cups-config --libs) -lcupsimage
```
`install.sh` (run with sudo):
```bash
make
install -m 0755 -o root -g root rastertotmt20iv /usr/lib/cups/filter/rastertotmt20iv
install -m 0700 -o root -g root epos            /usr/lib/cups/backend/epos
install -Dm 0644 ppd/tmt20iv.ppd /usr/share/ppd/epsont20iv/tmt20iv.ppd
lpadmin -p TMT20IV-ttp -E \
        -v epos://<printer-ip> -P /usr/share/ppd/epsont20iv/tmt20iv.ppd \
        -o printer-is-shared=true
cupsctl _share_printers=1
systemctl restart cups
```
> Replace `<printer-ip>` with the printer's address on the current network (test: 192.168.1.50).

## 6. Test plan (escalate — stop and fix at the first failure)
- **T1** minimal: `lp -d TMT20IV-ttp /usr/share/cups/data/testprint`
  (or any small PDF) → does anything print? Watch `journalctl -u cups -f` and the backend
  stderr. Confirms filter+backend+POST chain end-to-end.
- **T2** width/threshold/polarity: print a page with a full-width box + ruler → confirm
  576-dot width, no clipping, correct density, not inverted. Adjust W/threshold/polarity.
- **T3** real ticket: an HTML shaped like the client examples, Chrome → Ctrl+P → this
  queue → confirm layout, and that **barcode + QR rasterize sharply enough to scan**
  (native width, 203 dpi, no shrink).
- **T4** long receipt → confirm `<image>` chunking works and there's exactly one clean cut
  at the end. Tune CHUNK.
- **T5** shared: `cupsctl _share_printers=1`; from a second Mint add
  `ipp://<server>/printers/TMT20IV-ttp`; print from both at once →
  confirm CUPS serializes and the printer doesn't wedge.

## 7. Client Mints (no driver needed)
```bash
lpadmin -p tickets -E -v ipp://<server>/printers/TMT20IV-ttp
```
(or let DNS-SD/CUPS browsing discover it automatically).

## Debugging tips
- `cupsctl LogLevel=debug2`; watch `/var/log/cups/error_log`.
- Test the filter alone: feed it a raster file and inspect the XML on stdout before
  involving the backend.
- Test the backend alone: `DEVICE_URI=epos://<printer-ip> ./epos 1 user title 1 '' < job.xml`.
- If the printer wedges (`EX_TIMEOUT`/`status="1"` persists): power-cycle it (see FACTS).
