/******************************************************************************
 * test_raster.c - unit/golden tests for the raster -> ePOS XML core
 * (raster.c). No CUPS deps, so it builds and runs anywhere (make test). T12.
 ******************************************************************************/

#include "raster.h"
#include "buffer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond, msg)                                      \
    do {                                                      \
        if (cond) { printf("  ok   %s\n", msg); }             \
        else      { printf("  FAIL %s\n", msg); failures++; } \
    } while (0)

static int count_substr(const char *hay, const char *needle)
{
    int n = 0;
    size_t len = strlen(needle);
    const char *p = hay;

    while ((p = strstr(p, needle)) != NULL) { n++; p += len; }
    return n;
}

/* ---- base64 ---------------------------------------------------------- */

static void test_base64(void)
{
    struct { const char *in; size_t len; const char *out; } v[] = {
        { "",    0, ""     },
        { "M",   1, "TQ==" },
        { "Ma",  2, "TWE=" },
        { "Man", 3, "TWFu" },
        { "\xff",1, "/w==" },   /* single 0xFF byte -> the receipt "all black" pixel */
    };
    size_t i;

    for (i = 0; i < sizeof v / sizeof v[0]; i++)
    {
        buffer_t b;
        char msg[48];

        buffer_init(&b);
        base64_append(&b, (const unsigned char *)v[i].in, v[i].len);
        snprintf(msg, sizeof msg, "base64 len=%zu -> \"%s\"", v[i].len, v[i].out);
        CHECK(strcmp(buffer_data(&b), v[i].out) == 0, msg);
        buffer_free(&b);
    }
}

/* ---- pack_row -------------------------------------------------------- */

static void test_pack_row(void)
{
    unsigned char out[8];
    unsigned char l8w8[8] = { 0, 0, 0, 0, 255, 255, 255, 255 }; /* first 4 black */
    unsigned char l8w4[4] = { 0, 255, 0, 255 };                 /* black,white,black,white */
    unsigned char l1[1]   = { 0xAB };

    CHECK(pack_row(out, 1, 8, l8w8, 8, 8) && out[0] == 0xF0, "8bpp width8 -> 0xF0");
    CHECK(pack_row(out, 1, 4, l8w4, 4, 8) && out[0] == 0xA0, "8bpp width4 -> 0xA0");
    CHECK(pack_row(out, 1, 8, l1,   1, 1) && out[0] == 0xAB, "1bpp -> copied 0xAB");
    CHECK(!pack_row(out, 1, 8, l1,  1, 4), "unsupported depth -> false");
}

/* ---- render_page_body ------------------------------------------------
   Pages are pre-packed 1bpp, width 8 (row_bytes = 1). An ink row = 0xFF. */

static unsigned char *page_with_ink(unsigned height, const unsigned *ink, unsigned n)
{
    unsigned char *p = calloc(height, 1);
    unsigned i;

    for (i = 0; i < n; i++)
        if (ink[i] < height)
            p[ink[i]] = 0xFF;
    return p;
}

static void test_render(void)
{
    buffer_t b;

    /* all-blank page -> just feed/cut, no image */
    {
        unsigned char *p = calloc(30, 1);
        buffer_init(&b);
        render_page_body(&b, p, 8, 30, 1);
        CHECK(strcmp(buffer_data(&b), "<feed line=\"3\"/><cut type=\"feed\"/>") == 0,
              "blank page -> feed+cut only");
        CHECK(count_substr(buffer_data(&b), "<image") == 0, "blank page -> no <image>");
        buffer_free(&b); free(p);
    }

    /* exact end-to-end golden: 1 ink row -> one <image>/w==</image> + cut */
    {
        unsigned ink[1] = { 0 };
        unsigned char *p = page_with_ink(1, ink, 1);
        buffer_init(&b);
        render_page_body(&b, p, 8, 1, 1);
        CHECK(strcmp(buffer_data(&b),
              "<image width=\"8\" height=\"1\" align=\"center\" color=\"color_1\" "
              "mode=\"mono\">/w==</image><feed line=\"3\"/><cut type=\"feed\"/>") == 0,
              "single-row golden (base64 + emit + trim + cut)");
        buffer_free(&b); free(p);
    }

    /* one ink row at 10 of 30 -> trimmed band (lead pad 8, trail pad 24 clamped) = 28 */
    {
        unsigned ink[1] = { 10 };
        unsigned char *p = page_with_ink(30, ink, 1);
        buffer_init(&b);
        render_page_body(&b, p, 8, 30, 1);
        CHECK(count_substr(buffer_data(&b), "<image") == 1, "one ink row -> one image");
        CHECK(strstr(buffer_data(&b), "width=\"8\" height=\"28\"") != NULL,
              "one ink row -> trimmed to 28 rows");
        buffer_free(&b); free(p);
    }

    /* ink at both edges -> nothing trimmed, one image of all 30 rows */
    {
        unsigned ink[2] = { 0, 29 };
        unsigned char *p = page_with_ink(30, ink, 2);
        buffer_init(&b);
        render_page_body(&b, p, 8, 30, 1);
        CHECK(count_substr(buffer_data(&b), "<image") == 1, "both edges -> one image");
        CHECK(strstr(buffer_data(&b), "height=\"30\"") != NULL, "both edges -> full 30 rows");
        buffer_free(&b); free(p);
    }

    /* 2500 inked rows -> chunked CHUNK_ROWS(1024) + 1024 + 452 */
    {
        unsigned char *p = malloc(2500);
        memset(p, 0xFF, 2500);
        buffer_init(&b);
        render_page_body(&b, p, 8, 2500, 1);
        CHECK(count_substr(buffer_data(&b), "<image") == 3, "2500 rows -> 3 chunks");
        CHECK(strstr(buffer_data(&b), "height=\"1024\"") != NULL, "chunk height 1024 present");
        CHECK(strstr(buffer_data(&b), "height=\"452\"") != NULL, "last chunk height 452");
        buffer_free(&b); free(p);
    }
}

int main(void)
{
    printf("== base64 ==\n");           test_base64();
    printf("== pack_row ==\n");         test_pack_row();
    printf("== render_page_body ==\n"); test_render();

    printf("\n%s (%d failure%s)\n", failures ? "TESTS FAILED" : "ALL TESTS PASSED",
           failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
