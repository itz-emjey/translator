#ifndef TRANSLATOR_JSON_H
#define TRANSLATOR_JSON_H

#include <stddef.h>

typedef enum {
    JSON_NULL,
    JSON_BOOL,
    JSON_NUMBER,
    JSON_STRING,
    JSON_ARRAY,
    JSON_OBJECT
} JsonType;

typedef struct JsonValue JsonValue;

struct JsonValue {
    JsonType type;
    union {
        int boolean;
        double number;
        char *string;
        struct {
            JsonValue **items;
            size_t count;
        } array;
        struct {
            char **keys;
            JsonValue **values;
            size_t count;
        } object;
    } u;
};

/* Parses a NUL-terminated JSON document. Returns NULL on invalid input. */
JsonValue *json_parse(const char *text);
void json_free(JsonValue *value);

JsonValue *json_array_get(const JsonValue *array, size_t index);
JsonValue *json_object_get(const JsonValue *object, const char *key);
size_t json_array_size(const JsonValue *array);
const char *json_string(const JsonValue *value);

#endif /* TRANSLATOR_JSON_H */
