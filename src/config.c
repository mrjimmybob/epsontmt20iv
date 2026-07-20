/******************************************************************************
 * epsontm20iv
 *
 * config.c - derives printer connection settings from the CUPS DEVICE_URI
 *            (epos://host[:port]) instead of tmbridge's config file.
 ******************************************************************************/

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    char     printer_host[128];
    uint16_t printer_port;
    char     device[64];
    bool     verify_tls;
    int      http_timeout;
    int      epos_timeout;
    bool     debug_xml;
} config_t;

static config_t g_config;

/*****************************************************************************/

bool config_init_from_device_uri(const char *uri)
{
    const char *authority;
    const char *end;
    const char *colon;
    size_t authority_len;
    size_t host_len;

    if (uri == NULL || strncmp(uri, "epos://", 7) != 0)
        return false;

    authority = uri + 7;

    end = strchr(authority, '/');
    authority_len = (end != NULL) ? (size_t)(end - authority) : strlen(authority);

    if (authority_len == 0)
        return false;

    colon = memchr(authority, ':', authority_len);

    if (colon != NULL)
    {
        host_len = (size_t)(colon - authority);
        g_config.printer_port = (uint16_t)strtoul(colon + 1, NULL, 10);

        if (g_config.printer_port == 0)
            g_config.printer_port = 443;
    }
    else
    {
        host_len = authority_len;
        g_config.printer_port = 443;
    }

    if (host_len == 0 || host_len >= sizeof(g_config.printer_host))
        return false;

    memcpy(g_config.printer_host, authority, host_len);
    g_config.printer_host[host_len] = '\0';

    snprintf(g_config.device, sizeof(g_config.device), "%s", "local_printer");

    g_config.verify_tls  = false; /* printer cert SAN doesn't match its real IP - see FACTS.md */
    g_config.http_timeout = 20;   /* curl-level ceiling, above the printer's own 10s timeout */
    g_config.epos_timeout = 10000;
    g_config.debug_xml    = (getenv("EPOS_DEBUG_XML") != NULL);

    return true;
}

/*****************************************************************************/

const char *config_get_printer_host(void)
{
    return g_config.printer_host;
}

uint16_t config_get_printer_port(void)
{
    return g_config.printer_port;
}

const char *config_get_device(void)
{
    return g_config.device;
}

bool config_get_verify_tls(void)
{
    return g_config.verify_tls;
}

int config_get_http_timeout(void)
{
    return g_config.http_timeout;
}

int config_get_epos_timeout(void)
{
    return g_config.epos_timeout;
}

bool config_get_debug_xml(void)
{
    return g_config.debug_xml;
}
