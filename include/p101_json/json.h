#ifndef P101_JSON_JSON_H
#define P101_JSON_JSON_H

#include <p101_error/error.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C"
{
#endif

    enum p101_json_kind
    {
        P101_JSON_OBJECT,
        P101_JSON_ARRAY,
        P101_JSON_STRING,
        P101_JSON_PRIMITIVE
    };

    struct p101_json_token
    {
        enum p101_json_kind kind;
        size_t              start;
        size_t              end;
        size_t              parent;
        size_t              child_count;
    };

    struct p101_json
    {
        char                   *text;
        size_t                  text_size;
        struct p101_json_token *tokens;
        size_t                  token_count;
        size_t                  token_capacity;
    };

    void p101_json_init(struct p101_json *document);
    void p101_json_destroy(struct p101_json *document);
    bool p101_json_parse(struct p101_error *err, const char *text, size_t length, struct p101_json *document);
    bool p101_json_object_get(const struct p101_json *document, size_t object_index, const char *key, size_t *value_index);
    bool p101_json_token_equals(const struct p101_json *document, size_t token_index, const char *value);
    bool p101_json_token_copy(struct p101_error *err, const struct p101_json *document, size_t token_index, char *output, size_t output_size);
    bool p101_json_token_size(const struct p101_json *document, size_t token_index, size_t *value);
    bool p101_json_array_get(const struct p101_json *document, size_t array_index, size_t element_index, size_t *value_index);
    int  p101_json_write_string(FILE *stream, const char *text);
    int  p101_json_write_string_contents(FILE *stream, const char *text);

    /*
     * Decode one JSON string literal starting at *cursor, advancing *cursor past
     * the closing quote on success. Only \u00xx escapes are admitted. Decoded
     * bytes are stored in `output` when it is not NULL, always NUL terminated and
     * never exceeding output_size - 1 bytes; a NULL `output` validates and skips
     * the literal instead. Returns 0 on success and -1 on malformed or oversized
     * input.
     */
    int p101_json_read_string(const char **cursor, char *output, size_t output_size);

#ifdef __cplusplus
}
#endif

#endif
