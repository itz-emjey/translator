#include "util.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void sb_init(StrBuf *sb)
{
    sb->data = NULL;
    sb->len = 0;
    sb->cap = 0;
}

void sb_free(StrBuf *sb)
{
    free(sb->data);
    sb->data = NULL;
    sb->len = 0;
    sb->cap = 0;
}

int sb_reserve(StrBuf *sb, size_t extra)
{
    size_t need;
    size_t cap;
    char *data;

    if (extra > (size_t)-1 - sb->len - 1) {
        return -1;
    }
    need = sb->len + extra + 1;
    if (need <= sb->cap) {
        return 0;
    }

    cap = sb->cap ? sb->cap : 128;
    while (cap < need) {
        if (cap > (size_t)-1 / 2) {
            cap = need;
            break;
        }
        cap *= 2;
    }

    data = (char *)realloc(sb->data, cap);
    if (!data) {
        return -1;
    }
    sb->data = data;
    sb->cap = cap;
    return 0;
}

int sb_append(StrBuf *sb, const char *data, size_t len)
{
    if (len == 0) {
        return 0;
    }
    if (sb_reserve(sb, len) != 0) {
        return -1;
    }
    memcpy(sb->data + sb->len, data, len);
    sb->len += len;
    sb->data[sb->len] = '\0';
    return 0;
}

int sb_append_cstr(StrBuf *sb, const char *text)
{
    if (!text) {
        text = "";
    }
    return sb_append(sb, text, strlen(text));
}

int sb_append_char(StrBuf *sb, char c)
{
    return sb_append(sb, &c, 1);
}

int sb_appendf(StrBuf *sb, const char *fmt, ...)
{
    va_list ap;
    va_list ap_copy;
    int needed;

    va_start(ap, fmt);
    va_copy(ap_copy, ap);
    needed = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (needed < 0) {
        va_end(ap_copy);
        return -1;
    }
    if (sb_reserve(sb, (size_t)needed) != 0) {
        va_end(ap_copy);
        return -1;
    }
    vsnprintf(sb->data + sb->len, (size_t)needed + 1, fmt, ap_copy);
    va_end(ap_copy);
    sb->len += (size_t)needed;
    return 0;
}

char *sb_steal(StrBuf *sb)
{
    char *data = sb->data;

    if (!data) {
        data = (char *)malloc(1);
        if (data) {
            data[0] = '\0';
        }
    }
    sb->data = NULL;
    sb->len = 0;
    sb->cap = 0;
    return data;
}

static int is_unreserved(unsigned char c)
{
    return (c >= 'A' && c <= 'Z') ||
           (c >= 'a' && c <= 'z') ||
           (c >= '0' && c <= '9') ||
           c == '-' || c == '_' || c == '.' || c == '~';
}

char *url_encode(const char *text)
{
    static const char hex[] = "0123456789ABCDEF";
    const unsigned char *p = (const unsigned char *)text;
    size_t len;
    size_t i;
    size_t out_index = 0;
    char *out;

    if (!text) {
        text = "";
    }
    len = strlen(text);
    if (len > ((size_t)-1 - 1) / 3) {
        return NULL;
    }

    out = (char *)malloc(len * 3 + 1);
    if (!out) {
        return NULL;
    }

    for (i = 0; i < len; i++) {
        unsigned char c = p[i];
        if (is_unreserved(c)) {
            out[out_index++] = (char)c;
        } else {
            out[out_index++] = '%';
            out[out_index++] = hex[c >> 4];
            out[out_index++] = hex[c & 0x0F];
        }
    }
    out[out_index] = '\0';
    return out;
}

int json_escape_append(StrBuf *sb, const char *text)
{
    const unsigned char *p = (const unsigned char *)text;

    if (!text) {
        text = "";
        p = (const unsigned char *)text;
    }

    for (; *p; p++) {
        unsigned char c = *p;
        switch (c) {
        case '"':
            if (sb_append_cstr(sb, "\\\"") != 0) return -1;
            break;
        case '\\':
            if (sb_append_cstr(sb, "\\\\") != 0) return -1;
            break;
        case '\b':
            if (sb_append_cstr(sb, "\\b") != 0) return -1;
            break;
        case '\f':
            if (sb_append_cstr(sb, "\\f") != 0) return -1;
            break;
        case '\n':
            if (sb_append_cstr(sb, "\\n") != 0) return -1;
            break;
        case '\r':
            if (sb_append_cstr(sb, "\\r") != 0) return -1;
            break;
        case '\t':
            if (sb_append_cstr(sb, "\\t") != 0) return -1;
            break;
        default:
            if (c < 0x20) {
                if (sb_appendf(sb, "\\u%04x", (unsigned int)c) != 0) return -1;
            } else {
                if (sb_append_char(sb, (char)c) != 0) return -1;
            }
            break;
        }
    }
    return 0;
}

char *read_file(const char *path, size_t *out_len)
{
    FILE *file;
    long size;
    char *buffer;
    size_t read;

    file = fopen(path, "rb");
    if (!file) {
        return NULL;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return NULL;
    }
    size = ftell(file);
    if (size < 0) {
        fclose(file);
        return NULL;
    }
    rewind(file);

    buffer = (char *)malloc((size_t)size + 1);
    if (!buffer) {
        fclose(file);
        return NULL;
    }
    read = fread(buffer, 1, (size_t)size, file);
    fclose(file);
    buffer[read] = '\0';
    if (out_len) {
        *out_len = read;
    }
    return buffer;
}

char *trim_copy(const char *text)
{
    const char *start;
    const char *end;

    if (!text) {
        text = "";
    }
    start = text;
    while (*start == ' ' || *start == '\t' || *start == '\n' ||
           *start == '\r' || *start == '\f' || *start == '\v') {
        start++;
    }
    end = start + strlen(start);
    while (end > start) {
        char c = end[-1];
        if (c != ' ' && c != '\t' && c != '\n' && c != '\r' &&
            c != '\f' && c != '\v') {
            break;
        }
        end--;
    }
    {
        size_t len = (size_t)(end - start);
        char *copy = (char *)malloc(len + 1);
        if (!copy) {
            return NULL;
        }
        memcpy(copy, start, len);
        copy[len] = '\0';
        return copy;
    }
}

char *str_dup(const char *text)
{
    size_t len;
    char *copy;

    if (!text) {
        text = "";
    }
    len = strlen(text);
    copy = (char *)malloc(len + 1);
    if (!copy) {
        return NULL;
    }
    memcpy(copy, text, len + 1);
    return copy;
}

int str_ci_ncmp(const char *a, const char *b, size_t n)
{
    size_t i;

    for (i = 0; i < n; i++) {
        unsigned char ca = (unsigned char)a[i];
        unsigned char cb = (unsigned char)b[i];
        if (ca >= 'A' && ca <= 'Z') ca = (unsigned char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = (unsigned char)(cb - 'A' + 'a');
        if (ca != cb) {
            return (int)ca - (int)cb;
        }
        if (ca == '\0') {
            return 0;
        }
    }
    return 0;
}
