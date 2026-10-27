#include "json.h"
#include "util.h"

#include <stdlib.h>
#include <string.h>

#define JSON_MAX_DEPTH 128

typedef struct {
    const char *cur;
    const char *end;
    int depth;
    int error;
} Parser;

static void skip_ws(Parser *p)
{
    while (p->cur < p->end) {
        char c = *p->cur;
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            p->cur++;
        } else {
            break;
        }
    }
}

static JsonValue *json_new(JsonType type)
{
    JsonValue *value = (JsonValue *)calloc(1, sizeof(*value));
    if (value) {
        value->type = type;
    }
    return value;
}

void json_free(JsonValue *value)
{
    size_t i;

    if (!value) {
        return;
    }

    switch (value->type) {
    case JSON_STRING:
        free(value->u.string);
        break;
    case JSON_ARRAY:
        for (i = 0; i < value->u.array.count; i++) {
            json_free(value->u.array.items[i]);
        }
        free(value->u.array.items);
        break;
    case JSON_OBJECT:
        for (i = 0; i < value->u.object.count; i++) {
            free(value->u.object.keys[i]);
            json_free(value->u.object.values[i]);
        }
        free(value->u.object.keys);
        free(value->u.object.values);
        break;
    default:
        break;
    }
    free(value);
}

static int array_push(JsonValue *array, JsonValue *item)
{
    size_t count = array->u.array.count + 1;
    JsonValue **items = (JsonValue **)realloc(
        array->u.array.items, count * sizeof(*items));
    if (!items) {
        return -1;
    }
    array->u.array.items = items;
    items[count - 1] = item;
    array->u.array.count = count;
    return 0;
}

static int object_push(JsonValue *object, char *key, JsonValue *value)
{
    size_t count = object->u.object.count + 1;
    char **keys = (char **)realloc(
        object->u.object.keys, count * sizeof(*keys));
    JsonValue **values;

    if (!keys) {
        return -1;
    }
    object->u.object.keys = keys;

    values = (JsonValue **)realloc(
        object->u.object.values, count * sizeof(*values));
    if (!values) {
        return -1;
    }
    object->u.object.values = values;

    keys[count - 1] = key;
    values[count - 1] = value;
    object->u.object.count = count;
    return 0;
}

static int parse_hex4(Parser *p, unsigned int *out)
{
    unsigned int value = 0;
    int i;

    if ((size_t)(p->end - p->cur) < 4) {
        p->error = 1;
        return -1;
    }
    for (i = 0; i < 4; i++) {
        char c = p->cur[i];
        value <<= 4;
        if (c >= '0' && c <= '9') {
            value |= (unsigned int)(c - '0');
        } else if (c >= 'a' && c <= 'f') {
            value |= (unsigned int)(c - 'a' + 10);
        } else if (c >= 'A' && c <= 'F') {
            value |= (unsigned int)(c - 'A' + 10);
        } else {
            p->error = 1;
            return -1;
        }
    }
    p->cur += 4;
    *out = value;
    return 0;
}

static int append_utf8(StrBuf *sb, unsigned int cp)
{
    char bytes[4];

    if (cp <= 0x7F) {
        bytes[0] = (char)cp;
        return sb_append(sb, bytes, 1);
    }
    if (cp <= 0x7FF) {
        bytes[0] = (char)(0xC0 | (cp >> 6));
        bytes[1] = (char)(0x80 | (cp & 0x3F));
        return sb_append(sb, bytes, 2);
    }
    if (cp <= 0xFFFF) {
        bytes[0] = (char)(0xE0 | (cp >> 12));
        bytes[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        bytes[2] = (char)(0x80 | (cp & 0x3F));
        return sb_append(sb, bytes, 3);
    }
    if (cp <= 0x10FFFF) {
        bytes[0] = (char)(0xF0 | (cp >> 18));
        bytes[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
        bytes[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
        bytes[3] = (char)(0x80 | (cp & 0x3F));
        return sb_append(sb, bytes, 4);
    }
    return 0;
}

static char *parse_string(Parser *p)
{
    StrBuf sb;

    if (p->cur >= p->end || *p->cur != '"') {
        p->error = 1;
        return NULL;
    }
    p->cur++;
    sb_init(&sb);

    while (p->cur < p->end) {
        unsigned char c = (unsigned char)*p->cur++;

        if (c == '"') {
            if (!sb.data) {
                return str_dup("");
            }
            return sb_steal(&sb);
        }

        if (c == '\\') {
            char esc;
            if (p->cur >= p->end) {
                break;
            }
            esc = *p->cur++;
            switch (esc) {
            case '"': sb_append_char(&sb, '"'); break;
            case '\\': sb_append_char(&sb, '\\'); break;
            case '/': sb_append_char(&sb, '/'); break;
            case 'b': sb_append_char(&sb, '\b'); break;
            case 'f': sb_append_char(&sb, '\f'); break;
            case 'n': sb_append_char(&sb, '\n'); break;
            case 'r': sb_append_char(&sb, '\r'); break;
            case 't': sb_append_char(&sb, '\t'); break;
            case 'u': {
                unsigned int cp;
                if (parse_hex4(p, &cp) != 0) {
                    break;
                }
                if (cp >= 0xD800 && cp <= 0xDBFF) {
                    if ((size_t)(p->end - p->cur) >= 6 &&
                        p->cur[0] == '\\' && p->cur[1] == 'u') {
                        unsigned int low;
                        p->cur += 2;
                        if (parse_hex4(p, &low) != 0) {
                            break;
                        }
                        if (low >= 0xDC00 && low <= 0xDFFF) {
                            cp = 0x10000u + ((cp - 0xD800u) << 10) +
                                 (low - 0xDC00u);
                        } else {
                            cp = 0xFFFDu;
                        }
                    } else {
                        cp = 0xFFFDu;
                    }
                } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                    cp = 0xFFFDu;
                }
                append_utf8(&sb, cp);
                break;
            }
            default:
                p->error = 1;
                break;
            }
            if (p->error) {
                break;
            }
        } else {
            if (c < 0x20) {
                p->error = 1;
                break;
            }
            sb_append_char(&sb, (char)c);
        }
    }

    p->error = 1;
    sb_free(&sb);
    return NULL;
}

static JsonValue *parse_value(Parser *p);

static JsonValue *parse_number(Parser *p)
{
    char *endptr = NULL;
    double number;
    JsonValue *value;

    if (p->cur >= p->end ||
        (*p->cur != '-' && (*p->cur < '0' || *p->cur > '9'))) {
        p->error = 1;
        return NULL;
    }

    number = strtod(p->cur, &endptr);
    if (!endptr || endptr == p->cur || endptr > p->end) {
        p->error = 1;
        return NULL;
    }
    p->cur = endptr;

    value = json_new(JSON_NUMBER);
    if (!value) {
        p->error = 1;
        return NULL;
    }
    value->u.number = number;
    return value;
}

static int match_literal(Parser *p, const char *literal)
{
    size_t len = strlen(literal);

    if ((size_t)(p->end - p->cur) < len ||
        memcmp(p->cur, literal, len) != 0) {
        p->error = 1;
        return 0;
    }
    p->cur += len;
    return 1;
}

static JsonValue *parse_array(Parser *p)
{
    JsonValue *array;

    if (++p->depth > JSON_MAX_DEPTH) {
        p->error = 1;
        p->depth--;
        return NULL;
    }

    p->cur++; /* '[' */
    array = json_new(JSON_ARRAY);
    if (!array) {
        p->error = 1;
        p->depth--;
        return NULL;
    }

    skip_ws(p);
    if (p->cur < p->end && *p->cur == ']') {
        p->cur++;
        p->depth--;
        return array;
    }

    for (;;) {
        JsonValue *item = parse_value(p);
        if (!item) {
            json_free(array);
            p->depth--;
            return NULL;
        }
        if (array_push(array, item) != 0) {
            json_free(item);
            json_free(array);
            p->error = 1;
            p->depth--;
            return NULL;
        }
        skip_ws(p);
        if (p->cur < p->end && *p->cur == ',') {
            p->cur++;
            skip_ws(p);
            continue;
        }
        if (p->cur < p->end && *p->cur == ']') {
            p->cur++;
            p->depth--;
            return array;
        }
        json_free(array);
        p->error = 1;
        p->depth--;
        return NULL;
    }
}

static JsonValue *parse_object(Parser *p)
{
    JsonValue *object;

    if (++p->depth > JSON_MAX_DEPTH) {
        p->error = 1;
        p->depth--;
        return NULL;
    }

    p->cur++; /* '{' */
    object = json_new(JSON_OBJECT);
    if (!object) {
        p->error = 1;
        p->depth--;
        return NULL;
    }

    skip_ws(p);
    if (p->cur < p->end && *p->cur == '}') {
        p->cur++;
        p->depth--;
        return object;
    }

    for (;;) {
        char *key;
        JsonValue *value;

        skip_ws(p);
        key = parse_string(p);
        if (!key) {
            json_free(object);
            p->depth--;
            return NULL;
        }
        skip_ws(p);
        if (p->cur >= p->end || *p->cur != ':') {
            free(key);
            json_free(object);
            p->error = 1;
            p->depth--;
            return NULL;
        }
        p->cur++;
        skip_ws(p);

        value = parse_value(p);
        if (!value) {
            free(key);
            json_free(object);
            p->depth--;
            return NULL;
        }
        if (object_push(object, key, value) != 0) {
            free(key);
            json_free(value);
            json_free(object);
            p->error = 1;
            p->depth--;
            return NULL;
        }

        skip_ws(p);
        if (p->cur < p->end && *p->cur == ',') {
            p->cur++;
            skip_ws(p);
            continue;
        }
        if (p->cur < p->end && *p->cur == '}') {
            p->cur++;
            p->depth--;
            return object;
        }
        json_free(object);
        p->error = 1;
        p->depth--;
        return NULL;
    }
}

static JsonValue *parse_value(Parser *p)
{
    if (p->cur >= p->end) {
        p->error = 1;
        return NULL;
    }

    switch (*p->cur) {
    case '{':
        return parse_object(p);
    case '[':
        return parse_array(p);
    case '"': {
        char *text = parse_string(p);
        JsonValue *value;
        if (!text) {
            return NULL;
        }
        value = json_new(JSON_STRING);
        if (!value) {
            free(text);
            p->error = 1;
            return NULL;
        }
        value->u.string = text;
        return value;
    }
    case 't':
        if (!match_literal(p, "true")) {
            return NULL;
        }
        {
            JsonValue *value = json_new(JSON_BOOL);
            if (!value) {
                p->error = 1;
                return NULL;
            }
            value->u.boolean = 1;
            return value;
        }
    case 'f':
        if (!match_literal(p, "false")) {
            return NULL;
        }
        {
            JsonValue *value = json_new(JSON_BOOL);
            if (!value) {
                p->error = 1;
                return NULL;
            }
            value->u.boolean = 0;
            return value;
        }
    case 'n':
        if (!match_literal(p, "null")) {
            return NULL;
        }
        {
            JsonValue *value = json_new(JSON_NULL);
            if (!value) {
                p->error = 1;
            }
            return value;
        }
    default:
        return parse_number(p);
    }
}

JsonValue *json_parse(const char *text)
{
    Parser p;
    JsonValue *value;

    if (!text) {
        return NULL;
    }

    p.cur = text;
    p.end = text + strlen(text);
    p.depth = 0;
    p.error = 0;

    skip_ws(&p);
    value = parse_value(&p);
    if (!value) {
        return NULL;
    }
    skip_ws(&p);
    if (p.cur != p.end) {
        json_free(value);
        return NULL;
    }
    return value;
}

JsonValue *json_array_get(const JsonValue *array, size_t index)
{
    if (!array || array->type != JSON_ARRAY || index >= array->u.array.count) {
        return NULL;
    }
    return array->u.array.items[index];
}

JsonValue *json_object_get(const JsonValue *object, const char *key)
{
    size_t i;

    if (!object || object->type != JSON_OBJECT || !key) {
        return NULL;
    }
    for (i = 0; i < object->u.object.count; i++) {
        if (strcmp(object->u.object.keys[i], key) == 0) {
            return object->u.object.values[i];
        }
    }
    return NULL;
}

size_t json_array_size(const JsonValue *array)
{
    if (!array || array->type != JSON_ARRAY) {
        return 0;
    }
    return array->u.array.count;
}

const char *json_string(const JsonValue *value)
{
    if (!value || value->type != JSON_STRING) {
        return NULL;
    }
    return value->u.string;
}
