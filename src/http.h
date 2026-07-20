#ifndef HTTP_H
#define HTTP_H

#include <stdbool.h>
#include <stddef.h>

#include "buffer.h"

bool http_init(void);

void http_cleanup(void);

bool http_post(const char *url, const char *content_type, const void *body, size_t body_length, buffer_t *response);

#endif
