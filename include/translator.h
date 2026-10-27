#ifndef TRANSLATOR_TRANSLATOR_H
#define TRANSLATOR_TRANSLATOR_H

#include <stddef.h>

typedef struct {
    char *translated;
    char *detected;
    char *meaning; /* NULL or empty when Google supplies no dictionary data */
} Translation;

/*
 * Mirrors core/translator.py.
 * Returns 0 on success, -1 on failure (err holds a human readable message).
 */
int translate_text(const char *text, const char *source, const char *target,
                   Translation *out, char *err, size_t err_len);
void translation_free(Translation *translation);

#endif /* TRANSLATOR_TRANSLATOR_H */
