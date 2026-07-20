/******************************************************************************
 * tmbridge

 *
 * http.c
 ******************************************************************************/

#include "http.h"

#include "config.h"
#include "log.h"

#include <curl/curl.h>
#include <stdlib.h>
#include <string.h>

/*****************************************************************************/

static size_t write_callback(void *contents, size_t size, size_t nmemb, void *userp)
{
    buffer_t *response;
    size_t bytes;

    response = (buffer_t *)userp;
    bytes = size * nmemb;

    if (!buffer_append(response, contents, bytes))
        return 0;

    return bytes;
}

/*****************************************************************************/

bool http_init(void)

{
    CURLcode rc;

    rc = curl_global_init(CURL_GLOBAL_DEFAULT);
    if (rc != CURLE_OK)
    {
        log_error("curl_global_init(): %s", curl_easy_strerror(rc));
        return false;
    }

    return true;
}

/*****************************************************************************/

void http_cleanup(void)
{
    curl_global_cleanup();
}

/*****************************************************************************/

bool http_post(const char *url, const char *content_type, const void *body, size_t body_length, buffer_t *response)
{
    CURL *curl;
    CURLcode rc;
    struct curl_slist *headers;

    char header[128];
    long status;

    if (response == NULL)
    {
        log_error("http_post(): response buffer is NULL");
        return false;
    }

    buffer_clear(response);

    curl = curl_easy_init();

    if (curl == NULL)
    {
        log_error("curl_easy_init() failed");
        return false;
    }

    headers = NULL;

    snprintf(header, sizeof(header), "Content-Type: %s", content_type);

    headers = curl_slist_append(headers, header);
    headers = curl_slist_append(headers, "SOAPAction: \"\"");

    if (headers == NULL)
    {
        log_error("Unable to allocate HTTP headers");
        curl_easy_cleanup(curl);
        return false;
    }

    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (curl_off_t)body_length);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, config_get_http_timeout());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, response);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "tmbridge/0.1");

    if (!config_get_verify_tls())
    {
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    }

    rc = curl_easy_perform(curl);
    if (rc != CURLE_OK)
    {
        log_error("HTTP POST failed: %s", curl_easy_strerror(rc));
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        return false;
    }

    rc = curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    if (rc != CURLE_OK)
    {
        log_error("Unable to obtain HTTP response code: %s", curl_easy_strerror(rc));
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        return false;
    }

    if (status != 200)
    {
        log_error("HTTP request failed with status %ld", status);
        if (buffer_length(response) > 0)
            log_debug("Response: %s", buffer_data(response));
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        return false;
    }

    log_debug("HTTP POST successful (%ld)", status);

    if (buffer_length(response) > 0)
        log_debug("Response: %s", buffer_data(response));

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return true;
}

