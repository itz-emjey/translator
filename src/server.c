#include "server.h"

#include "json.h"
#include "languages.h"
#include "translator.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
typedef SOCKET socket_t;
typedef int socklen_type;
#define INVALID_SOCK INVALID_SOCKET
#define close_socket closesocket
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int socket_t;
typedef socklen_t socklen_type;
#define INVALID_SOCK (-1)
#define close_socket close
#endif

#define MAX_REQUEST_BYTES (1024u * 1024u)
#define MAX_BODY_BYTES (256u * 1024u)

static void report_socket_error(const char *what)
{
#ifdef _WIN32
    fprintf(stderr, "%s failed (Winsock error %d)\n", what, WSAGetLastError());
#else
    perror(what);
#endif
}

static int socket_read(socket_t socket, char *buffer, int length)
{
    return (int)recv(socket, buffer, length, 0);
}

static int socket_write_all(socket_t socket, const char *data, size_t length)
{
    size_t sent = 0;

    while (sent < length) {
#ifdef _WIN32
        int written = send(socket, data + sent, (int)(length - sent), 0);
#else
        ssize_t written = send(socket, data + sent, length - sent, 0);
#endif
        if (written <= 0) {
            return -1;
        }
        sent += (size_t)written;
    }
    return 0;
}

static void set_client_timeout(socket_t socket)
{
#ifdef _WIN32
    DWORD timeout = 20000;
    setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeout,
               sizeof(timeout));
    setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO, (const char *)&timeout,
               sizeof(timeout));
#else
    struct timeval timeout;
    timeout.tv_sec = 20;
    timeout.tv_usec = 0;
    setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
#endif
}

static void send_response(socket_t client, int status, const char *reason,
                          const char *content_type, const char *body,
                          size_t body_length)
{
    StrBuf head;

    sb_init(&head);
    sb_appendf(&head, "HTTP/1.1 %d %s\r\n", status, reason);
    sb_appendf(&head, "Content-Type: %s\r\n", content_type);
    sb_appendf(&head, "Content-Length: %zu\r\n", body_length);
    sb_append_cstr(&head,
                   "Cache-Control: no-store\r\n"
                   "X-Content-Type-Options: nosniff\r\n"
                   "Connection: close\r\n\r\n");

    if (socket_write_all(client, head.data, head.len) == 0 && body_length > 0) {
        socket_write_all(client, body, body_length);
    }
    sb_free(&head);
}

static void send_json_error(socket_t client, int status, const char *reason,
                            const char *message)
{
    StrBuf body;

    sb_init(&body);
    sb_append_cstr(&body, "{\"error\":\"");
    json_escape_append(&body, message);
    sb_append_cstr(&body, "\"}");

    send_response(client, status, reason, "application/json; charset=utf-8",
                  body.data, body.len);
    sb_free(&body);
}

static size_t find_header_end(const char *data, size_t length)
{
    size_t i;

    for (i = 0; i + 3 < length; i++) {
        if (data[i] == '\r' && data[i + 1] == '\n' &&
            data[i + 2] == '\r' && data[i + 3] == '\n') {
            return i + 4;
        }
    }
    return 0;
}

static int header_value(const char *request, const char *name, char *out,
                        size_t out_length)
{
    size_t name_length = strlen(name);
    const char *line = strstr(request, "\r\n");

    if (!line || out_length == 0) {
        return 0;
    }
    line += 2;

    while (line[0] != '\0' && !(line[0] == '\r' && line[1] == '\n')) {
        const char *end = strstr(line, "\r\n");
        const char *value;
        size_t length;

        if (!end) {
            break;
        }
        if (str_ci_ncmp(line, name, name_length) == 0 &&
            line[name_length] == ':') {
            value = line + name_length + 1;
            while (value < end && (*value == ' ' || *value == '\t')) {
                value++;
            }
            length = (size_t)(end - value);
            if (length >= out_length) {
                length = out_length - 1;
            }
            memcpy(out, value, length);
            out[length] = '\0';
            return 1;
        }
        line = end + 2;
    }
    return 0;
}

static char *load_template(void)
{
    static const char *paths[] = {
        "templates/index.html",
        "../templates/index.html"
    };
    size_t i;

    for (i = 0; i < sizeof(paths) / sizeof(paths[0]); i++) {
        char *data = read_file(paths[i], NULL);
        if (data) {
            return data;
        }
    }
    return NULL;
}

static char *render_template(const char *template_text,
                             const char *source_options,
                             const char *target_options)
{
    static const char source_tag[] = "{{SOURCE_OPTIONS}}";
    static const char target_tag[] = "{{TARGET_OPTIONS}}";
    StrBuf output;
    const char *cursor = template_text;
    int failed = 0;

    sb_init(&output);

    for (;;) {
        const char *source = strstr(cursor, source_tag);
        const char *target = strstr(cursor, target_tag);
        const char *next;
        const char *tag;
        const char *replacement;

        if (!source) {
            next = target;
        } else if (!target) {
            next = source;
        } else {
            next = (source < target) ? source : target;
        }

        if (!next) {
            if (sb_append_cstr(&output, cursor) != 0) {
                failed = 1;
            }
            break;
        }

        if (sb_append(&output, cursor, (size_t)(next - cursor)) != 0) {
            failed = 1;
            break;
        }

        if (next == source) {
            tag = source_tag;
            replacement = source_options;
        } else {
            tag = target_tag;
            replacement = target_options;
        }
        if (sb_append_cstr(&output, replacement ? replacement : "") != 0) {
            failed = 1;
            break;
        }
        cursor = next + strlen(tag);
    }

    if (failed) {
        sb_free(&output);
        return NULL;
    }
    return sb_steal(&output);
}

static void serve_index(socket_t client)
{
    char *template_text = load_template();
    StrBuf source_options;
    StrBuf target_options;
    char *html;
    size_t i;

    if (!template_text) {
        send_json_error(client, 500, "Internal Server Error",
                        "templates/index.html was not found");
        return;
    }

    sb_init(&source_options);
    sb_init(&target_options);

    for (i = 0; i < LANGUAGE_COUNT; i++) {
        sb_appendf(&source_options,
                   "                <option value=\"%s\">%s  (%s)</option>\n",
                   LANGUAGES[i].code, LANGUAGES[i].name, LANGUAGES[i].code);
        if (strcmp(LANGUAGES[i].code, "auto") != 0) {
            sb_appendf(&target_options,
                       "                <option value=\"%s\"%s>%s  (%s)</option>\n",
                       LANGUAGES[i].code,
                       strcmp(LANGUAGES[i].code, "en") == 0 ? " selected" : "",
                       LANGUAGES[i].name, LANGUAGES[i].code);
        }
    }

    html = render_template(template_text, source_options.data,
                           target_options.data);
    if (!html) {
        send_json_error(client, 500, "Internal Server Error",
                        "Failed to render template");
    } else {
        send_response(client, 200, "OK", "text/html; charset=utf-8", html,
                      strlen(html));
        free(html);
    }

    sb_free(&source_options);
    sb_free(&target_options);
    free(template_text);
}

static void handle_translate(socket_t client, const char *body,
                             size_t body_length)
{
    char *payload;
    JsonValue *request;
    const char *text;
    const char *source;
    const char *target;
    char *trimmed_text;
    Translation translation;
    char error[512];
    StrBuf response;

    payload = (char *)malloc(body_length + 1);
    if (!payload) {
        send_json_error(client, 500, "Internal Server Error", "Out of memory");
        return;
    }
    memcpy(payload, body, body_length);
    payload[body_length] = '\0';

    request = json_parse(payload);
    if (!request || request->type != JSON_OBJECT) {
        json_free(request);
        free(payload);
        send_json_error(client, 400, "Bad Request", "Invalid JSON body");
        return;
    }

    text = json_string(json_object_get(request, "text"));
    source = json_string(json_object_get(request, "source"));
    target = json_string(json_object_get(request, "target"));

    trimmed_text = text ? trim_copy(text) : NULL;
    if (!trimmed_text || !*trimmed_text) {
        free(trimmed_text);
        json_free(request);
        free(payload);
        send_json_error(client, 400, "Bad Request", "Text is required");
        return;
    }

    if (!source) {
        source = "auto";
    }
    if (!target) {
        target = "en";
    }
    if (!is_valid_source(source)) {
        snprintf(error, sizeof(error), "Unsupported source language: %s",
                 source);
        free(trimmed_text);
        json_free(request);
        free(payload);
        send_json_error(client, 400, "Bad Request", error);
        return;
    }
    if (!is_valid_target(target)) {
        snprintf(error, sizeof(error), "Unsupported target language: %s",
                 target);
        free(trimmed_text);
        json_free(request);
        free(payload);
        send_json_error(client, 400, "Bad Request", error);
        return;
    }

    if (translate_text(trimmed_text, source, target, &translation, error,
                       sizeof(error)) != 0) {
        free(trimmed_text);
        json_free(request);
        free(payload);
        send_json_error(client, 502, "Bad Gateway", error);
        return;
    }

    sb_init(&response);
    sb_append_cstr(&response, "{\"translated\":\"");
    json_escape_append(&response, translation.translated);
    sb_append_cstr(&response, "\",\"detected\":\"");
    json_escape_append(&response, translation.detected);
    sb_append_cstr(&response, "\",\"meaning\":\"");
    if (translation.meaning) {
        json_escape_append(&response, translation.meaning);
    }
    sb_append_cstr(&response, "\"}");

    send_response(client, 200, "OK", "application/json; charset=utf-8",
                  response.data, response.len);

    sb_free(&response);
    translation_free(&translation);
    free(trimmed_text);
    json_free(request);
    free(payload);
}

static void handle_client(socket_t client)
{
    StrBuf request;
    char buffer[8192];
    size_t header_end = 0;
    char method[16];
    char target[2048];
    char path[2048];
    const char *query;
    size_t path_length;
    long content_length = 0;
    char content_length_text[32];
    const char *body;
    size_t body_length;

    sb_init(&request);

    for (;;) {
        int received = socket_read(client, buffer, (int)sizeof(buffer));
        if (received <= 0) {
            sb_free(&request);
            return;
        }
        if (sb_append(&request, buffer, (size_t)received) != 0) {
            send_json_error(client, 500, "Internal Server Error",
                            "Out of memory");
            sb_free(&request);
            return;
        }
        if (request.len > MAX_REQUEST_BYTES) {
            send_json_error(client, 413, "Payload Too Large",
                            "Request is too large");
            sb_free(&request);
            return;
        }
        header_end = find_header_end(request.data, request.len);
        if (header_end) {
            break;
        }
    }

    if (sscanf(request.data, "%15s %2047s", method, target) != 2) {
        send_json_error(client, 400, "Bad Request", "Malformed request");
        sb_free(&request);
        return;
    }

    query = strchr(target, '?');
    path_length = query ? (size_t)(query - target) : strlen(target);
    if (path_length >= sizeof(path)) {
        path_length = sizeof(path) - 1;
    }
    memcpy(path, target, path_length);
    path[path_length] = '\0';

    if (header_value(request.data, "Content-Length", content_length_text,
                     sizeof(content_length_text))) {
        char *end = NULL;
        content_length = strtol(content_length_text, &end, 10);
        if (end == content_length_text || content_length < 0) {
            content_length = 0;
        }
    }

    if (content_length > (long)MAX_BODY_BYTES) {
        send_json_error(client, 413, "Payload Too Large", "Body is too large");
        sb_free(&request);
        return;
    }

    while (request.len < header_end + (size_t)content_length) {
        int received = socket_read(client, buffer, (int)sizeof(buffer));
        if (received <= 0) {
            sb_free(&request);
            return;
        }
        if (sb_append(&request, buffer, (size_t)received) != 0) {
            send_json_error(client, 500, "Internal Server Error",
                            "Out of memory");
            sb_free(&request);
            return;
        }
        if (request.len > MAX_REQUEST_BYTES) {
            send_json_error(client, 413, "Payload Too Large",
                            "Request is too large");
            sb_free(&request);
            return;
        }
    }

    body = request.data + header_end;
    body_length = (size_t)content_length;

    if (strcmp(path, "/api/translate") == 0) {
        if (strcmp(method, "POST") == 0) {
            handle_translate(client, body, body_length);
        } else {
            send_json_error(client, 405, "Method Not Allowed",
                            "Use POST for this endpoint");
        }
    } else if (strcmp(method, "GET") == 0) {
        if (strcmp(path, "/") == 0 || strcmp(path, "/index.html") == 0) {
            serve_index(client);
        } else if (strcmp(path, "/favicon.ico") == 0) {
            send_response(client, 204, "No Content", "text/plain", "", 0);
        } else {
            send_json_error(client, 404, "Not Found", "Not found");
        }
    } else {
        send_json_error(client, 405, "Method Not Allowed",
                        "Method not allowed");
    }

    sb_free(&request);
}

int server_run(const char *host, int port)
{
    socket_t server;
    struct sockaddr_in address;
    int reuse = 1;

#ifdef _WIN32
    {
        WSADATA wsa_data;
        if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
            fprintf(stderr, "WSAStartup failed\n");
            return 1;
        }
    }
#else
    signal(SIGPIPE, SIG_IGN);
#endif

    server = socket(AF_INET, SOCK_STREAM, 0);
    if (server == INVALID_SOCK) {
        report_socket_error("socket");
        return 1;
    }

    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, (const char *)&reuse,
               sizeof(reuse));

    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons((unsigned short)port);
    if (!host || strcmp(host, "0.0.0.0") == 0) {
        address.sin_addr.s_addr = htonl(INADDR_ANY);
    } else if (inet_pton(AF_INET, host, &address.sin_addr) != 1) {
        fprintf(stderr, "Invalid bind address: %s\n", host);
        close_socket(server);
        return 1;
    }

    if (bind(server, (const struct sockaddr *)&address,
             (socklen_type)sizeof(address)) != 0) {
        report_socket_error("bind");
        close_socket(server);
        return 1;
    }
    if (listen(server, 16) != 0) {
        report_socket_error("listen");
        close_socket(server);
        return 1;
    }

    printf("Translator (C) listening on http://%s:%d\n",
           host && *host ? host : "127.0.0.1", port);
    printf("Press Ctrl+C to stop.\n");
    fflush(stdout);

    for (;;) {
        struct sockaddr_in peer;
        socklen_type peer_length = (socklen_type)sizeof(peer);
        socket_t client = accept(server, (struct sockaddr *)&peer,
                                 &peer_length);

        if (client == INVALID_SOCK) {
            continue;
        }
        set_client_timeout(client);
        handle_client(client);
        close_socket(client);
    }

    close_socket(server);
    return 0;
}
