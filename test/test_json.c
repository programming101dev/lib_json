#include <p101_json/json.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int check(bool condition, const char *expression, int line)
{
    int status;

    if(condition)
    {
        status = EXIT_SUCCESS;
    }
    else
    {
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, line, expression);
        status = EXIT_FAILURE;
    }
    return status;
}

#define CHECK(expression)                                                                                                                                                                                                                                          \
    do                                                                                                                                                                                                                                                             \
    {                                                                                                                                                                                                                                                              \
        int check_status_;                                                                                                                                                                                                                                         \
        check_status_ = check((expression), #expression, __LINE__);                                                                                                                                                                                                \
        if(check_status_ != EXIT_SUCCESS)                                                                                                                                                                                                                          \
        {                                                                                                                                                                                                                                                          \
            status = check_status_;                                                                                                                                                                                                                                \
            goto done;                                                                                                                                                                                                                                             \
        }                                                                                                                                                                                                                                                          \
    } while(0)

int main(void)
{
    static const char  json_text[] = "{\"name\":\"p101\",\"count\":3,\"items\":[\"a\",\"b\"],\"enabled\":true}";
    struct p101_json   document;
    struct p101_error *err;
    size_t             token_index;
    size_t             value;
    char               text[16];
    bool               result;
    bool               has_error;
    int                comparison;
    int                status;

    status = EXIT_SUCCESS;
    p101_json_init(&document);
    err = p101_error_create(false);
    CHECK(err != NULL);

    result = p101_json_parse(err, json_text, sizeof(json_text) - 1U, &document);
    CHECK(result);
    CHECK(document.token_count == 11U);

    result = p101_json_object_get(&document, 0U, "name", &token_index);
    CHECK(result);
    result = p101_json_token_equals(&document, token_index, "p101");
    CHECK(result);
    result = p101_json_token_copy(err, &document, token_index, text, sizeof(text));
    CHECK(result);
    comparison = strcmp(text, "p101");
    CHECK(comparison == 0);

    result = p101_json_object_get(&document, 0U, "count", &token_index);
    CHECK(result);
    result = p101_json_token_size(&document, token_index, &value);
    CHECK(result);
    CHECK(value == 3U);

    result = p101_json_object_get(&document, 0U, "items", &token_index);
    CHECK(result);
    result = p101_json_array_get(&document, token_index, 1U, &token_index);
    CHECK(result);
    result = p101_json_token_copy(err, &document, token_index, text, sizeof(text));
    CHECK(result);
    comparison = strcmp(text, "b");
    CHECK(comparison == 0);

    result = p101_json_object_get(&document, 0U, "missing", &token_index);
    CHECK(!result);
    result = p101_json_array_get(&document, 0U, 0U, &token_index);
    CHECK(!result);
    result = p101_json_token_copy(err, &document, 0U, text, sizeof(text));
    CHECK(!result);

    p101_json_destroy(&document);
    result = p101_json_parse(err, "{", 1U, &document);
    CHECK(!result);
    has_error = p101_error_has_error(err);
    CHECK(has_error);

done:
    p101_json_destroy(&document);
    p101_error_destroy(err);
    return status;
}
