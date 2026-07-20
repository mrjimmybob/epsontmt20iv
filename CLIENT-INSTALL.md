# Adding the ticket printer on a client Mint

This is for any Linux Mint machine that should print tickets, other than the
print-server itself (currently `192.168.1.51`). No driver, PPD, or C code is
installed on clients - all rendering happens on the server; a client just
points at the shared queue like any other network printer.

## Option A - automatic discovery (try this first)

1. Open **Printers** (Settings > Printers, or `system-config-printers`).
2. If the server and client are on the same subnet and mDNS/DNS-SD works,
   the queue should appear on its own as something like
   `TMT20IV-ttp @ <server>`. Select it, set as default
   if desired, done.

If it doesn't appear within a few seconds, use Option B.

## Option B - add it manually

```bash
sudo lpadmin -p tickets -E \
    -v ipp://192.168.1.51/printers/TMT20IV-ttp
```

- `tickets` is just the local name for the queue on this client - call it
  whatever's convenient.
- `192.168.1.51` is the print-server's address on **this** network - if the
  client network differs from this test network, use the server's actual
  address there instead.
- The path after `/printers/` must match the server's queue name exactly.
  Confirm it on the server with `lpstat -a` if this doesn't work.

## Verify

```bash
lp -d tickets /usr/share/cups/data/testprint
```

Should print a normal test page on the thermal printer via the server.

## Troubleshooting

- Nothing shows up via discovery: on the **server**, confirm
  `cupsctl | grep share` shows `_share_printers=1`, and that `cups-browsed`
  is running (`systemctl status cups-browsed`).
- `lp` reports "client-error-not-found": the queue name in the `-v` URI
  doesn't match the server's current queue name - re-check with `lpstat -a`
  on the server.
- Printing silently does nothing / times out: the server itself may be
  unreachable from this client (routing/firewall) - confirm with `ping
  192.168.1.51` first, independent of printing.
