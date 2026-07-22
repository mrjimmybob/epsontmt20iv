/******************************************************************************
 * epsontm20iv
 *
 * rastertotmt20iv.c - CUPS raster filter: reads a CUPS raster stream and
 * emits the inner ePOS <epos-print> body (chunked <image> strips + a
 * <feed>/<cut>) on stdout. The backend (epos_backend.c) wraps this in the
 * SOAP envelope and POSTs it - see FACTS.md for the wire format.
 *
 * This file is the CUPS glue only; the raster -> XML conversion lives in
 * raster.c (raster.h), which has no CUPS dependency so it can be unit-tested
 * off-target (tests/test_raster.c, T12).
 *
 * The PPD declares a generous page so callers never size a page to their
 * ticket. To avoid feeding a receipt's worth of blank tail paper, the core
 * trims to the last inked row (plus a small pad); an all-blank page still
 * gets a feed+cut so the job completes cleanly.
 ******************************************************************************/

#include "buffer.h"
#include "raster.h"

#include <cups/raster.h>

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/*****************************************************************************/

static bool process_page(cups_raster_t *ras, cups_page_header2_t *header, buffer_t *body)
{
    unsigned width = header->cupsWidth;
    unsigned height = header->cupsHeight;
    size_t row_bytes;
    unsigned char *line;
    unsigned char *page;
    unsigned y;
    bool ok = true;

    if (width == 0 || height == 0)
    {
        fprintf(stderr, "ERROR: rastertotmt20iv: empty page (width=%u height=%u)\n", width, height);
        return false;
    }

    if (width > PRINTER_WIDTH_DOTS)
    {
        fprintf(stderr, "WARN: rastertotmt20iv: page width %u exceeds printer max %u dots, clamping\n",
                width, PRINTER_WIDTH_DOTS);
        width = PRINTER_WIDTH_DOTS;
    }
    else if (width != PRINTER_WIDTH_DOTS)
    {
        /* T03: a narrower-than-expected raster is the "prints too small" failure -
           it used to pass through silently. Print it (better than nothing) but make
           the misconfiguration loud in the log so it can't hide again. */
        fprintf(stderr,
                "WARN: rastertotmt20iv: page width %u dots != expected %u; ticket will "
                "print narrow. Check the queue's page size, 203dpi resolution, and "
                "print-scaling=none (see FACTS.md).\n",
                width, PRINTER_WIDTH_DOTS);
    }

    row_bytes = (width + 7) / 8;

    /* T08: log resolution and page height too. Resolution is the one number that
       would have identified the "prints too small" bug instantly (it was a scaling
       issue, not a width issue). Height in mm makes long-receipt/pagination debugging
       obvious. */
    fprintf(stderr,
            "DEBUG: rastertotmt20iv: page %ux%u (emit width %u), %u bpp, colorspace=%d, "
            "bytes/line=%u, res=%ux%udpi, height=%.1fmm\n",
            header->cupsWidth, height, width, header->cupsBitsPerPixel, header->cupsColorSpace,
            header->cupsBytesPerLine, header->HWResolution[0], header->HWResolution[1],
            header->HWResolution[1] ? (double)height * 25.4 / (double)header->HWResolution[1] : 0.0);

    line = malloc(header->cupsBytesPerLine);
    page = malloc((size_t)row_bytes * height);

    if (line == NULL || page == NULL)
    {
        fprintf(stderr, "ERROR: rastertotmt20iv: out of memory\n");
        free(line);
        free(page);
        return false;
    }

    /* Read every source row, packing it into the 1-bpp page buffer. */
    for (y = 0; y < height; y++)
    {
        unsigned char *out_row = page + (size_t)y * row_bytes;

        if (cupsRasterReadPixels(ras, line, header->cupsBytesPerLine) == 0)
        {
            fprintf(stderr, "ERROR: rastertotmt20iv: short raster read at line %u/%u\n", y, height);
            ok = false;
            break;
        }

        if (!pack_row(out_row, row_bytes, width, line,
                      header->cupsBytesPerLine, header->cupsBitsPerPixel))
        {
            ok = false;
            break;
        }
    }

    free(line);

    /* Convert the packed page to the ePOS body (trim + chunk + cut). */
    if (ok)
        ok = render_page_body(body, page, width, height, row_bytes);

    free(page);

    return ok;
}

/*****************************************************************************/

int main(int argc, char *argv[])
{
    cups_raster_t *ras;
    cups_page_header2_t header;
    buffer_t body;
    int fd;
    int status = 0;

    if (argc < 6 || argc > 7)
    {
        fprintf(stderr, "ERROR: Usage: rastertotmt20iv job-id user title copies options [file]\n");
        return 1;
    }

    /* T02: with cupsManualCopies True, CUPS produces copies by handing us the page
       duplicated N times, so this filter just emits the pages it is given, one cut
       each. It must NOT multiply by argv[4] (that was the 3x3=9 double-count bug). */

    fd = (argc == 7) ? open(argv[6], O_RDONLY) : 0;

    if (fd < 0)
    {
        fprintf(stderr, "ERROR: rastertotmt20iv: unable to open '%s'\n", argv[6]);
        return 1;
    }

    ras = cupsRasterOpen(fd, CUPS_RASTER_READ);

    if (ras == NULL)
    {
        fprintf(stderr, "ERROR: rastertotmt20iv: cupsRasterOpen failed\n");
        if (fd != 0)
            close(fd);
        return 1;
    }

    if (!buffer_init(&body))
    {
        fprintf(stderr, "ERROR: rastertotmt20iv: out of memory\n");
        cupsRasterClose(ras);
        if (fd != 0)
            close(fd);
        return 1;
    }

    while (cupsRasterReadHeader2(ras, &header))
    {
        if (!process_page(ras, &header, &body))
        {
            status = 1;
            break;
        }
    }

    cupsRasterClose(ras);

    if (fd != 0)
        close(fd);

    if (status == 0)
        fwrite(buffer_data(&body), 1, buffer_length(&body), stdout);

    buffer_free(&body);

    return status;
}
