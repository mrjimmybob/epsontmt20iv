/******************************************************************************
 * tmbridge
 *
 * log.c
 ******************************************************************************/

#define _POSIX_C_SOURCE 200809L

#include "log.h"

#include <stdarg.h>
#include <stdio.h>
#include <time.h>

/*****************************************************************************/

static void log_message(const char *level, const char *fmt, va_list args)
{
    time_t now;
    struct tm tm_now;

    now = time(NULL);

    localtime_r(&now, &tm_now);

    fprintf(stderr,
            "[%04d-%02d-%02d %02d:%02d:%02d] %-5s ",
            tm_now.tm_year + 1900,
            tm_now.tm_mon + 1,
            tm_now.tm_mday,
            tm_now.tm_hour,
            tm_now.tm_min,
            tm_now.tm_sec,
            level);

    vfprintf(stderr, fmt, args);

    fputc('\n', stderr);

    fflush(stderr);
}

/*****************************************************************************/

void log_info(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    log_message("INFO", fmt, args);
    va_end(args);
}

/*****************************************************************************/

void log_error(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    log_message("ERROR", fmt, args);
    va_end(args);
}

/*****************************************************************************/

void log_debug(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    log_message("DEBUG", fmt, args);
    va_end(args);
}

/*****************************************************************************/

void log_warning(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    log_message("WARNING", fmt, args);
    va_end(args);
}

