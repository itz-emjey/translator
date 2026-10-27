#include "translator.h"

#include "http_client.h"
#include "json.h"
#include "languages.h"
#include "util.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define USER_AGENT \
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 " \
    "(KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36"
#define ACCEPT_LANGUAGE "en-US,en;q=0.9"
#define API_URL "https://translate.googleapis.com/translate_a/single"

static void set_error(char *err, size_t err_len, const char *fmt, ...)
{
    va_list ap;

    if (!err || err_len == 0) {
        return;
    }
    va_start(ap, fmt);
    vsnprintf(err, err_len, fmt, ap);
    va_end(ap);
}

/*
 * Python: data[1] holds
 *   [pos, [glosses], [[translation, [words], null, score], ...], source_word, count]
 */
static char *extract_meaning(const JsonValue *root)
{
    JsonValue *dictionary;
    StrBuf lines;
    size_t i;
    size_t entry_count;

    dictionary = json_array_get(root, 1);
    if (!dictionary || dictionary->type != JSON_ARRAY) {
        return NULL;
    }

    sb_init(&lines);
    entry_count = json_array_size(dictionary);

    for (i = 0; i < entry_count; i++) {
        JsonValue *entry = json_array_get(dictionary, i);
        const char *word;
        const char *part_of_speech;
        JsonValue *sub_entries;
        StrBuf header;
        char *trimmed;
        size_t j;
        size_t sub_count;

        if (!entry || entry->type != JSON_ARRAY ||
            json_array_size(entry) < 3) {
            continue;
        }

        word = json_string(json_array_get(entry, 3));
        part_of_speech = json_string(json_array_get(entry, 0));
        sub_entries = json_array_get(entry, 2);

        sb_init(&header);
        if (word) {
            sb_append_cstr(&header, word);
        }
        if (part_of_speech && *part_of_speech) {
            sb_appendf(&header, "  (%s)", part_of_speech);
        }
        trimmed = trim_copy(header.data);
        sb_free(&header);

        if (!trimmed || !*trimmed) {
            free(trimmed);
            continue;
        }
        sb_appendf(&lines, "\xE2\x80\xA2 %s\n", trimmed);
        free(trimmed);

        sub_count = json_array_size(sub_entries);
        if (sub_count > 4) {
            sub_count = 4;
        }
        for (j = 0; j < sub_count; j++) {
            JsonValue *sub = json_array_get(sub_entries, j);
            const char *sub_text;

            if (!sub || sub->type != JSON_ARRAY) {
                continue;
            }
            sub_text = json_string(json_array_get(sub, 0));
            if (sub_text && *sub_text) {
                sb_appendf(&lines, "    - %s\n", sub_text);
            }
        }
    }

    if (lines.len == 0) {
        sb_free(&lines);
        return NULL;
    }
    while (lines.len > 0 &&
           (lines.data[lines.len - 1] == '\n' ||
            lines.data[lines.len - 1] == '\r')) {
        lines.len--;
    }
    lines.data[lines.len] = '\0';
    return sb_steal(&lines);
}

int translate_text(const char *text, const char *source, const char *target,
                   Translation *out, char *err, size_t err_len)
{
    char *encoded_text = NULL;
    StrBuf url;
    HttpResponse response;
    JsonValue *root = NULL;
    StrBuf translated;
    char *detected = NULL;
    char *meaning = NULL;
    int result = -1;

    if (!out) {
        return -1;
    }
    memset(out, 0, sizeof(*out));
    memset(&response, 0, sizeof(response));
    sb_init(&url);
    sb_init(&translated);

    if (!text || !*text) {
        set_error(err, err_len, "Text is required");
        goto cleanup;
    }
    if (!is_valid_source(source)) {
        set_error(err, err_len, "Unsupported source language: %s",
                  source ? source : "(null)");
        goto cleanup;
    }
    if (!is_valid_target(target)) {
        set_error(err, err_len, "Unsupported target language: %s",
                  target ? target : "(null)");
        goto cleanup;
    }

    encoded_text = url_encode(text);
    if (!encoded_text) {
        set_error(err, err_len, "Out of memory");
        goto cleanup;
    }

    if (sb_appendf(&url,
                   "%s?client=dict-chrome-ex&sl=%s&tl=%s&dt=t&dt=bd&q=%s",
                   API_URL, source, target, encoded_text) != 0) {
        set_error(err, err_len, "Out of memory");
        goto cleanup;
    }

    if (http_get(url.data, USER_AGENT, ACCEPT_LANGUAGE, &response) != 0) {
        set_error(err, err_len, "%s",
                  response.error[0] ? response.error : "Request failed");
        goto cleanup;
    }
    if (response.status != 200) {
        set_error(err, err_len, "Translation service returned HTTP %d",
                  response.status);
        goto cleanup;
    }

    root = json_parse(response.body);
    if (!root || root->type != JSON_ARRAY) {
        set_error(err, err_len, "Invalid response from translation service");
        goto cleanup;
    }

    /* data[0] is a list of [translated_segment, original_segment, ...]. */
    {
        JsonValue *segments = json_array_get(root, 0);
        size_t i;
        size_t segment_count = json_array_size(segments);

        for (i = 0; i < segment_count; i++) {
            JsonValue *segment = json_array_get(segments, i);
            const char *piece;

            if (!segment || segment->type != JSON_ARRAY) {
                continue;
            }
            piece = json_string(json_array_get(segment, 0));
            if (piece) {
                sb_append_cstr(&translated, piece);
            }
        }
    }

    if (translated.len == 0) {
        set_error(err, err_len, "Empty translation");
        goto cleanup;
    }

    {
        const char *value = json_string(json_array_get(root, 2));
        detected = str_dup((value && *value) ? value : source);
        if (!detected) {
            set_error(err, err_len, "Out of memory");
            goto cleanup;
        }
    }

    meaning = extract_meaning(root);

    out->translated = sb_steal(&translated);
    if (!out->translated) {
        set_error(err, err_len, "Out of memory");
        goto cleanup;
    }
    out->detected = detected;
    out->meaning = meaning;
    detected = NULL;
    meaning = NULL;
    result = 0;

cleanup:
    free(encoded_text);
    free(detected);
    free(meaning);
    json_free(root);
    http_response_free(&response);
    sb_free(&url);
    sb_free(&translated);
    return result;
}

void translation_free(Translation *translation)
{
    if (!translation) {
        return;
    }
    free(translation->translated);
    free(translation->detected);
    free(translation->meaning);
    translation->translated = NULL;
    translation->detected = NULL;
    translation->meaning = NULL;
}
