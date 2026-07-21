/******************************************************************************
 * epsontm20iv
 *
 * epos_backend.c - CUPS backend "epos": reads the ePOS body produced by
 * rastertotmt20iv (on stdin or a job file), POSTs it to the TM-T20IV over
 * HTTPS, and maps the printer's reply to a CUPS backend exit code. See
 * FACTS.md for the endpoint/response contract.
 ******************************************************************************/

#include "buffer.h"
#include "config.h"
#include "epos.h"
#include "http.h"
#include "status.h"

#include <cups/backend.h>

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/*****************************************************************************/

static bool read_all(int fd, buffer_t *out)
{
    char chunk[8192];
    ssize_t n;

    while ((n = read(fd, chunk, sizeof(chunk))) > 0)
    {
        if (!buffer_append(out, chunk, (size_t)n))
            return false;
    }

    return n == 0;
}

/*****************************************************************************/

int main(int argc, char *argv[])
{
    const char *device_uri;
    buffer_t job;
    buffer_t response;
    int fd;
    int result;

    if (argc == 1)
    {
        puts("network epos \"Unknown\" \"Epson TM-T20IV ePOS\"");
        return CUPS_BACKEND_OK;
    }

    if (argc < 6 || argc > 7)
    {
        fprintf(stderr, "ERROR: Usage: epos job-id user title copies options [file]\n");
        return CUPS_BACKEND_FAILED;
    }

    device_uri = getenv("DEVICE_URI");

    if (device_uri == NULL || !config_init_from_device_uri(device_uri))
    {
        fprintf(stderr, "ERROR: epos: invalid or missing DEVICE_URI ('%s')\n",
                device_uri != NULL ? device_uri : "(null)");
        return CUPS_BACKEND_FAILED;
    }

    if (!buffer_init(&job))
    {
        fprintf(stderr, "ERROR: epos: out of memory\n");
        return CUPS_BACKEND_FAILED;
    }

    fd = (argc == 7) ? open(argv[6], O_RDONLY) : 0;

    if (fd < 0)
    {
        fprintf(stderr, "ERROR: epos: unable to open '%s': %s\n", argv[6], strerror(errno));
        buffer_free(&job);
        return CUPS_BACKEND_FAILED;
    }

    if (!read_all(fd, &job))
    {
        fprintf(stderr, "ERROR: epos: failed reading job data\n");
        if (fd != 0)
            close(fd);
        buffer_free(&job);
        return CUPS_BACKEND_FAILED;
    }

    if (fd != 0)
        close(fd);

    if (!http_init())
    {
        fprintf(stderr, "ERROR: epos: unable to initialize HTTP client\n");
        buffer_free(&job);
        return CUPS_BACKEND_FAILED;
    }

    if (!buffer_init(&response))
    {
        fprintf(stderr, "ERROR: epos: out of memory\n");
        http_cleanup();
        buffer_free(&job);
        return CUPS_BACKEND_FAILED;
    }

    {
        bool printed = epos_print(&job, &response);
        epos_state_class_t state = EPOS_STATE_OK;
        uint32_t status;

        /* Always translate the printer's status bits into CUPS state reasons,
           whether or not the job printed: a successful job can still warn
           (paper low), and a failure usually carries the cause (paper end /
           cover open). This also clears stale reasons once healthy again. */
        if (epos_status_parse(buffer_data(&response), &status))
            state = epos_status_report(status, stderr);

        if (printed)
        {
            fprintf(stderr, "INFO: epos: printed successfully\n");
            result = CUPS_BACKEND_OK;
        }
        else if (strstr(buffer_data(&response), "EX_TIMEOUT") != NULL)
        {
            fprintf(stderr, "ERROR: epos: printer busy (EX_TIMEOUT), will retry: %s\n",
                    buffer_data(&response));
            result = CUPS_BACKEND_RETRY;
        }
        else if (state == EPOS_STATE_BLOCKED)
        {
            /* Physical condition the operator must clear (paper out / cover
               open / mechanism). Stop the queue so the ticket isn't lost and
               reprints once fixed, rather than discarding it. */
            fprintf(stderr, "ERROR: epos: printer needs attention "
                            "(load paper / close cover / clear mechanism)\n");
            result = CUPS_BACKEND_RETRY;
        }
        else
        {
            fprintf(stderr, "ERROR: epos: print failed: %s\n",
                    buffer_length(&response) > 0 ? buffer_data(&response) : "(no response / transport error)");
            result = CUPS_BACKEND_FAILED;
        }
    }

    buffer_free(&response);
    buffer_free(&job);
    http_cleanup();

    return result;
}
