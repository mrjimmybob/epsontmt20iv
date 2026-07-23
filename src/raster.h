#ifndef RASTER_H
#define RASTER_H

/******************************************************************************
 * raster.h - the raster -> ePOS <image> conversion core, decoupled from CUPS
 * so it is unit-testable off-target (see tests/test_raster.c, TASKS.md T12).
 * rastertotmt20iv.c does the CUPS I/O and calls these; the wire format is
 * 1 bpp, MSB-first, bit=1 => black dot, rows padded to a byte (FACTS.md).
 ******************************************************************************/

#include <stdbool.h>
#include <stddef.h>

#include "buffer.h"

/* Tuning knobs (also referenced by the filter and the tests). */
#define CHUNK_ROWS        1024  /* rows per <image> strip. T07 confirmed a single
                                   request of >=3600 dots (~450mm, ~338KB) prints, so
                                   1024 is a safe margin and cuts the chunk count 4x. */
#define INVERT_BITS          0  /* flip to 1 if output prints inverted */
#define PRINTER_WIDTH_DOTS 576  /* 80mm @ 203dpi printable width, per FACTS.md */
#define TRIM_PAD_ROWS       24  /* ~3mm blank kept after the last ink row */
#define LEAD_PAD_ROWS        8  /* ~1mm blank kept before the first ink row */

/* base64-encode `len` bytes of `data` onto `out`. */
bool base64_append(buffer_t *out, const unsigned char *data, size_t len);

/* Pack one source raster row into a 1-bpp, MSB-first output row (bit=1 => black).
   bits_per_pixel is 1 (copied straight) or 8 (grey, thresholded at 128).
   src_bytes_per_line is the length of `line`. Returns false on unsupported depth. */
bool pack_row(unsigned char *out_row, size_t row_bytes, unsigned width,
              const unsigned char *line, size_t src_bytes_per_line, unsigned bits_per_pixel);

/* Emit one <image ...>base64</image> strip of `rows` rows. */
bool emit_strip(buffer_t *body, unsigned width, const unsigned char *strip,
                unsigned rows, size_t row_bytes);

/* Transform a packed 1-bpp page (width x height, row_bytes per row) into the ePOS
   body: trim blank leading/trailing rows, chunk into <image> strips, append the
   per-page <feed>/<cut>. This is the whole raster->XML core (T12 tests target it). */
bool render_page_body(buffer_t *body, const unsigned char *page, unsigned width,
                      unsigned height, size_t row_bytes);

#endif
