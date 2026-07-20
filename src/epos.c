/******************************************************************************
 * epsontm20iv (adapted from tmbridge)
 *
 * epos.c - wraps an epos-print body in the SOAP envelope, POSTs it, and
 *          checks the reply for success="true".
 ******************************************************************************/

#include "epos.h"

#include "buffer.h"
#include "config.h"
#include "http.h"
#include "log.h"

#include <stdio.h>
#include <string.h>

/*****************************************************************************/

bool epos_print(const buffer_t *xml, buffer_t *response_out)
{
    buffer_t soap;
    buffer_t local_response;
    buffer_t *response;

    char url[512];
    bool ok;
    bool own_response;

    if (!buffer_init(&soap))
        return false;

    own_response = (response_out == NULL);

    if (own_response)
    {
        if (!buffer_init(&local_response))
        {
            buffer_free(&soap);
            return false;
        }

        response = &local_response;
    }
    else
    {
        response = response_out;
        buffer_clear(response);
    }

    snprintf(
        url,
        sizeof(url),
        "https://%s:%d/cgi-bin/epos/service.cgi?devid=%s&timeout=%d",
        config_get_printer_host(),
        config_get_printer_port(),
        config_get_device(),
        config_get_epos_timeout());

    ok = buffer_append_string(&soap, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>")
      && buffer_append_string(&soap, "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\">")
      && buffer_append_string(&soap, "<s:Body>")
      && buffer_append_string(&soap, "<epos-print xmlns=\"http://www.epson-pos.com/schemas/2011/03/epos-print\">")
      && buffer_append(&soap, buffer_data(xml), buffer_length(xml))
      && buffer_append_string(&soap, "</epos-print>")
      && buffer_append_string(&soap, "</s:Body>")
      && buffer_append_string(&soap, "</s:Envelope>");

    if (!ok)
    {
        if (own_response)
            buffer_free(response);

        buffer_free(&soap);
        return false;
    }

    if (config_get_debug_xml())
    {
        log_debug(
            "========== BEGIN SOAP ==========\n"
            "%s\n"
            "=========== END SOAP ===========",
            buffer_data(&soap));
    }

    ok = http_post(url, "text/xml; charset=utf-8", buffer_data(&soap), buffer_length(&soap), response);

    if (ok)
    {
        if (config_get_debug_xml())
        {
            log_debug("Printer response:\n%s", buffer_data(response));
        }

        /* The printer returns HTTP 200 even for failures (EX_TIMEOUT when
           busy, SchemaError on bad XML). Only success="true" in the SOAP body
           means the job actually printed. */
        ok = strstr(buffer_data(response), "success=\"true\"") != NULL;

        if (!ok)
        {
            log_error("Printer rejected job: %s", buffer_data(response));
        }
    }

    if (own_response)
        buffer_free(response);

    buffer_free(&soap);

    return ok;
}
