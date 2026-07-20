#ifndef EPOS_H
#define EPOS_H

#include <stdbool.h>

#include "buffer.h"

/* xml is the inner <epos-print> body (image/feed/cut elements); this wraps it
   in the SOAP envelope, POSTs it, and checks the reply for success="true".
   If response_out is non-NULL (and buffer_init'd by the caller), the raw
   reply body is left in it so the caller can classify a failure (e.g. look
   for "EX_TIMEOUT" to decide whether to retry). */
bool epos_print(const buffer_t *xml, buffer_t *response_out);

#endif
