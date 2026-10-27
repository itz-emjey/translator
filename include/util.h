#ifndef TRANSLATOR_UTIL_H
#define TRANSLATOR_UTIL_H

#include <stddef.h>

/* A growable, always NUL-terminated byte buffer. */
typedef struct {
    char *data;
    size_t len;
    size_t cap;
} StrBuf;

void sb_init(StrBuf *sb);
void sb_free(StrBuf *sb);
int sb_reserve(StrBuf *sb, size_t extra);
int sb_append(StrBuf *sb, const char *data, size_t len);
int sb_append_cstr(StrBuf *sb, const char *text);
int sb_append_char(StrBuf *sb, char c);
int sb_appendf(StrBuf *sb, const char *fmt, ...);
char *sb_steal(StrBuf *sb);

char *url_encode(const char *text);
int json_escape_append(StrBuf *sb, const char *text);
char *read_file(const char *path, size_t *out_len);
char *trim_copy(const char *text);
char *str_dup(const char *text);
int str_ci_ncmp(const char *a, const char *b, size_t n);

#endif /* TRANSLATOR_UTIL_H */
