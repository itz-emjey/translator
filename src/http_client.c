#include "http_client.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winhttp.h>
#include <wchar.h>

static wchar_t *utf8_to_wide(const char *text)
{
    int length;
    wchar_t *wide;

    length = MultiByteToWideChar(CP_UTF8, 0, text, -1, NULL, 0);
    if (length <= 0) {
        return NULL;
    }
    wide = (wchar_t *)malloc((size_t)length * sizeof(wchar_t));
    if (!wide) {
        return NULL;
    }
    if (MultiByteToWideChar(CP_UTF8, 0, text, -1, wide, length) <= 0) {
        free(wide);
        return NULL;
    }
    return wide;
}

static void set_winhttp_error(HttpResponse *out, const char *what)
{
    snprintf(out->error, sizeof(out->error), "%s failed (Windows error %lu)",
             what, (unsigned long)GetLastError());
}

int http_get(const char *url, const char *user_agent,
             const char *accept_language, HttpResponse *out)
{
    HINTERNET session = NULL;
    HINTERNET connection = NULL;
    HINTERNET request = NULL;
    wchar_t *wide_url = NULL;
    wchar_t *wide_agent = NULL;
    wchar_t *wide_headers = NULL;
    wchar_t *object_name = NULL;
    StrBuf headers;
    StrBuf body;
    int result = -1;

    memset(out, 0, sizeof(*out));
    sb_init(&headers);
    sb_init(&body);

    wide_url = utf8_to_wide(url);
    wide_agent = utf8_to_wide(user_agent);
    if (!wide_url || !wide_agent) {
        snprintf(out->error, sizeof(out->error), "Out of memory");
        goto cleanup;
    }

    {
        URL_COMPONENTS components;
        DWORD path_length;
        DWORD extra_length;
        DWORD flags;
        wchar_t host[256];

        memset(&components, 0, sizeof(components));
        components.dwStructSize = sizeof(components);
        components.dwSchemeLength = (DWORD)-1;
        components.dwHostNameLength = (DWORD)-1;
        components.dwUrlPathLength = (DWORD)-1;
        components.dwExtraInfoLength = (DWORD)-1;

        if (!WinHttpCrackUrl(wide_url, 0, 0, &components)) {
            set_winhttp_error(out, "WinHttpCrackUrl");
            goto cleanup;
        }

        if (components.dwHostNameLength == 0 ||
            components.dwHostNameLength >=
                (DWORD)(sizeof(host) / sizeof(host[0]))) {
            snprintf(out->error, sizeof(out->error), "Invalid URL host");
            goto cleanup;
        }
        wmemcpy(host, components.lpszHostName, components.dwHostNameLength);
        host[components.dwHostNameLength] = L'\0';

        path_length = components.dwUrlPathLength;
        extra_length = components.dwExtraInfoLength;
        object_name = (wchar_t *)malloc(
            ((size_t)path_length + extra_length + 1) * sizeof(wchar_t));
        if (!object_name) {
            snprintf(out->error, sizeof(out->error), "Out of memory");
            goto cleanup;
        }
        if (path_length) {
            wmemcpy(object_name, components.lpszUrlPath, path_length);
        }
        if (extra_length) {
            wmemcpy(object_name + path_length, components.lpszExtraInfo,
                    extra_length);
        }
        object_name[path_length + extra_length] = L'\0';

        if (sb_appendf(&headers, "Accept-Language: %s\r\n",
                       accept_language ? accept_language : "en-US,en;q=0.9") != 0) {
            snprintf(out->error, sizeof(out->error), "Out of memory");
            goto cleanup;
        }
        wide_headers = utf8_to_wide(headers.data);
        if (!wide_headers) {
            snprintf(out->error, sizeof(out->error), "Out of memory");
            goto cleanup;
        }

        session = WinHttpOpen(wide_agent, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                              WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!session) {
            set_winhttp_error(out, "WinHttpOpen");
            goto cleanup;
        }
        WinHttpSetTimeouts(session, 15000, 15000, 15000, 15000);

        connection = WinHttpConnect(session, host, components.nPort, 0);
        if (!connection) {
            set_winhttp_error(out, "WinHttpConnect");
            goto cleanup;
        }

        flags = (components.nScheme == INTERNET_SCHEME_HTTPS)
                    ? WINHTTP_FLAG_SECURE
                    : 0;
        request = WinHttpOpenRequest(connection, L"GET", object_name, NULL,
                                     WINHTTP_NO_REFERER,
                                     WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
        if (!request) {
            set_winhttp_error(out, "WinHttpOpenRequest");
            goto cleanup;
        }

        {
            DWORD redirect_policy = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
            WinHttpSetOption(request, WINHTTP_OPTION_REDIRECT_POLICY,
                             &redirect_policy, sizeof(redirect_policy));
        }

#ifdef WINHTTP_OPTION_DECOMPRESSION
        {
            DWORD decompression = WINHTTP_DECOMPRESSION_FLAG_ALL;
            WinHttpSetOption(request, WINHTTP_OPTION_DECOMPRESSION,
                             &decompression, sizeof(decompression));
        }
#endif

        if (!WinHttpSendRequest(request, wide_headers, (DWORD)-1L,
                                WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
            set_winhttp_error(out, "WinHttpSendRequest");
            goto cleanup;
        }
        if (!WinHttpReceiveResponse(request, NULL)) {
            set_winhttp_error(out, "WinHttpReceiveResponse");
            goto cleanup;
        }

        {
            DWORD status = 0;
            DWORD status_size = sizeof(status);
            if (!WinHttpQueryHeaders(request,
                                     WINHTTP_QUERY_STATUS_CODE |
                                         WINHTTP_QUERY_FLAG_NUMBER,
                                     WINHTTP_HEADER_NAME_BY_INDEX, &status,
                                     &status_size, WINHTTP_NO_HEADER_INDEX)) {
                set_winhttp_error(out, "WinHttpQueryHeaders");
                goto cleanup;
            }
            out->status = (int)status;
        }

        for (;;) {
            DWORD available = 0;
            DWORD read = 0;
            char *chunk;

            if (!WinHttpQueryDataAvailable(request, &available)) {
                set_winhttp_error(out, "WinHttpQueryDataAvailable");
                goto cleanup;
            }
            if (available == 0) {
                break;
            }
            if (available > 16u * 1024u * 1024u) {
                snprintf(out->error, sizeof(out->error), "Response too large");
                goto cleanup;
            }
            chunk = (char *)malloc(available);
            if (!chunk) {
                snprintf(out->error, sizeof(out->error), "Out of memory");
                goto cleanup;
            }
            if (!WinHttpReadData(request, chunk, available, &read)) {
                free(chunk);
                set_winhttp_error(out, "WinHttpReadData");
                goto cleanup;
            }
            if (read == 0) {
                free(chunk);
                break;
            }
            if (sb_append(&body, chunk, read) != 0) {
                free(chunk);
                snprintf(out->error, sizeof(out->error), "Out of memory");
                goto cleanup;
            }
            free(chunk);
        }
    }

    out->body_len = body.len;
    out->body = sb_steal(&body);
    if (!out->body) {
        snprintf(out->error, sizeof(out->error), "Out of memory");
        goto cleanup;
    }
    result = 0;

cleanup:
    if (request) {
        WinHttpCloseHandle(request);
    }
    if (connection) {
        WinHttpCloseHandle(connection);
    }
    if (session) {
        WinHttpCloseHandle(session);
    }
    free(wide_url);
    free(wide_agent);
    free(wide_headers);
    free(object_name);
    sb_free(&headers);
    sb_free(&body);
    return result;
}

#else /* POSIX: use libcurl */

#include <curl/curl.h>

static size_t write_callback(char *ptr, size_t size, size_t nmemb,
                             void *userdata)
{
    StrBuf *body = (StrBuf *)userdata;
    size_t bytes = size * nmemb;

    if (sb_append(body, ptr, bytes) != 0) {
        return 0;
    }
    return bytes;
}

int http_get(const char *url, const char *user_agent,
             const char *accept_language, HttpResponse *out)
{
    CURL *curl;
    CURLcode code;
    struct curl_slist *headers = NULL;
    StrBuf body;
    char language_header[512];
    long status = 0;
    int result = -1;

    memset(out, 0, sizeof(*out));
    sb_init(&body);

    curl = curl_easy_init();
    if (!curl) {
        snprintf(out->error, sizeof(out->error),
                 "Failed to initialize libcurl");
        return -1;
    }

    snprintf(language_header, sizeof(language_header), "Accept-Language: %s",
             accept_language ? accept_language : "en-US,en;q=0.9");
    headers = curl_slist_append(headers, language_header);
    if (!headers) {
        snprintf(out->error, sizeof(out->error), "Out of memory");
        curl_easy_cleanup(curl);
        return -1;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, user_agent);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");

    code = curl_easy_perform(curl);
    if (code != CURLE_OK) {
        snprintf(out->error, sizeof(out->error), "Request failed: %s",
                 curl_easy_strerror(code));
        goto cleanup;
    }

    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    out->status = (int)status;
    out->body_len = body.len;
    out->body = sb_steal(&body);
    if (!out->body) {
        snprintf(out->error, sizeof(out->error), "Out of memory");
        goto cleanup;
    }
    result = 0;

cleanup:
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    sb_free(&body);
    return result;
}

#endif /* _WIN32 */

void http_response_free(HttpResponse *response)
{
    if (!response) {
        return;
    }
    free(response->body);
    response->body = NULL;
    response->body_len = 0;
}
