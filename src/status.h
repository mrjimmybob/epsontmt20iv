#ifndef STATUS_H
#define STATUS_H

/******************************************************************************
 * status.h - decode the Epson ePOS-Print ASB status bitmask (the status="N"
 * attribute in the printer's reply) and translate it into CUPS printer-state
 * reasons. Deliberately depends only on the C standard library (no CUPS/curl),
 * so it is unit-testable off-target. See TASKS.md T01 and FACTS.md.
 ******************************************************************************/

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

typedef enum
{
    EPOS_STATE_OK = 0,      /* no error/warning bits we act on            */
    EPOS_STATE_WARNING = 1, /* printable but attention (e.g. paper low)   */
    EPOS_STATE_BLOCKED = 2  /* cannot print (paper end / cover / hard err) */
} epos_state_class_t;

/* Extract the decimal status="N" attribute from an ePOS-Print response body.
   Returns true and writes *out on success; false if absent/unparseable. */
bool epos_status_parse(const char *body, uint32_t *out);

/* Decode the bitmask and emit one "STATE: +reason" / "STATE: -reason" line per
   managed CUPS reason to err (both polarities so stale reasons are cleared),
   plus a human-readable INFO summary. Returns the overall severity so the
   caller can choose a CUPS backend exit code. */
epos_state_class_t epos_status_report(uint32_t status, FILE *err);

#endif
