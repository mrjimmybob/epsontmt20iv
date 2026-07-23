# Ticket printer — troubleshooting

Epson TM-T20IV thermal ticket printer, printed to through the CUPS queue
`TMT20IV-ttp` on the print server.

There are two parts: things **anyone at the shop** can check (no commands), and a
short triage sequence **for the technician**.

---

# Part 1 — Anyone can check (no commands needed)

## Nothing comes out of the printer

Work through these in order — the first three cover most cases:

1. **Is there paper?** Open the cover and check the roll isn't empty or about to
   run out. Reload it and **close the cover firmly until it clicks.**
2. **Is the cover properly closed?** A cover that isn't fully latched stops
   printing exactly like an empty roll does.
3. **Is the printer switched on?** Check the power light. If a light is blinking
   red/orange, note what it's doing — that's useful to the technician.
4. **Is the network cable plugged in** at the back of the printer, and is the
   little light next to the socket lit?

**After reloading paper, wait about a minute.** The pending ticket should print
by itself — the system keeps retrying, you do not need to print it again. If
nothing appears after a couple of minutes, call the technician.

> Don't press "print" over and over. Repeated attempts pile up jobs and they will
> all print at once when the problem is fixed.

## Something prints, but it looks wrong

| What you see | What it means |
|---|---|
| Ticket is **tiny** — a small block of text on wide paper | Configuration problem. Call the technician (it's a one-line fix). |
| **Blank paper** feeds out and cuts | The ticket content isn't arriving. Call the technician. |
| Ticket **cut in the middle**, or several cuts | Call the technician. |
| Text is there but **faint** | Usually the paper roll is in backwards, or the roll is low quality. Try reloading it. |

## Turning it off and on

If the printer seems completely stuck — nothing prints and reloading paper
doesn't help — **switch the printer off, wait 5 seconds, switch it on.** Then try
printing one ticket. This is safe and fixes a genuinely stuck printer.

---

# Part 2 — For the technician

## The one test that splits the problem in half

Run this on the print server, replacing the IP. It talks **straight to the
printer**, bypassing CUPS entirely:

```bash
curl -k -X POST "https://<printer-ip>/cgi-bin/epos/service.cgi?devid=local_printer&timeout=10000" \
  -H "Content-Type: text/xml; charset=utf-8" -H 'SOAPAction: ""' \
  --data-binary '<?xml version="1.0" encoding="utf-8"?><s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/"><s:Body><epos-print xmlns="http://www.epson-pos.com/schemas/2011/03/epos-print"><text align="center">test ok</text><feed line="3"/><cut type="feed"/></epos-print></s:Body></s:Envelope>'
```

- **Prints, reply contains `success="true"`** → printer + network are fine. The
  problem is on the CUPS side.
- **Reply contains `EPTR_REC_EMPTY`** → out of paper.
- **Reply contains `EX_TIMEOUT` repeatedly** → the print mechanism is wedged;
  power-cycle the printer.
- **curl can't connect** → network/IP problem (see "printer IP changed" below).

## Triage commands

```bash
lpstat -p TMT20IV-ttp -l     # queue state + reason ("out of paper", "disabled since…")
lpstat -o                    # jobs stuck in the queue
lpstat -v                    # confirms the queue points at epos://<printer-ip>
```

For detail, turn logging up, reprint, then turn it back down:

```bash
sudo cupsctl LogLevel=debug
lp -d TMT20IV-ttp /tmp/sample_receipt_80mm.pdf
grep -E 'rastertotmt20iv|epos:|STATE:' /var/log/cups/error_log | tail -30
sudo cupsctl LogLevel=warn
```

Backend and filter messages go to **`/var/log/cups/error_log`**, not
`journalctl` — `journalctl -u cups` only shows the daemon itself.

## Common cases

| Symptom | Cause / fix |
|---|---|
| `lpstat` says **out of paper** | Reload paper; the held job retries automatically. If it's been longer than the retry window (see SERVER-INSTALL.md), resubmit it. |
| Queue **disabled since …** | `sudo cupsenable TMT20IV-ttp` (and `sudo cupsaccept TMT20IV-ttp`). |
| Jobs **piling up**, nothing printing | Diagnose first, then clear with `cancel -a TMT20IV-ttp`. |
| Ticket prints at **~25% size** | `print-scaling` isn't `none`: `sudo lpadmin -p TMT20IV-ttp -o print-scaling-default=none`. Never remove this setting. |
| Log shows `page width … != expected 576` | The source page isn't 80 mm wide — a page-size/CSS problem in the app sending the ticket, not the driver. |
| curl test works, CUPS prints nothing | Check the driver is installed (`dpkg -l epsontmt20iv`) and the queue URI is right (`lpstat -v`). |
| curl test fails to connect | Printer IP changed or network issue — see below. |
| Printer replies but nothing prints, `status="1"` / `EX_TIMEOUT` persists | Mechanism wedged. **Power-cycle the printer.** |

## The printer's IP changed

Point the existing queue at the new address — no reinstall:

```bash
sudo lpadmin -p TMT20IV-ttp -v epos://<new-printer-ip>
```

Then pin it (static IP or DHCP reservation) so it doesn't drift again.

## Client PCs can't print, but the server can

Client machines don't have the driver — they print to the **shared queue** on the
server. Check the server is reachable and sharing is on
(`sudo cupsctl _share_printers=1`), then re-add the queue on the client
(CLIENT-INSTALL.md).

## Background

Deeper detail — why this printer needs a custom driver, the ePOS protocol, the
verified status codes — is in **FACTS.md**.
