#include "languages.h"

#include <string.h>

const Language LANGUAGES[] = {
    { "auto", "Auto Detect" },
    { "ceb",  "Cebuano" },
    { "en",   "English" },
    { "fr",   "French" },
    { "tl",   "Tagalog" }
};

const size_t LANGUAGE_COUNT = sizeof(LANGUAGES) / sizeof(LANGUAGES[0]);

static int equals_ignore_case(const char *a, const char *b)
{
    while (*a && *b) {
        char ca = *a;
        char cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca = (char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = (char)(cb - 'A' + 'a');
        if (ca != cb) {
            return 0;
        }
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

const char *language_name(const char *code)
{
    size_t i;

    if (!code) {
        return NULL;
    }
    for (i = 0; i < LANGUAGE_COUNT; i++) {
        if (strcmp(LANGUAGES[i].code, code) == 0) {
            return LANGUAGES[i].name;
        }
    }
    return code;
}

const char *language_code(const char *name)
{
    size_t i;

    if (!name) {
        return NULL;
    }
    for (i = 0; i < LANGUAGE_COUNT; i++) {
        if (equals_ignore_case(LANGUAGES[i].name, name)) {
            return LANGUAGES[i].code;
        }
    }
    return NULL;
}

int is_valid_source(const char *code)
{
    size_t i;

    if (!code) {
        return 0;
    }
    for (i = 0; i < LANGUAGE_COUNT; i++) {
        if (strcmp(LANGUAGES[i].code, code) == 0) {
            return 1;
        }
    }
    return 0;
}

int is_valid_target(const char *code)
{
    if (!code || strcmp(code, "auto") == 0) {
        return 0;
    }
    return is_valid_source(code);
}
