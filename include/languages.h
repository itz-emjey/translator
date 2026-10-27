#ifndef TRANSLATOR_LANGUAGES_H
#define TRANSLATOR_LANGUAGES_H

#include <stddef.h>

typedef struct {
    const char *code;
    const char *name;
} Language;

/* Same table as core/languages.py. */
extern const Language LANGUAGES[];
extern const size_t LANGUAGE_COUNT;

const char *language_name(const char *code);
const char *language_code(const char *name);
int is_valid_source(const char *code);
int is_valid_target(const char *code);

#endif /* TRANSLATOR_LANGUAGES_H */
