/******************************************************************************
 * epsontm20iv
 *
 * rastertotmt20iv.c - CUPS raster filter: reads a CUPS raster stream and
 * emits the inner ePOS <epos-print> body (chunked <image> strips + a
 * <feed>/<cut>) on stdout. The backend (epos_backend.c) wraps this in the
 * SOAP envelope and POSTs it - see FACTS.md for the wire format this must
 * produce (1 bpp, MSB-first, bit=1 => black dot, row padded to a byte).
 *
 * The PPD declares a generous fixed page (continuous-roll style) so callers
 * never need to size a page to their ticket - like printing to any other
 * driver. To avoid feeding/printing a receipt's worth of blank tail paper
 * on every short ticket, this filter buffers the full page, finds the last
 * row that actually has ink, and only emits/cuts through that point (plus a
 * small pad). A page that's all blank (no ink) still gets a feed+cut so the
 * job completes cleanly.
 ******************************************************************************/

#include "buffer.h"

#include <cups/raster.h>

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CHUNK_ROWS         256  /* rows per <image> strip; tune in test T4 */
#define INVERT_BITS          0  /* flip to 1 if T2 shows output printing inverted */
#define PRINTER_WIDTH_DOTS 576  /* physical max per FACTS.md - never exceed */
#define TRIM_PAD_ROWS       24  /* ~3mm of blank kept after the last ink row */
#define LEAD_PAD_ROWS        8  /* ~1mm of blank kept before the first ink row */
#define MAX_COPIES          50  /* clamp argv[4]; PPD sets cupsManualCopies True */

static const char BASE64_ALPHABET[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/*****************************************************************************/

static bool base64_append(buffer_t *out, const unsigned char *data, size_t len)
{
    size_t i;
    size_t rem;
    char b[4];

    for (i = 0; i + 3 <= len; i += 3)
    {
        unsigned v = ((unsigned)data[i] << 16) | ((unsigned)data[i + 1] << 8) | data[i + 2];

        b[0] = BASE64_ALPHABET[(v >> 18) & 0x3f];
        b[1] = BASE64_ALPHABET[(v >> 12) & 0x3f];
        b[2] = BASE64_ALPHABET[(v >> 6) & 0x3f];
        b[3] = BASE64_ALPHABET[v & 0x3f];

        if (!buffer_append(out, b, 4))
            return false;
    }

    rem = len - i;

    if (rem == 1)
    {
        unsigned v = (unsigned)data[i] << 16;

        b[0] = BASE64_ALPHABET[(v >> 18) & 0x3f];
        b[1] = BASE64_ALPHABET[(v >> 12) & 0x3f];
        b[2] = '=';
        b[3] = '=';

        if (!buffer_append(out, b, 4))
            return false;
    }
    else if (rem == 2)
    {
        unsigned v = ((unsigned)data[i] << 16) | ((unsigned)data[i + 1] << 8);

        b[0] = BASE64_ALPHABET[(v >> 18) & 0x3f];
        b[1] = BASE64_ALPHABET[(v >> 12) & 0x3f];
        b[2] = BASE64_ALPHABET[(v >> 6) & 0x3f];
        b[3] = '=';

        if (!buffer_append(out, b, 4))
            return false;
    }

    return true;
}

/*****************************************************************************/

static bool pack_row(unsigned char *out_row, size_t row_bytes, unsigned width,
                      const unsigned char *line, cups_page_header2_t *header)
{
    if (header->cupsBitsPerPixel == 1)
    {
        size_t n = (row_bytes < header->cupsBytesPerLine) ? row_bytes : header->cupsBytesPerLine;

        memcpy(out_row, line, n);

        if (n < row_bytes)
            memset(out_row + n, 0, row_bytes - n);

        if (INVERT_BITS)
        {
            size_t k;

            for (k = 0; k < row_bytes; k++)
                out_row[k] = (unsigned char)~out_row[k];
        }

        return true;
    }

    if (header->cupsBitsPerPixel == 8)
    {
        unsigned x;

        memset(out_row, 0, row_bytes);

        for (x = 0; x < width; x++)
        {
            int black = line[x] < 128;

            if (INVERT_BITS)
                black = !black;

            if (black)
                out_row[x / 8] |= (unsigned char)(0x80 >> (x % 8));
        }

        return true;
    }

    fprintf(stderr, "ERROR: rastertotmt20iv: unsupported cupsBitsPerPixel=%u\n",
            header->cupsBitsPerPixel);

    return false;
}

/*****************************************************************************/

static bool emit_strip(buffer_t *body, unsigned width, const unsigned char *strip, unsigned rows, size_t row_bytes)
{
    if (!buffer_append_format(body,
            "<image width=\"%u\" height=\"%u\" align=\"center\" color=\"color_1\" mode=\"mono\">",
            width, rows))
        return false;

    if (!base64_append(body, strip, (size_t)rows * row_bytes))
        return false;

    return buffer_append_string(body, "</image>");
}

/*****************************************************************************/

static bool process_page(cups_raster_t *ras, cups_page_header2_t *header, buffer_t *body)
{
    unsigned width = header->cupsWidth;
    unsigned height = header->cupsHeight;
    size_t row_bytes;
    unsigned char *line;
    unsigned char *page;
    unsigned y;
    unsigned first_ink_row;
    unsigned last_ink_row;
    bool any_ink;
    unsigned start_row;
    unsigned end_row;
    unsigned emit_rows;
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

    fprintf(stderr,
            "DEBUG: rastertotmt20iv: page %ux%u (emit width %u), %u bpp, colorspace=%d, bytes/line=%u\n",
            header->cupsWidth, height, width, header->cupsBitsPerPixel, header->cupsColorSpace,
            header->cupsBytesPerLine);

    line = malloc(header->cupsBytesPerLine);
    page = malloc((size_t)row_bytes * height);

    if (line == NULL || page == NULL)
    {
        fprintf(stderr, "ERROR: rastertotmt20iv: out of memory\n");
        free(line);
        free(page);
        return false;
    }

    first_ink_row = 0;
    last_ink_row = 0;
    any_ink = false;

    for (y = 0; y < height; y++)
    {
        unsigned char *out_row = page + (size_t)y * row_bytes;
        size_t k;

        if (cupsRasterReadPixels(ras, line, header->cupsBytesPerLine) == 0)
        {
            fprintf(stderr, "ERROR: rastertotmt20iv: short raster read at line %u/%u\n", y, height);
            ok = false;
            break;
        }

        if (!pack_row(out_row, row_bytes, width, line, header))
        {
            ok = false;
            break;
        }

        for (k = 0; k < row_bytes; k++)
        {
            if (out_row[k] != 0)
            {
                if (!any_ink)
                    first_ink_row = y;

                last_ink_row = y;
                any_ink = true;
                break;
            }
        }
    }

    free(line);

    if (ok)
    {
        if (!any_ink)
        {
            /* Nothing printed on this page - just feed/cut, don't send blank strips. */
            start_row = 0;
            end_row = 0;
        }
        else
        {
            unsigned padded_end = last_ink_row + 1 + TRIM_PAD_ROWS;

            start_row = (first_ink_row > LEAD_PAD_ROWS) ? first_ink_row - LEAD_PAD_ROWS : 0;
            end_row = (padded_end < height) ? padded_end : height;
        }

        emit_rows = end_row - start_row;

        for (y = start_row; y < end_row && ok; y += CHUNK_ROWS)
        {
            unsigned rows = end_row - y;

            if (rows > CHUNK_ROWS)
                rows = CHUNK_ROWS;

            ok = emit_strip(body, width, page + (size_t)y * row_bytes, rows, row_bytes);
        }

        fprintf(stderr, "DEBUG: rastertotmt20iv: emitted %u of %u rows (trimmed leading/trailing blank)\n",
                emit_rows, height);
    }

    free(page);

    /* Let move it out of the way so the backend can wrap it in the SOAP envelope and POST it.
    
    if (ok) {
        ok = buffer_append_string(body, "<feed line=\"3\"/><cut type=\"feed\"/>");
    }
    */    

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
    long copies;

    if (argc < 6 || argc > 7)
    {
        fprintf(stderr, "ERROR: Usage: rastertotmt20iv job-id user title copies options [file]\n");
        return 1;
    }

    /* T02: the PPD declares cupsManualCopies True, so CUPS passes the requested
       copy count in argv[4] and expects us to produce them. Parse and clamp it. */
    copies = strtol(argv[4], NULL, 10);
    if (copies < 1)
        copies = 1;
    if (copies > MAX_COPIES)
    {
        fprintf(stderr, "WARN: rastertotmt20iv: %ld copies requested, clamping to %d\n",
                copies, MAX_COPIES);
        copies = MAX_COPIES;
    }

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

    unsigned pages = 0;
    while (cupsRasterReadHeader2(ras, &header))
    {
        if (!process_page(ras, &header, &body))
        {
            status = 1;
            break;
        }
        pages++;
    }

    if (status == 0 && pages > 0)
    {
        if (!buffer_append_string(&body, "<feed line=\"3\"/><cut type=\"feed\"/>"))
            status = 1;
    }

    cupsRasterClose(ras);

    if (fd != 0)
        close(fd);

    /* T02: `body` is one complete receipt (all pages' images + a trailing feed/cut),
       so each copy is separately cut. Emit it `copies` times into the single ePOS
       body the backend POSTs. An empty job (pages == 0) has an empty body, so this
       writes nothing - correct. */
    if (status == 0)
    {
        long c;

        for (c = 0; c < copies; c++)
            fwrite(buffer_data(&body), 1, buffer_length(&body), stdout);
    }

    buffer_free(&body);

    return status;
}
