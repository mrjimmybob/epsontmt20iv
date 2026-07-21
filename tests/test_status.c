/******************************************************************************
 * test_status.c - unit tests for the ASB status decoder (TASKS.md T01/T12).
 * No CUPS/curl deps, so it builds and runs anywhere (make test).
 ******************************************************************************/

#include "status.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond, msg)                                            \
    do {                                                            \
        if (cond) { printf("  ok   %s\n", msg); }                   \
        else      { printf("  FAIL %s\n", msg); failures++; }       \
    } while (0)

/* Capture epos_status_report's stderr-style output into buf via a temp file. */
static epos_state_class_t report_to_buf(uint32_t status, char *buf, size_t cap)
{
    epos_state_class_t cls;
    FILE *fp = tmpfile();
    long n;

    buf[0] = '\0';
    if (fp == NULL)
        return EPOS_STATE_OK;

    cls = epos_status_report(status, fp);
    fflush(fp);
    rewind(fp);
    n = (long)fread(buf, 1, cap - 1, fp);
    if (n < 0) n = 0;
    buf[n] = '\0';
    fclose(fp);
    return cls;
}

#define HEALTHY   251658262u          /* 0x0F000016, the known-good value   */
#define PAPER_END 0x00080000u
#define NEAR_END  0x00020000u
#define COVER     0x00000020u
#define CUTTER    0x00000800u

int main(void)
{
    uint32_t v;
    char out[1024];
    epos_state_class_t cls;

    printf("== parse ==\n");
    CHECK(epos_status_parse("<response success=\"true\" code=\"\" status=\"251658262\" battery=\"0\"/>", &v)
          && v == HEALTHY, "parse healthy status attribute");
    CHECK(epos_status_parse("...status=\"1\"...", &v) && v == 1u, "parse status=1");
    CHECK(!epos_status_parse("<response success=\"true\"/>", &v), "missing status -> false");
    CHECK(!epos_status_parse(NULL, &v), "NULL body -> false");

    printf("== healthy ==\n");
    cls = report_to_buf(HEALTHY, out, sizeof(out));
    CHECK(cls == EPOS_STATE_OK, "healthy -> OK");
    CHECK(strstr(out, "STATE: -media-empty-error") != NULL, "healthy clears media-empty");
    CHECK(strstr(out, "STATE: -cover-open-error") != NULL, "healthy clears cover-open");
    CHECK(strstr(out, "+media-empty-error") == NULL, "healthy does not raise paper-end");

    printf("== paper end ==\n");
    cls = report_to_buf(HEALTHY | PAPER_END, out, sizeof(out));
    CHECK(cls == EPOS_STATE_BLOCKED, "paper-end -> BLOCKED");
    CHECK(strstr(out, "STATE: +media-empty-error") != NULL, "paper-end raises media-empty-error");

    printf("== paper near end ==\n");
    cls = report_to_buf(HEALTHY | NEAR_END, out, sizeof(out));
    CHECK(cls == EPOS_STATE_WARNING, "near-end -> WARNING (still printable)");
    CHECK(strstr(out, "STATE: +media-low-warning") != NULL, "near-end raises media-low-warning");
    CHECK(strstr(out, "+media-empty-error") == NULL, "near-end is not paper-end");

    printf("== cover open ==\n");
    cls = report_to_buf(HEALTHY | COVER, out, sizeof(out));
    CHECK(cls == EPOS_STATE_BLOCKED, "cover-open -> BLOCKED");
    CHECK(strstr(out, "STATE: +cover-open-error") != NULL, "cover-open raises cover-open-error");

    printf("== cutter/mechanical ==\n");
    cls = report_to_buf(HEALTHY | CUTTER, out, sizeof(out));
    CHECK(cls == EPOS_STATE_BLOCKED, "cutter error -> BLOCKED");
    CHECK(strstr(out, "STATE: +other-error") != NULL, "cutter error raises other-error");

    printf("\n%s (%d failure%s)\n", failures ? "TESTS FAILED" : "ALL TESTS PASSED",
           failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
