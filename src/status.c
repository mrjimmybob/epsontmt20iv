/******************************************************************************
 * epsontmt20iv
 *
 * status.c - decode the Epson ePOS-Print ASB status bitmask into CUPS
 *            printer-state reasons. Pure C stdlib; no CUPS/curl (unit-testable).
 *
 * The status="N" value in the ePOS-Print reply is the Epson Automatic Status
 * Back bitmask. A healthy print returns 251658262 = 0x0F000016 (print-success
 * + drawer state + fixed high bits, no error bits). See FACTS.md / TASKS.md T01.
 * The individual bit meanings below are per Epson's ePOS SDK; confirm the ones
 * that matter (paper end / near end / cover) against the real printer.
 ******************************************************************************/

#include "status.h"

#include <stdlib.h>
#include <string.h>

/* ePOS-Print ASB status bits. */
#define ASB_NO_RESPONSE     0x00000001u
#define ASB_PRINT_SUCCESS   0x00000002u
#define ASB_DRAWER_KICK     0x00000004u
#define ASB_OFFLINE         0x00000008u
#define ASB_COVER_OPEN      0x00000020u
#define ASB_PAPER_FEED      0x00000040u
#define ASB_WAIT_ONLINE     0x00000100u
#define ASB_PANEL_SWITCH    0x00000200u
#define ASB_MECH_ERR        0x00000400u
#define ASB_CUTTER_ERR      0x00000800u
#define ASB_UNRECOVER_ERR   0x00002000u
#define ASB_AUTORECOVER_ERR 0x00004000u
#define ASB_PAPER_NEAR_END  0x00020000u
#define ASB_PAPER_END       0x00080000u

#define ASB_HARD_ERR (ASB_MECH_ERR | ASB_CUTTER_ERR | ASB_UNRECOVER_ERR)

/*****************************************************************************/

bool epos_status_parse(const char *body, uint32_t *out)
{
    static const char KEY[] = "status=\"";
    const char *p;
    char *end;
    unsigned long v;

    if (body == NULL || out == NULL)
        return false;

    p = strstr(body, KEY);

    if (p == NULL)
        return false;

    p += sizeof(KEY) - 1;

    v = strtoul(p, &end, 10);

    if (end == p) /* no digits after status=" */
        return false;

    *out = (uint32_t)v;

    return true;
}

/*****************************************************************************/

epos_state_class_t epos_status_report(uint32_t status, FILE *err)
{
    static const struct
    {
        uint32_t mask;
        const char *reason;
        epos_state_class_t cls;
    } MAP[] = {
        { ASB_PAPER_END,      "media-empty-error", EPOS_STATE_BLOCKED },
        { ASB_PAPER_NEAR_END, "media-low-warning", EPOS_STATE_WARNING },
        { ASB_COVER_OPEN,     "cover-open-error",  EPOS_STATE_BLOCKED },
    };

    epos_state_class_t worst = EPOS_STATE_OK;
    bool hard;
    size_t i;

    for (i = 0; i < sizeof(MAP) / sizeof(MAP[0]); i++)
    {
        bool on = (status & MAP[i].mask) != 0;

        fprintf(err, "STATE: %c%s\n", on ? '+' : '-', MAP[i].reason);

        if (on && MAP[i].cls > worst)
            worst = MAP[i].cls;
    }

    /* Mechanical / cutter / unrecoverable errors collapse to one CUPS reason. */
    hard = (status & ASB_HARD_ERR) != 0;

    fprintf(err, "STATE: %cother-error\n", hard ? '+' : '-');

    if (hard)
        worst = EPOS_STATE_BLOCKED;

    fprintf(err,
            "INFO: epos: printer status 0x%08X%s%s%s%s%s%s%s\n",
            (unsigned)status,
            (status & ASB_PAPER_END)     ? " paper-end"      : "",
            (status & ASB_PAPER_NEAR_END)? " paper-low"      : "",
            (status & ASB_COVER_OPEN)    ? " cover-open"     : "",
            (status & ASB_OFFLINE)       ? " offline"        : "",
            (status & ASB_CUTTER_ERR)    ? " cutter-error"   : "",
            (status & (ASB_MECH_ERR | ASB_UNRECOVER_ERR)) ? " mech-error" : "",
            (status & ASB_NO_RESPONSE)   ? " no-response"    : "");

    return worst;
}
