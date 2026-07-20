#ifndef BUFFER_H
#define BUFFER_H

#include <stddef.h>
#include <stdbool.h>

typedef struct
{
    char   *data;
    size_t  length;
    size_t  capacity;
} buffer_t;


bool buffer_init(buffer_t *buffer);

void buffer_free(buffer_t *buffer);

void buffer_clear(buffer_t *buffer);

bool buffer_append(buffer_t *buffer, const void *data, size_t length);

bool buffer_append_string(buffer_t *buffer, const char *text);

bool buffer_append_char(buffer_t *buffer, char c);

bool buffer_append_format(buffer_t *buffer, const char *fmt, ...);

const char *buffer_data(const buffer_t *buffer);

size_t buffer_length(const buffer_t *buffer);

#endif
