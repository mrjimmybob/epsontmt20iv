/******************************************************************************
 * tmbridge
 *
 * buffer.c
 ******************************************************************************/

#include "buffer.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BUFFER_INITIAL_CAPACITY 256

/*****************************************************************************/

static bool buffer_reserve(buffer_t *buffer, size_t required)
{
    char *new_data;
    size_t new_capacity;

    if (required <= buffer->capacity)
        return true;

    new_capacity = buffer->capacity;

    while (new_capacity < required)
        new_capacity *= 2;

    new_data = realloc(buffer->data, new_capacity);

    if (new_data == NULL)
        return false;

    buffer->data = new_data;
    buffer->capacity = new_capacity;

    return true;
}

/*****************************************************************************/

bool buffer_init(buffer_t *buffer)
{
    buffer->data = malloc(BUFFER_INITIAL_CAPACITY);

    if (buffer->data == NULL)
        return false;

    buffer->length = 0;
    buffer->capacity = BUFFER_INITIAL_CAPACITY;

    buffer->data[0] = '\0';

    return true;
}

/*****************************************************************************/

void buffer_free(buffer_t *buffer)
{
    free(buffer->data);

    buffer->data = NULL;
    buffer->length = 0;
    buffer->capacity = 0;
}

/*****************************************************************************/

void buffer_clear(buffer_t *buffer)
{
    if (buffer->data == NULL)
        return;

    buffer->length = 0;
    buffer->data[0] = '\0';

    return;
}

/*****************************************************************************/

bool buffer_append(buffer_t *buffer, const void *data, size_t length)
{
    if (!buffer_reserve(buffer, buffer->length + length + 1))
        return false;

    memcpy(buffer->data + buffer->length, data, length);

    buffer->length += length;

    buffer->data[buffer->length] = '\0';

    return true;
}

/*****************************************************************************/

bool buffer_append_string(buffer_t *buffer, const char *text)
{
    return buffer_append(buffer, text, strlen(text));
}

/*****************************************************************************/

bool buffer_append_char(buffer_t *buffer, char c)
{
    return buffer_append(buffer, &c, 1);
}

/*****************************************************************************/

bool buffer_append_format(buffer_t *buffer, const char *fmt, ...)
{
    va_list args;
    va_list copy;
    int required;

    va_start(args, fmt);

    va_copy(copy, args);

    required = vsnprintf(NULL, 0, fmt, copy);

    va_end(copy);

    if (required < 0)
    {
        va_end(args);
        return false;
    }

    if (!buffer_reserve(buffer, buffer->length + (size_t)required + 1))
    {
        va_end(args);
        return false;
    }

    vsnprintf(buffer->data + buffer->length, buffer->capacity - buffer->length, fmt, args);

    va_end(args);

    buffer->length += (size_t)required;

    return true;
}

/*****************************************************************************/

const char *buffer_data(const buffer_t *buffer)
{
    return buffer->data;
}

/*****************************************************************************/


size_t buffer_length(const buffer_t *buffer)
{
    return buffer->length;
}

