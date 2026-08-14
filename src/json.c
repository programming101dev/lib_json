#include <errno.h>
#include <limits.h>
#include <p101_json/json.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum
{
    JSON_INITIAL_TOKEN_CAPACITY = 256U,
    JSON_CONTROL_BYTE_LIMIT     = 0x20U,
    JSON_UNICODE_HEX_DIGITS     = 4,
    JSON_HEXADECIMAL_BASE       = 16,
    JSON_DECIMAL_BASE           = 10,
    JSON_SIZE_TEXT_CAPACITY     = (sizeof(size_t) * CHAR_BIT) + 1U
};

static const size_t JSON_NO_PARENT = SIZE_MAX;

static bool json_add_token(struct p101_error *err, struct p101_json *document, enum p101_json_kind kind, size_t start, size_t parent, size_t *token_index);
static bool json_parse(struct p101_error *err, struct p101_json *document);
static bool json_is_space(char character);
static bool json_is_delimiter(char character);

#ifdef P101_JSON_TESTING
static size_t test_write_failure_after = SIZE_MAX;
static size_t test_successful_writes;

void p101_json_test_fail_write_after(size_t successful_writes)
{
    test_write_failure_after = successful_writes;
    test_successful_writes   = 0U;
}

static int test_write_should_fail(void)
{
    int should_fail;

    if(test_successful_writes == test_write_failure_after)
    {
        test_write_failure_after = SIZE_MAX;
        errno                    = EIO;
        should_fail              = 1;
    }
    else
    {
        test_successful_writes++;
        should_fail = 0;
    }
    return should_fail;
}

static int test_fprintf(FILE *stream, const char *format, ...)
{
    int result;
    int should_fail;

    should_fail = test_write_should_fail();
    if(should_fail != 0)
    {
        result = -1;
    }
    else
    {
        va_list arguments;

        va_start(arguments, format);
        result = vfprintf(stream, format, arguments);
        va_end(arguments);
    }
    return result;
}

static int test_fputc(int character, FILE *stream)
{
    int should_fail;
    int result;

    should_fail = test_write_should_fail();
    result      = should_fail != 0 ? EOF : fputc(character, stream);
    return result;
}

static int test_fputs(const char *text, FILE *stream)
{
    int should_fail;
    int result;

    should_fail = test_write_should_fail();
    result      = should_fail != 0 ? EOF : fputs(text, stream);
    return result;
}

    #define fprintf test_fprintf
    #define fputc test_fputc
    #define fputs test_fputs
#endif

void p101_json_init(struct p101_json *document)
{
    document->text           = NULL;
    document->text_size      = 0U;
    document->tokens         = NULL;
    document->token_count    = 0U;
    document->token_capacity = 0U;
}

void p101_json_destroy(struct p101_json *document)
{
    free(document->tokens);
    free(document->text);
    p101_json_init(document);
}

bool p101_json_parse(struct p101_error *err, const char *text, size_t length, struct p101_json *document)
{
    void *allocation;
    bool  parsed;

    if(document == NULL)
    {
        P101_ERROR_RAISE_ERRNO(err, EINVAL);
        parsed = false;
        goto done;
    }
    p101_json_destroy(document);
    if(text == NULL || length == SIZE_MAX)
    {
        P101_ERROR_RAISE_ERRNO(err, EINVAL);
        parsed = false;
        goto done;
    }
    allocation     = malloc(length + 1U);
    document->text = (char *)allocation;
    if(document->text == NULL)
    {
        P101_ERROR_RAISE_ERRNO(err, ENOMEM);
        parsed = false;
        goto done;
    }
    memcpy(document->text, text, length);
    document->text[length] = '\0';
    document->text_size    = length;
    parsed                 = json_parse(err, document);
    if(!parsed)
    {
        p101_json_destroy(document);
    }

done:
    return parsed;
}

bool p101_json_object_get(const struct p101_json *document, size_t object_index, const char *key, size_t *value_index)
{
    size_t index;
    size_t child;
    bool   key_matches;
    bool   found;

    found = false;
    if(object_index >= document->token_count || document->tokens[object_index].kind != P101_JSON_OBJECT)
    {
        goto done;
    }
    child = 0U;
    for(index = object_index + 1U; index < document->token_count && document->tokens[index].start < document->tokens[object_index].end; index++)
    {
        if(document->tokens[index].parent != object_index)
        {
            continue;
        }
        if((child % 2U) == 0U)
        {
            key_matches = p101_json_token_equals(document, index, key);
            if(key_matches && index + 1U < document->token_count && document->tokens[index + 1U].parent == object_index)
            {
                *value_index = index + 1U;
                found        = true;
                break;
            }
        }
        child++;
    }

done:
    return found;
}

bool p101_json_token_equals(const struct p101_json *document, size_t token_index, const char *value)
{
    size_t token_size;
    size_t value_size;
    int    comparison;
    bool   equal;

    equal = false;
    if(token_index >= document->token_count)
    {
        goto done;
    }
    token_size = document->tokens[token_index].end - document->tokens[token_index].start;
    value_size = strlen(value);
    if(token_size == value_size)
    {
        comparison = strncmp(document->text + document->tokens[token_index].start, value, token_size);
        equal      = comparison == 0;
    }

done:
    return equal;
}

bool p101_json_token_copy(struct p101_error *err, const struct p101_json *document, size_t token_index, char *output, size_t output_size)
{
    size_t length;
    bool   copied;

    copied = false;
    if(token_index >= document->token_count || document->tokens[token_index].kind != P101_JSON_STRING)
    {
        goto done;
    }
    length = document->tokens[token_index].end - document->tokens[token_index].start;
    if(length >= output_size)
    {
        P101_ERROR_RAISE_ERRNO(err, EOVERFLOW);
        goto done;
    }
    memcpy(output, document->text + document->tokens[token_index].start, length);
    output[length] = '\0';
    copied         = true;

done:
    return copied;
}

bool p101_json_token_size(const struct p101_json *document, size_t token_index, size_t *value)
{
    char text[JSON_SIZE_TEXT_CAPACITY];
    bool copied;
    bool valid;

    copied = false;
    if(token_index < document->token_count && document->tokens[token_index].kind == P101_JSON_PRIMITIVE)
    {
        size_t length;

        length = document->tokens[token_index].end - document->tokens[token_index].start;
        if(length < sizeof(text))
        {
            memcpy(text, document->text + document->tokens[token_index].start, length);
            text[length] = '\0';
            copied       = true;
        }
    }
    valid = false;
    if(copied)
    {
        char              *end;
        unsigned long long parsed;

        errno  = 0;
        parsed = strtoull(text, &end, JSON_DECIMAL_BASE);
        valid  = errno == 0 && end != text && *end == '\0' && parsed <= SIZE_MAX;
        if(valid)
        {
            *value = (size_t)parsed;
        }
    }
    return valid;
}

bool p101_json_array_get(const struct p101_json *document, size_t array_index, size_t element_index, size_t *value_index)
{
    size_t index;
    size_t child;
    bool   found;

    found = false;
    if(array_index >= document->token_count || document->tokens[array_index].kind != P101_JSON_ARRAY)
    {
        goto done;
    }
    child = 0U;
    for(index = array_index + 1U; index < document->token_count && document->tokens[index].start < document->tokens[array_index].end; index++)
    {
        if(document->tokens[index].parent == array_index)
        {
            if(child == element_index)
            {
                *value_index = index;
                found        = true;
                break;
            }
            child++;
        }
    }

done:
    return found;
}

int p101_json_write_string(FILE *stream, const char *text)
{
    int write_status;
    int result;

    if(stream == NULL || text == NULL)
    {
        errno  = EINVAL;
        result = -1;
        goto done;
    }
    write_status = fputc('"', stream);
    if(write_status != EOF)
    {
        write_status = p101_json_write_string_contents(stream, text);
    }
    if(write_status != 0)
    {
        result = -1;
        goto done;
    }
    write_status = fputc('"', stream);
    result       = write_status == EOF ? -1 : 0;

done:
    return result;
}

int p101_json_write_string_contents(FILE *stream, const char *text)
{
    const unsigned char *cursor;
    int                  write_status;
    int                  result;

    if(stream == NULL || text == NULL)
    {
        errno  = EINVAL;
        result = -1;
        goto done;
    }
    cursor = (const unsigned char *)text;
    while(*cursor != '\0')
    {
        switch(*cursor)
        {
            case '"':
                write_status = fputs("\\\"", stream);
                break;
            case '\\':
                write_status = fputs("\\\\", stream);
                break;
            case '\b':
                write_status = fputs("\\b", stream);
                break;
            case '\f':
                write_status = fputs("\\f", stream);
                break;
            case '\n':
                write_status = fputs("\\n", stream);
                break;
            case '\r':
                write_status = fputs("\\r", stream);
                break;
            case '\t':
                write_status = fputs("\\t", stream);
                break;
            default:
                if(*cursor < JSON_CONTROL_BYTE_LIMIT)
                {
                    write_status = fprintf(stream, "\\u%04x", (unsigned int)*cursor);
                }
                else
                {
                    write_status = fputc((int)*cursor, stream);
                }
                break;
        }
        if(write_status < 0 || write_status == EOF)
        {
            result = -1;
            goto done;
        }
        cursor++;
    }
    result = 0;

done:
    return result;
}

int p101_json_read_string(const char **cursor, char *output, size_t output_size)
{
    size_t unicode_remaining;
    size_t used;
    int    result;

    result = -1;
    if(cursor == NULL || *cursor == NULL || (output != NULL && output_size == 0U))
    {
        goto done;
    }
    used = 0U;
    if(**cursor != '"')
    {
        goto done;
    }
    (*cursor)++;
    while(**cursor != '\0' && **cursor != '"')
    {
        unsigned char value;
        int           escaped;

        value   = (unsigned char)*(*cursor)++;
        escaped = 0;
        if(value == '\\')
        {
            escaped = 1;
            value   = (unsigned char)*(*cursor)++;
            if(value == '"' || value == '\\' || value == '/')
            {
                /* The decoded byte is already in value. */
            }
            else if(value == 'b')
            {
                value = '\b';
            }
            else if(value == 'f')
            {
                value = '\f';
            }
            else if(value == 'n')
            {
                value = '\n';
            }
            else if(value == 'r')
            {
                value = '\r';
            }
            else if(value == 't')
            {
                value = '\t';
            }
            else if(value == 'u')
            {
                unsigned char high;
                unsigned char low;

                unicode_remaining = strlen(*cursor);
                if(unicode_remaining < JSON_UNICODE_HEX_DIGITS || (*cursor)[0] != '0' || (*cursor)[1] != '0')
                {
                    goto done;
                }
                high = (unsigned char)(*cursor)[2];
                low  = (unsigned char)(*cursor)[3];
                if(high >= '0' && high <= '9')
                {
                    high = (unsigned char)(high - '0');
                }
                else if(high >= 'a' && high <= 'f')
                {
                    high = (unsigned char)(high - 'a' + JSON_DECIMAL_BASE);
                }
                else if(high >= 'A' && high <= 'F')
                {
                    high = (unsigned char)(high - 'A' + JSON_DECIMAL_BASE);
                }
                else
                {
                    goto done;
                }
                if(low >= '0' && low <= '9')
                {
                    low = (unsigned char)(low - '0');
                }
                else if(low >= 'a' && low <= 'f')
                {
                    low = (unsigned char)(low - 'a' + JSON_DECIMAL_BASE);
                }
                else if(low >= 'A' && low <= 'F')
                {
                    low = (unsigned char)(low - 'A' + JSON_DECIMAL_BASE);
                }
                else
                {
                    goto done;
                }
                value = (unsigned char)((high * JSON_HEXADECIMAL_BASE) + low);
                *cursor += JSON_UNICODE_HEX_DIGITS;
            }
            else
            {
                goto done;
            }
        }
        if(value < JSON_CONTROL_BYTE_LIMIT && escaped == 0)
        {
            goto done;
        }
        if(output != NULL)
        {
            if(used + 1U >= output_size)
            {
                goto done;
            }
            output[used++] = (char)value;
        }
    }
    if(**cursor != '"')
    {
        goto done;
    }
    (*cursor)++;
    if(output != NULL)
    {
        output[used] = '\0';
    }
    result = 0;

done:
    return result;
}

static bool json_add_token(struct p101_error *err, struct p101_json *document, enum p101_json_kind kind, size_t start, size_t parent, size_t *token_index)
{
    struct p101_json_token *tokens;
    void                   *allocation;
    size_t                  capacity;
    bool                    added;

    added = false;
    if(document->token_count == document->token_capacity)
    {
        capacity = document->token_capacity == 0U ? JSON_INITIAL_TOKEN_CAPACITY : document->token_capacity * 2U;
        if(capacity < document->token_capacity || capacity > SIZE_MAX / sizeof(*document->tokens))
        {
            P101_ERROR_RAISE_ERRNO(err, EOVERFLOW);
            goto done;
        }
        allocation = realloc(document->tokens, capacity * sizeof(*document->tokens));
        tokens     = (struct p101_json_token *)allocation;
        if(tokens == NULL)
        {
            P101_ERROR_RAISE_ERRNO(err, ENOMEM);
            goto done;
        }
        document->tokens         = tokens;
        document->token_capacity = capacity;
    }
    *token_index                               = document->token_count;
    document->tokens[*token_index].kind        = kind;
    document->tokens[*token_index].start       = start;
    document->tokens[*token_index].end         = start;
    document->tokens[*token_index].parent      = parent;
    document->tokens[*token_index].child_count = 0U;
    document->token_count++;
    if(parent != JSON_NO_PARENT)
    {
        document->tokens[parent].child_count++;
    }
    added = true;

done:
    return added;
}

static bool json_parse(struct p101_error *err, struct p101_json *document)
{
    size_t index;
    size_t parent;
    size_t token_index;
    size_t cursor;
    bool   escaped;
    bool   added;
    bool   delimiter;
    bool   error_clear;
    bool   parsed;

    parent = JSON_NO_PARENT;
    parsed = true;
    cursor = 0U;
    while(cursor < document->text_size && parsed)
    {
        char character;
        bool space;

        character = document->text[cursor];
        space     = json_is_space(character);
        if(space)
        {
            cursor++;
            continue;
        }
        if(character == '{' || character == '[')
        {
            added = json_add_token(err, document, character == '{' ? P101_JSON_OBJECT : P101_JSON_ARRAY, cursor, parent, &token_index);
            if(!added)
            {
                parsed = false;
            }
            else
            {
                parent = token_index;
                cursor++;
            }
            continue;
        }
        if(character == '}' || character == ']')
        {
            if(parent == JSON_NO_PARENT)
            {
                parsed = false;
                break;
            }
            if((character == '}' && document->tokens[parent].kind != P101_JSON_OBJECT) || (character == ']' && document->tokens[parent].kind != P101_JSON_ARRAY))
            {
                parsed = false;
                break;
            }
            document->tokens[parent].end = cursor + 1U;
            parent                       = document->tokens[parent].parent;
            cursor++;
            continue;
        }
        if(character == '"')
        {
            cursor++;
            added = json_add_token(err, document, P101_JSON_STRING, cursor, parent, &token_index);
            if(!added)
            {
                parsed = false;
                continue;
            }
            escaped = false;
            while(cursor < document->text_size)
            {
                character = document->text[cursor];
                if(!escaped && character == '"')
                {
                    break;
                }
                if(!escaped && character == '\\')
                {
                    escaped = true;
                }
                else
                {
                    escaped = false;
                }
                cursor++;
            }
            if(cursor >= document->text_size)
            {
                parsed = false;
            }
            else
            {
                document->tokens[token_index].end = cursor;
                cursor++;
            }
            continue;
        }
        if(character == ':' || character == ',')
        {
            cursor++;
            continue;
        }
        index     = cursor;
        delimiter = false;
        while(cursor < document->text_size && !delimiter)
        {
            cursor++;
            if(cursor < document->text_size)
            {
                delimiter = json_is_delimiter(document->text[cursor]);
            }
        }
        if(index == cursor)
        {
            parsed = false;
            continue;
        }
        added = json_add_token(err, document, P101_JSON_PRIMITIVE, index, parent, &token_index);
        if(!added)
        {
            parsed = false;
        }
        else
        {
            document->tokens[token_index].end = cursor;
        }
    }
    if(parsed && (parent != JSON_NO_PARENT || document->token_count == 0U))
    {
        parsed = false;
    }
    error_clear = p101_error_has_no_error(err);
    if(!parsed && error_clear)
    {
        P101_ERROR_RAISE_USER(err, "invalid JSON contract", EINVAL);
    }
    return parsed;
}

static bool json_is_space(char character)
{
    bool is_space;

    is_space = (character == ' ' || character == '\t' || character == '\n' || character == '\r') != 0;
    return is_space;
}

static bool json_is_delimiter(char character)
{
    bool is_delimiter;

    is_delimiter = json_is_space(character);
    if(!is_delimiter)
    {
        is_delimiter = (character == ',' || character == ']' || character == '}') != 0;
    }
    return is_delimiter;
}
