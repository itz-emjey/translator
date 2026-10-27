#ifndef TRANSLATOR_HTTP_CLIENT_H
#define TRANSLATOR_HTTP_CLIENT_H

#include <stddef.h>

typedef struct {
    int status;
    char *body;
    size_t body_len;
    char error[256];
} HttpResponse;

/*
 * Performs an HTTPS GET.
 * Returns 0 when the request completed (out->status holds the HTTP status),
 * -1 when the request failed (out->error holds the reason).
 */
int http_get(const char *url, const char *user_agent,
             const char *accept_language, HttpResponse *out);
void http_response_free(HttpResponse *response);

#endif /* TRANSLATOR_HTTP_CLIENT_H */
