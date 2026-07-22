/******************************************************************************
 * epsontm20iv
 *
 * raster.c - raster -> ePOS <image> conversion core (base64, row packing,
 * strip emission, blank-trim + chunking). No CUPS dependency, so it is
 * unit-testable off-target. See raster.h and tests/test_raster.c (T12).
 ******************************************************************************/

#include "raster.h"

#include <stdio.h>
#include <string.h>

static const char BASE64_ALPHABET[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/*****************************************************************************/

bool base64_append(buffer_t *out, const unsigned char *data, size_t len)
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

bool pack_row(unsigned char *out_row, size_t row_bytes, unsigned width,
              const unsigned char *line, size_t src_bytes_per_line, unsigned bits_per_pixel)
{
    if (bits_per_pixel == 1)
    {
        size_t n = (row_bytes < src_bytes_per_line) ? row_bytes : src_bytes_per_line;

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

    if (bits_per_pixel == 8)
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

    fprintf(stderr, "ERROR: rastertotmt20iv: unsupported bits_per_pixel=%u\n", bits_per_pixel);

    return false;
}

/*****************************************************************************/

bool emit_strip(buffer_t *body, unsigned width, const unsigned char *strip, unsigned rows, size_t row_bytes)
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

bool render_page_body(buffer_t *body, const unsigned char *page, unsigned width,
                      unsigned height, size_t row_bytes)
{
    unsigned first_ink_row = 0;
    unsigned last_ink_row = 0;
    bool any_ink = false;
    unsigned start_row;
    unsigned end_row;
    unsigned emit_rows;
    unsigned y;
    bool ok = true;

    /* find the inked band */
    for (y = 0; y < height; y++)
    {
        const unsigned char *row = page + (size_t)y * row_bytes;
        size_t k;

        for (k = 0; k < row_bytes; k++)
        {
            if (row[k] != 0)
            {
                if (!any_ink)
                    first_ink_row = y;

                last_ink_row = y;
                any_ink = true;
                break;
            }
        }
    }

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

    /* T02: cut after every page. CUPS produces N copies as N duplicated pages, so a
       per-page cut yields one separately-cut receipt per copy. A receipt is a
       single-page job, so this never slices a logical receipt. */
    if (ok)
        ok = buffer_append_string(body, "<feed line=\"3\"/><cut type=\"feed\"/>");

    return ok;
}
