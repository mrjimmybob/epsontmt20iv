#ifndef CONFIG_H
#define CONFIG_H

#include <stdbool.h>
#include <stdint.h>

/* Unlike tmbridge's file-based config, the CUPS backend gets everything it
   needs from the device URI (epos://host[:port]) that CUPS passes in
   $DEVICE_URI. Printer behavior (verify_tls off, timeouts) is fixed per
   FACTS.md, not user-configurable. */

bool        config_init_from_device_uri(const char *uri);

const char *config_get_printer_host(void);
uint16_t    config_get_printer_port(void);

const char *config_get_device(void);

bool        config_get_verify_tls(void);
int         config_get_http_timeout(void);
int         config_get_epos_timeout(void);

bool        config_get_debug_xml(void);

#endif
