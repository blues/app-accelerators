/*!
 * @file n_cjson.c
 *
 * Written by Ray Ozzie and Blues Inc. team.
 *
 * Portions Copyright (c) 2019 Blues Inc. MIT License. Use of this source code is
 * governed by licenses granted by the copyright holder including that found in
 * the
 * <a href="https://github.com/blues/note-c/blob/master/LICENSE">LICENSE</a>
 * file.
 *
 * MODIFIED for use in notecard primarily by altering default memory allocator
 * and by renaming the functions so that they won't conflict with a developer's
 * own decision to incorporate the actual production cJSON into their own app.
 * In no way shall this interfere with a production cJSON.
 *
 * Renaming was done as follows:
 * CJSON_ -> N_CJSON_
 * cJSON_ -> J
 * cJSON -> J
 *
 * Portions Copyright (c) 2009-2017 Dave Gamble and cJSON contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software i
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

/* disable warnings about old C89 functions in MSVC */
#if !defined(_CRT_SECURE_NO_DEPRECATE) && defined(_MSC_VER)
#define _CRT_SECURE_NO_DEPRECATE
#endif

#ifdef __GNUC__
#pragma GCC visibility push(default)
#endif
#if defined(_MSC_VER)
#pragma warning (push)
/* disable warning about single line comments in system headers */
#pragma warning (disable : 4001)
#endif

#include <string.h>
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <limits.h>
#include <ctype.h>

// For Note, disable dependencies
#undef ENABLE_LOCALES
#define MINIMIZE_CLIB_DEPENDENCIES      1       // Use tiny but non-robust versions of conversions

#include "n_lib.h"

#define STRINGIFY(x) STRINGIFY_(x)
#define STRINGIFY_(x) #x

#ifdef ENABLE_LOCALES
#include <locale.h>
#endif

#if defined(_MSC_VER)
#pragma warning (pop)
#endif
#ifdef __GNUC__
#pragma GCC visibility pop
#endif

typedef struct {
    const unsigned char *json;
    size_t position;
} error;
static error global_error = { NULL, 0 };

// Forwards
void htoa16(uint16_t n, unsigned char *p);
static J *JNew_Item(void);

N_CJSON_PUBLIC(const char *) JGetErrorPtr(void)
{
    return (const char*) (global_error.json + global_error.position);
}

N_CJSON_PUBLIC(char *) JGetStringValue(J *item)
{
    if (!JIsString(item)) {
        return NULL;
    }

    return item->valuestring;
}

/* This is a safeguard to prevent copy-pasters from using incompatible C and header files */
#if (N_CJSON_VERSION_MAJOR != 1) || (N_CJSON_VERSION_MINOR != 7) || (N_CJSON_VERSION_PATCH != 7)
#error J.h and J.c have different versions. Make sure that both have the same.
#endif

N_CJSON_PUBLIC(const char*) JVersion(void)
{
    return STRINGIFY(N_CJSON_VERSION_MAJOR) "." STRINGIFY(N_CJSON_VERSION_MINOR) "." STRINGIFY(N_CJSON_VERSION_PATCH);
}

/* Case insensitive string comparison, doesn't consider two NULL pointers equal though */
static int case_insensitive_strcmp(const unsigned char *string1, const unsigned char *string2)
{
    if ((string1 == NULL) || (string2 == NULL)) {
        return 1;
    }

    if (string1 == string2) {
        return 0;
    }

    for(; tolower(*string1) == tolower(*string2); (void)string1++, string2++) {
        if (*string1 == '\0') {
            return 0;
        }
    }

    return tolower(*string1) - tolower(*string2);
}

static unsigned char* Jstrdup(const unsigned char* string)
{
    size_t length = 0;
    unsigned char *copy = NULL;

    if (string == NULL) {
        return NULL;
    }

    length = strlen((const char*)string) + sizeof("");
    copy = (unsigned char*)_Malloc(length);
    if (copy == NULL) {
        return NULL;
    }
    memcpy(copy, string, length);

    return copy;
}

N_CJSON_PUBLIC(void *) JMalloc(size_t size)
{
    return _Malloc(size);
}
N_CJSON_PUBLIC(void) JFree(void *p)
{
    _Free(p);
}

/* Internal constructor. */
static J *JNew_Item()
{
    J* node = (J*)_Malloc(sizeof(J));
    if (node) {
        memset(node, '\0', sizeof(J));
    }

    return node;
}

/* Delete a J structure. */
N_CJSON_PUBLIC(void) JDelete(J *item)
{
    J *next = NULL;
    while (item != NULL) {
        next = item->next;
        if (!(item->type & JIsReference) && (item->child != NULL)) {
            JDelete(item->child);
        }
        if (!(item->type & JIsReference) && (item->valuestring != NULL)) {
            _Free(item->valuestring);
        }
        if (!(item->type & JStringIsConst) && (item->string != NULL)) {
            _Free(item->string);
        }
        _Free(item);
        item = next;
    }
}

/* get the decimal point character of the current locale */
static unsigned char get_decimal_point(void)
{
#ifdef ENABLE_LOCALES
    struct lconv *lconv = localeconv();
    return (unsigned char) lconv->decimal_point[0];
#else
    return '.';
#endif
}

typedef struct {
    const unsigned char *content;
    size_t length;
    size_t offset;
    size_t depth; /* How deeply nested (in arrays/objects) is the input at the current offset. */
} parse_buffer;

/* check if the given size is left to read in a given parse buffer (starting with 1) */
#define can_read(buffer, size) ((buffer != NULL) && (((buffer)->offset + size) <= (buffer)->length))
/* check if the buffer can be accessed at the given index (starting with 0) */
#define can_access_at_index(buffer, index) ((buffer != NULL) && (((buffer)->offset + index) < (buffer)->length))
#define cannot_access_at_index(buffer, index) (!can_access_at_index(buffer, index))
/* get a pointer to the buffer at the position */
#define buffer_at_offset(buffer) ((buffer)->content + (buffer)->offset)

/* Parse the input text to generate a number, and populate the result into item. */
static Jbool parse_number(J * const item, parse_buffer * const input_buffer)
{
    JNUMBER number = 0;
    unsigned char *after_end = NULL;
    unsigned char number_c_string[64];
    unsigned char decimal_point = get_decimal_point();
    size_t i = 0;

    if ((input_buffer == NULL) || (input_buffer->content == NULL)) {
        return false;
    }

    /* copy the number into a temporary buffer and replace '.' with the decimal point
     * of the current locale (for strtod)
     * This also takes care of '\0' not necessarily being available for marking the end of the input */
    for (i = 0; (i < (sizeof(number_c_string) - 1)) && can_access_at_index(input_buffer, i); i++) {
        switch (buffer_at_offset(input_buffer)[i]) {
        case '0':
        case '1':
        case '2':
        case '3':
        case '4':
        case '5':
        case '6':
        case '7':
        case '8':
        case '9':
        case '+':
        case '-':
        case 'e':
        case 'E':
            number_c_string[i] = buffer_at_offset(input_buffer)[i];
            break;

        case '.':
            number_c_string[i] = decimal_point;
            break;

        default:
            goto loop_end;
        }
    }
loop_end:
    number_c_string[i] = '\0';

    /* some platforms may not have locale support */
#if !MINIMIZE_CLIB_DEPENDENCIES
    number = strtod((const char*)number_c_string, (char**)&after_end);
#else
    number = JAtoN((const char*)number_c_string, (char**)&after_end);
#endif
    if (number_c_string == after_end) {
        return false; /* parse_error */
    }

    item->valuenumber = number;

    /* use saturation in case of overflow */
    if (number >= LONG_MAX) {
        item->valueint = LONG_MAX;
    } else if (number <= LONG_MIN) {
        item->valueint = LONG_MIN;
    } else {
        item->valueint = (long int)number;
    }

    item->type = JNumber;

    input_buffer->offset += (size_t)(after_end - number_c_string);
    return true;
}

/* don't ask me, but the original JSetNumberValue returns an integer or JNUMBER */
N_CJSON_PUBLIC(JNUMBER) JSetNumberHelper(J *object, JNUMBER number)
{
    if (object == NULL) {
        return number;
    }
    if (number >= LONG_MAX) {
        object->valueint = LONG_MAX;
    } else if (number <= LONG_MIN) {
        object->valueint = LONG_MIN;
    } else {
        object->valueint = (long int)number;
    }

    return object->valuenumber = number;
}

typedef struct {
    unsigned char *buffer;
    size_t length;
    size_t offset;
    size_t depth; /* current nesting depth (for formatted printing) */
    Jbool noalloc;
    Jbool format; /* is this print a formatted print */
} printbuffer;

/* realloc printbuffer if necessary to have at least "needed" bytes more */
static unsigned char* ensure(printbuffer * const p, size_t needed)
{
    unsigned char *newbuffer = NULL;
    size_t newsize = 0;

    if ((p == NULL) || (p->buffer == NULL)) {
        return NULL;
    }

    if ((p->length > 0) && (p->offset >= p->length)) {
        /* make sure that offset is valid */
        return NULL;
    }

    if (needed > INT_MAX) {
        /* sizes bigger than INT_MAX are currently not supported */
        return NULL;
    }

    needed += p->offset + 1;
    if (needed <= p->length) {
        return p->buffer + p->offset;
    }

    if (p->noalloc) {
        return NULL;
    }

    /* calculate new buffer size */
    if (needed > (INT_MAX / 2)) {
        /* overflow of int, use INT_MAX if possible */
        if (needed <= INT_MAX) {
            newsize = INT_MAX;
        } else {
            return NULL;
        }
    } else {
        newsize = needed * 2;
    }

    /* otherwise reallocate manually */
    newbuffer = (unsigned char*)_Malloc(newsize);
    if (!newbuffer) {
        _Free(p->buffer);
        p->length = 0;
        p->buffer = NULL;
        return NULL;
    }
    if (newbuffer) {
        memcpy(newbuffer, p->buffer, p->offset + 1);
    }
    _Free(p->buffer);

    p->length = newsize;
    p->buffer = newbuffer;

    return newbuffer + p->offset;
}

/* calculate the new length of the string in a printbuffer and update the offset */
static void update_offset(printbuffer * const buffer)
{
    const unsigned char *buffer_pointer = NULL;
    if ((buffer == NULL) || (buffer->buffer == NULL)) {
        return;
    }
    buffer_pointer = buffer->buffer + buffer->offset;

    buffer->offset += strlen((const char*)buffer_pointer);
}

/* Render the number nicely from the given item into a string. */
static Jbool print_number(const J * const item, printbuffer * const output_buffer)
{
    if (item == NULL) {
        return false;
    }

    unsigned char *output_pointer = NULL;
    JNUMBER d = item->valuenumber;
    int length = 0;
    size_t i = 0;
    unsigned char number_buffer[JNTOA_MAX]; /* temporary buffer to print the number into */
    unsigned char decimal_point = get_decimal_point();

    if (output_buffer == NULL) {
        return false;
    }

    /* This checks for NaN and Infinity */
    if ((d * 0) != 0) {
        char *nbuf = (char *) number_buffer;
        strcpy(nbuf, "null");
        length = strlen(nbuf);
    } else {
#if !MINIMIZE_CLIB_DEPENDENCIES
        JNUMBER test;
        /* Try 15 decimal places of precision to avoid nonsignificant nonzero digits */
        length = sprintf((char*)number_buffer, "%1.15g", d);

        /* Check whether the original double can be recovered */
        if ((sscanf((char*)number_buffer, "%lg", &test) != 1) || ((JNUMBER)test != d)) {
            /* If not, print with 17 decimal places of precision */
            length = sprintf((char*)number_buffer, "%1.17g", d);
        }
#else
        char *nbuf = (char *) number_buffer;
        JNtoA(d, nbuf, -1);
        length = strlen(nbuf);
#endif
    }

    /* conversion failed or buffer overrun occured */
    if ((length < 0) || (length > (int)(sizeof(number_buffer) - 1))) {
        return false;
    }

    /* reserve appropriate space in the output */
    output_pointer = ensure(output_buffer, (size_t)length + sizeof(""));
    if (output_pointer == NULL) {
        return false;
    }

    /* copy the printed number to the output and replace locale
     * dependent decimal point with '.' */
    for (i = 0; i < ((size_t)length); i++) {
        if (number_buffer[i] == decimal_point) {
            output_pointer[i] = '.';
            continue;
        }

        output_pointer[i] = number_buffer[i];
    }
    output_pointer[i] = '\0';

    output_buffer->offset += (size_t)length;

    return true;
}

/* parse 4 digit hexadecimal number */
static unsigned long parse_hex4(const unsigned char * const input)
{
    unsigned long int h = 0;
    size_t i = 0;

    for (i = 0; i < 4; i++) {
        /* parse digit */
        if ((input[i] >= '0') && (input[i] <= '9')) {
            h += (unsigned int) input[i] - '0';
        } else if ((input[i] >= 'A') && (input[i] <= 'F')) {
            h += (unsigned int) 10 + input[i] - 'A';
        } else if ((input[i] >= 'a') && (input[i] <= 'f')) {
            h += (unsigned int) 10 + input[i] - 'a';
        } else { /* invalid */
            return 0;
        }

        if (i < 3) {
            /* shift left to make place for the next nibble */
            h = h << 4;
        }
    }

    return h;
}

/* converts a UTF-16 literal to UTF-8
 * A literal can be one or two sequences of the form \uXXXX */
static unsigned char utf16_literal_to_utf8(const unsigned char * const input_pointer, const unsigned char * const input_end, unsigned char **output_pointer)
{
    long unsigned int codepoint = 0;
    unsigned long int first_code = 0;
    const unsigned char *first_sequence = input_pointer;
    unsigned char utf8_length = 0;
    unsigned char utf8_position = 0;
    unsigned char sequence_length = 0;
    unsigned char first_byte_mark = 0;

    if ((input_end - first_sequence) < 6) {
        /* input ends unexpectedly */
        goto fail;
    }

    /* get the first utf16 sequence */
    first_code = parse_hex4(first_sequence + 2);

    /* check that the code is valid */
    if (((first_code >= 0xDC00) && (first_code <= 0xDFFF))) {
        goto fail;
    }

    /* UTF16 surrogate pair */
    if ((first_code >= 0xD800) && (first_code <= 0xDBFF)) {
        const unsigned char *second_sequence = first_sequence + 6;
        unsigned int second_code = 0;
        sequence_length = 12; /* \uXXXX\uXXXX */

        if ((input_end - second_sequence) < 6) {
            /* input ends unexpectedly */
            goto fail;
        }

        if ((second_sequence[0] != '\\') || (second_sequence[1] != 'u')) {
            /* missing second half of the surrogate pair */
            goto fail;
        }

        /* get the second utf16 sequence */
        second_code = parse_hex4(second_sequence + 2);
        /* check that the code is valid */
        if ((second_code < 0xDC00) || (second_code > 0xDFFF)) {
            /* invalid second half of the surrogate pair */
            goto fail;
        }


        /* calculate the unicode codepoint from the surrogate pair */
        codepoint = 0x10000 + (((first_code & 0x3FF) << 10) | (second_code & 0x3FF));
    } else {
        sequence_length = 6; /* \uXXXX */
        codepoint = first_code;
    }

    /* encode as UTF-8
     * takes at maximum 4 bytes to encode:
     * 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx */
    if (codepoint < 0x80) {
        /* normal ascii, encoding 0xxxxxxx */
        utf8_length = 1;
    } else if (codepoint < 0x800) {
        /* two bytes, encoding 110xxxxx 10xxxxxx */
        utf8_length = 2;
        first_byte_mark = 0xC0; /* 11000000 */
    } else if (codepoint < 0x10000) {
        /* three bytes, encoding 1110xxxx 10xxxxxx 10xxxxxx */
        utf8_length = 3;
        first_byte_mark = 0xE0; /* 11100000 */
    } else if (codepoint <= 0x10FFFF) {
        /* four bytes, encoding 1110xxxx 10xxxxxx 10xxxxxx 10xxxxxx */
        utf8_length = 4;
        first_byte_mark = 0xF0; /* 11110000 */
    } else {
        /* invalid unicode codepoint */
        goto fail;
    }

    /* encode as utf8 */
    for (utf8_position = (unsigned char)(utf8_length - 1); utf8_position > 0; utf8_position--) {
        /* 10xxxxxx */
        (*output_pointer)[utf8_position] = (unsigned char)((codepoint | 0x80) & 0xBF);
        codepoint >>= 6;
    }
    /* encode first byte */
    if (utf8_length > 1) {
        (*output_pointer)[0] = (unsigned char)((codepoint | first_byte_mark) & 0xFF);
    } else {
        (*output_pointer)[0] = (unsigned char)(codepoint & 0x7F);
    }

    *output_pointer += utf8_length;

    return sequence_length;

fail:
    return 0;
}

/* Parse the input text into an unescaped cinput, and populate item. */
static Jbool parse_string(J * const item, parse_buffer * const input_buffer)
{
    const unsigned char *input_pointer = buffer_at_offset(input_buffer) + 1;
    const unsigned char *input_end = buffer_at_offset(input_buffer) + 1;
    unsigned char *output_pointer = NULL;
    unsigned char *output = NULL;

    /* not a string */
    if (buffer_at_offset(input_buffer)[0] != '\"') {
        goto fail;
    }

    {
        /* calculate approximate size of the output (overestimate) */
        size_t allocation_length = 0;
        size_t skipped_bytes = 0;
        while (((size_t)(input_end - input_buffer->content) < input_buffer->length) && (*input_end != '\"')) {
            /* is escape sequence */
            if (input_end[0] == '\\') {
                if ((size_t)(input_end + 1 - input_buffer->content) >= input_buffer->length) {
                    /* prevent buffer overflow when last input character is a backslash */
                    goto fail;
                }
                skipped_bytes++;
                input_end++;
            }
            input_end++;
        }
        if (((size_t)(input_end - input_buffer->content) >= input_buffer->length) || (*input_end != '\"')) {
            goto fail; /* string ended unexpectedly */
        }

        /* This is at most how much we need for the output */
        allocation_length = (size_t) (input_end - buffer_at_offset(input_buffer)) - skipped_bytes;
        output = (unsigned char*)_Malloc(allocation_length + 1);  // trailing '\0'
        if (output == NULL) {
            goto fail; /* allocation failure */
        }
    }

    output_pointer = output;
    /* loop through the string literal */
    while (input_pointer < input_end) {
        if (*input_pointer != '\\') {
            *output_pointer++ = *input_pointer++;
        }
        /* escape sequence */
        else {
            unsigned char sequence_length = 2;
            if ((input_end - input_pointer) < 1) {
                goto fail;
            }

            switch (input_pointer[1]) {
            case 'b':
                *output_pointer++ = '\b';
                break;
            case 'f':
                *output_pointer++ = '\f';
                break;
            case 'n':
                *output_pointer++ = '\n';
                break;
            case 'r':
                *output_pointer++ = '\r';
                break;
            case 't':
                *output_pointer++ = '\t';
                break;
            case '\"':
            case '\\':
            case '/':
                *output_pointer++ = input_pointer[1];
                break;

            /* UTF-16 literal */
            case 'u':
                sequence_length = utf16_literal_to_utf8(input_pointer, input_end, &output_pointer);
                if (sequence_length == 0) {
                    /* failed to convert UTF16-literal to UTF-8 */
                    goto fail;
                }
                break;

            default:
                goto fail;
            }
            input_pointer += sequence_length;
        }
    }

    /* zero terminate the output */
    *output_pointer = '\0';

    item->type = JString;
    item->valuestring = (char*)output;

    input_buffer->offset = (size_t) (input_end - input_buffer->content);
    input_buffer->offset++;

    return true;

fail:
    if (output != NULL) {
        _Free(output);
    }

    if (input_pointer != NULL) {
        input_buffer->offset = (size_t)(input_pointer - input_buffer->content);
    }

    return false;
}

/* Convert a 16-bit number to 4 hex digits, null-terminating it */
void htoa16(uint16_t n, unsigned char *p)
{
    int i;
    for (i=0; i<4; i++) {
        uint16_t nibble = (n >> 12) & 0xff;
        n = n << 4;
        if (nibble >= 10) {
            *p++ = 'A' + (nibble-10);
        } else {
            *p++ = '0' + nibble;
        }
    }
    *p = '\0';
}

/* Render the cstring provided to an escaped version that can be printed. */
static Jbool print_string_ptr(const unsigned char * const input, printbuffer * const output_buffer)
{
    const unsigned char *input_pointer = NULL;
    unsigned char *output = NULL;
    unsigned char *output_pointer = NULL;
    size_t output_length = 0;
    /* numbers of additional characters needed for escaping */
    size_t escape_characters = 0;

    if (output_buffer == NULL) {
        return false;
    }

    /* empty string */
    if (input == NULL) {
        output = ensure(output_buffer, 2);  // sizeof("\"\"")
        if (output == NULL) {
            return false;
        }
        output[0] = '"';
        output[1] = '"';
        output[2] = '\0';

        return true;
    }

    /* set "flag" to 1 if something needs to be escaped */
    for (input_pointer = input; *input_pointer; input_pointer++) {
        switch (*input_pointer) {
        case '\"':
        case '\\':
        case '\b':
        case '\f':
        case '\n':
        case '\r':
        case '\t':
            /* one character escape sequence */
            escape_characters++;
            break;
        default:
            if (*input_pointer < 32) {
                /* UTF-16 escape sequence uXXXX */
                escape_characters += 5;
            }
            break;
        }
    }
    output_length = (size_t)(input_pointer - input) + escape_characters;

    output = ensure(output_buffer, output_length + 2);  // sizeof("\"\"")
    if (output == NULL) {
        return false;
    }

    /* no characters have to be escaped */
    if (escape_characters == 0) {
        output[0] = '\"';
        memcpy(output + 1, input, output_length);
        output[output_length + 1] = '\"';
        output[output_length + 2] = '\0';

        return true;
    }

    output[0] = '\"';
    output_pointer = output + 1;
    /* copy the string */
    for (input_pointer = input; *input_pointer != '\0'; (void)input_pointer++, output_pointer++) {
        if ((*input_pointer > 31) && (*input_pointer != '\"') && (*input_pointer != '\\')) {
            /* normal character, copy */
            *output_pointer = *input_pointer;
        } else {
            /* character needs to be escaped */
            *output_pointer++ = '\\';
            switch (*input_pointer) {
            case '\\':
                *output_pointer = '\\';
                break;
            case '\"':
                *output_pointer = '\"';
                break;
            case '\b':
                *output_pointer = 'b';
                break;
            case '\f':
                *output_pointer = 'f';
                break;
            case '\n':
                *output_pointer = 'n';
                break;
            case '\r':
                *output_pointer = 'r';
                break;
            case '\t':
                *output_pointer = 't';
                break;
            default:
                /* escape and print as unicode codepoint */
                *output_pointer++ = 'u';
                htoa16(*input_pointer, output_pointer);
                output_pointer += 4;
                break;
            }
        }
    }
    output[output_length + 1] = '\"';
    output[output_length + 2] = '\0';

    return true;
}

/* Invoke print_string_ptr (which is useful) on an item. */
static Jbool print_string(const J * const item, printbuffer * const p)
{
    return print_string_ptr((unsigned char*)item->valuestring, p);
}

/* Predeclare these prototypes. */
static Jbool parse_value(J * const item, parse_buffer * const input_buffer);
static Jbool print_value(const J * const item, printbuffer * const output_buffer);
static Jbool parse_array(J * const item, parse_buffer * const input_buffer);
static Jbool print_array(const J * const item, printbuffer * const output_buffer);
static Jbool parse_object(J * const item, parse_buffer * const input_buffer);
static Jbool print_object(const J * const item, printbuffer * const output_buffer);

/* Utility to jump whitespace and cr/lf */
static parse_buffer *buffer_skip_whitespace(parse_buffer * const buffer)
{
    if ((buffer == NULL) || (buffer->content == NULL)) {
        return NULL;
    }

    while (can_access_at_index(buffer, 0) && (buffer_at_offset(buffer)[0] <= 32)) {
        buffer->offset++;
    }

    if (buffer->offset == buffer->length) {
        buffer->offset--;
    }

    return buffer;
}

/* skip the UTF-8 BOM (byte order mark) if it is at the beginning of a buffer */
static parse_buffer *skip_utf8_bom(parse_buffer * const buffer)
{
    if ((buffer == NULL) || (buffer->content == NULL) || (buffer->offset != 0)) {
        return NULL;
    }

    if (can_access_at_index(buffer, 4) && (strncmp((const char*)buffer_at_offset(buffer), "\xEF\xBB\xBF", 3) == 0)) {
        buffer->offset += 3;
    }

    return buffer;
}

/* Parse an object - create a new root, and populate. */
N_CJSON_PUBLIC(J *) JParseWithOpts(const char *value, const char **return_parse_end, Jbool require_null_terminated)
{
    parse_buffer buffer = { 0, 0, 0, 0 };
    J *item = NULL;

    /* reset error position */
    global_error.json = NULL;
    global_error.position = 0;

    if (value == NULL) {
        goto fail;
    }

    buffer.content = (const unsigned char*)value;
    buffer.length = strlen((const char*)value) + 1;   // Trailing '\0'
    buffer.offset = 0;

    item = JNew_Item();
    if (item == NULL) { /* memory fail */
        goto fail;
    }

    if (!parse_value(item, buffer_skip_whitespace(skip_utf8_bom(&buffer)))) {
        /* parse failure. ep is set. */
        goto fail;
    }

    /* if we require null-terminated JSON without appended garbage, skip and then check for a null terminator */
    if (require_null_terminated) {
        buffer_skip_whitespace(&buffer);
        if ((buffer.offset >= buffer.length) || buffer_at_offset(&buffer)[0] != '\0') {
            goto fail;
        }
    }
    if (return_parse_end) {
        *return_parse_end = (const char*)buffer_at_offset(&buffer);
    }

    return item;

fail:
    if (item != NULL) {
        JDelete(item);
    }

    if (value != NULL) {
        error local_error;
        local_error.json = (const unsigned char*)value;
        local_error.position = 0;

        if (buffer.offset < buffer.length) {
            local_error.position = buffer.offset;
        } else if (buffer.length > 0) {
            local_error.position = buffer.length - 1;
        }

        if (return_parse_end != NULL) {
            *return_parse_end = (const char*)local_error.json + local_error.position;
        }

        global_error = local_error;
    }

    return NULL;
}

/* Default options for JParse */
N_CJSON_PUBLIC(J *) JParse(const char *value)
{
    return JParseWithOpts(value, 0, 0);
}

#define cjson_min(a, b) ((a < b) ? a : b)

static unsigned char *print(const J * const item, Jbool format)
{
    static const size_t default_buffer_size = 128;
    printbuffer buffer[1];
    unsigned char *printed = NULL;

    memset(buffer, 0, sizeof(buffer));

    /* create buffer */
    buffer->buffer = (unsigned char*) _Malloc(default_buffer_size);
    buffer->length = default_buffer_size;
    buffer->format = format;
    if (buffer->buffer == NULL) {
        goto fail;
    }

    /* print the value */
    if (!print_value(item, buffer)) {
        goto fail;
    }
    update_offset(buffer);

    /* copy the JSON over to a new buffer */
    printed = (unsigned char*) _Malloc(buffer->offset + 1);
    if (printed == NULL) {
        goto fail;
    }
    memcpy(printed, buffer->buffer, cjson_min(buffer->length, buffer->offset + 1));
    printed[buffer->offset] = '\0'; /* just to be sure */

    /* free the buffer */
    _Free(buffer->buffer);

    return printed;

fail:
    if (buffer->buffer != NULL) {
        _Free(buffer->buffer);
    }

    if (printed != NULL) {
        _Free(printed);
    }

    return NULL;
}

/* Render a J item/entity/structure to text. */
N_CJSON_PUBLIC(char *) JPrint(const J *item)
{
    if (item == NULL) {
        return (char *)"";
    }
    return (char*)print(item, true);
}

N_CJSON_PUBLIC(char *) JPrintUnformatted(const J *item)
{
    if (item == NULL) {
        return (char *)"";
    }
    return (char*)print(item, false);
}

N_CJSON_PUBLIC(char *) JPrintBuffered(const J *item, int prebuffer, Jbool fmt)
{
    printbuffer p = { 0, 0, 0, 0, 0, 0 };

    if (item == NULL) {
        return (char *)"";
    }

    if (prebuffer < 0) {
        return NULL;
    }

    p.buffer = (unsigned char*)_Malloc((size_t)prebuffer);
    if (!p.buffer) {
        return NULL;
    }

    p.length = (size_t)prebuffer;
    p.offset = 0;
    p.noalloc = false;
    p.format = fmt;

    if (!print_value(item, &p)) {
        _Free(p.buffer);
        return NULL;
    }

    return (char*)p.buffer;
}

N_CJSON_PUBLIC(Jbool) JPrintPreallocated(J *item, char *buf, const int len, const Jbool fmt)
{
    printbuffer p = { 0, 0, 0, 0, 0, 0 };

    if (item == NULL) {
        return false;
    }
    if ((len < 0) || (buf == NULL)) {
        return false;
    }

    p.buffer = (unsigned char*)buf;
    p.length = (size_t)len;
    p.offset = 0;
    p.noalloc = true;
    p.format = fmt;

    return print_value(item, &p);
}

/* Parser core - when encountering text, process appropriately. */
static Jbool parse_value(J * const item, parse_buffer * const input_buffer)
{
    if (item == NULL) {
        return false;
    }
    if ((input_buffer == NULL) || (input_buffer->content == NULL)) {
        return false; /* no input */
    }

    /* parse the different types of values */
    /* null */
    if (can_read(input_buffer, 4) && (strncmp((const char*)buffer_at_offset(input_buffer), c_null, c_null_len) == 0)) {
        item->type = JNULL;
        input_buffer->offset += 4;
        return true;
    }
    /* false */
    if (can_read(input_buffer, 5) && (strncmp((const char*)buffer_at_offset(input_buffer), c_false, c_false_len) == 0)) {
        item->type = JFalse;
        input_buffer->offset += 5;
        return true;
    }
    /* true */
    if (can_read(input_buffer, 4) && (strncmp((const char*)buffer_at_offset(input_buffer), c_true, c_true_len) == 0)) {
        item->type = JTrue;
        item->valueint = 1;
        input_buffer->offset += 4;
        return true;
    }
    /* string */
    if (can_access_at_index(input_buffer, 0) && (buffer_at_offset(input_buffer)[0] == '\"')) {
        return parse_string(item, input_buffer);
    }
    /* number */
    if (can_access_at_index(input_buffer, 0) && ((buffer_at_offset(input_buffer)[0] == '-') || ((buffer_at_offset(input_buffer)[0] >= '0') && (buffer_at_offset(input_buffer)[0] <= '9')))) {
        return parse_number(item, input_buffer);
    }
    /* array */
    if (can_access_at_index(input_buffer, 0) && (buffer_at_offset(input_buffer)[0] == '[')) {
        return parse_array(item, input_buffer);
    }
    /* object */
    if (can_access_at_index(input_buffer, 0) && (buffer_at_offset(input_buffer)[0] == '{')) {
        return parse_object(item, input_buffer);
    }

    return false;
}

/* Render a value to text. */
static Jbool print_value(const J * const item, printbuffer * const output_buffer)
{
    unsigned char *output = NULL;

    if ((item == NULL) || (output_buffer == NULL)) {
        return false;
    }

    switch ((item->type) & 0xFF) {
    case JNULL:
        output = ensure(output_buffer, c_null_len+1);
        if (output == NULL) {
            return false;
        }
        strcpy((char*)output, c_null);
        return true;

    case JFalse:
        output = ensure(output_buffer, c_false_len+1);
        if (output == NULL) {
            return false;
        }
        strcpy((char*)output, c_false);
        return true;

    case JTrue:
        output = ensure(output_buffer, c_true_len+1);
        if (output == NULL) {
            return false;
        }
        strcpy((char*)output, c_true);
        return true;

    case JNumber:
        return print_number(item, output_buffer);

    case JRaw: {
        size_t raw_length = 0;
        if (item->valuestring == NULL) {
            return false;
        }

        raw_length = strlen(item->valuestring) + 1;   // Trailing '\0';
        output = ensure(output_buffer, raw_length);
        if (output == NULL) {
            return false;
        }
        memcpy(output, item->valuestring, raw_length);
        return true;
    }

    case JString:
        return print_string(item, output_buffer);

    case JArray:
        return print_array(item, output_buffer);

    case JObject:
        return print_object(item, output_buffer);

    default:
        return false;
    }
}

/* Build an array from input text. */
static Jbool parse_array(J * const item, parse_buffer * const input_buffer)
{
    J *head = NULL; /* head of the linked list */
    J *current_item = NULL;

    if (input_buffer->depth >= N_CJSON_NESTING_LIMIT) {
        return false; /* to deeply nested */
    }
    input_buffer->depth++;

    if (buffer_at_offset(input_buffer)[0] != '[') {
        /* not an array */
        goto fail;
    }

    input_buffer->offset++;
    buffer_skip_whitespace(input_buffer);
    if (can_access_at_index(input_buffer, 0) && (buffer_at_offset(input_buffer)[0] == ']')) {
        /* empty array */
        goto success;
    }

    /* check if we skipped to the end of the buffer */
    if (cannot_access_at_index(input_buffer, 0)) {
        input_buffer->offset--;
        goto fail;
    }

    /* step back to character in front of the first element */
    input_buffer->offset--;
    /* loop through the comma separated array elements */
    do {
        /* allocate next item */
        J *new_item = JNew_Item();
        if (new_item == NULL) {
            goto fail; /* allocation failure */
        }

        /* attach next item to list */
        if (head == NULL) {
            /* start the linked list */
            current_item = head = new_item;
        } else {
            /* add to the end and advance */
            current_item->next = new_item;
            new_item->prev = current_item;
            current_item = new_item;
        }

        /* parse next value */
        input_buffer->offset++;
        buffer_skip_whitespace(input_buffer);
        if (!parse_value(current_item, input_buffer)) {
            goto fail; /* failed to parse value */
        }
        buffer_skip_whitespace(input_buffer);
    } while (can_access_at_index(input_buffer, 0) && (buffer_at_offset(input_buffer)[0] == ','));

    if (cannot_access_at_index(input_buffer, 0) || buffer_at_offset(input_buffer)[0] != ']') {
        goto fail; /* expected end of array */
    }

success:
    input_buffer->depth--;

    item->type = JArray;
    item->child = head;

    input_buffer->offset++;

    return true;

fail:
    if (head != NULL) {
        JDelete(head);
    }

    return false;
}

/* Render an array to text */
static Jbool print_array(const J * const item, printbuffer * const output_buffer)
{
    unsigned char *output_pointer = NULL;
    size_t length = 0;
    J *current_element = item->child;

    if (output_buffer == NULL) {
        return false;
    }

    /* Compose the output array. */
    /* opening square bracket */
    output_pointer = ensure(output_buffer, 1);
    if (output_pointer == NULL) {
        return false;
    }

    *output_pointer = '[';
    output_buffer->offset++;
    output_buffer->depth++;

    while (current_element != NULL) {
        if (!print_value(current_element, output_buffer)) {
            return false;
        }
        update_offset(output_buffer);
        if (current_element->next) {
            length = (size_t) (output_buffer->format ? 2 : 1);
            output_pointer = ensure(output_buffer, length + 1);
            if (output_pointer == NULL) {
                return false;
            }
            *output_pointer++ = ',';
            if(output_buffer->format) {
                *output_pointer++ = ' ';
            }
            *output_pointer = '\0';
            output_buffer->offset += length;
        }
        current_element = current_element->next;
    }

    output_pointer = ensure(output_buffer, 2);
    if (output_pointer == NULL) {
        return false;
    }
    *output_pointer++ = ']';
    *output_pointer = '\0';
    output_buffer->depth--;

    return true;
}

/* Build an object from the text. */
static Jbool parse_object(J * const item, parse_buffer * const input_buffer)
{
    J *head = NULL; /* linked list head */
    J *current_item = NULL;

    if (input_buffer->depth >= N_CJSON_NESTING_LIMIT) {
        return false; /* to deeply nested */
    }
    input_buffer->depth++;

    if (cannot_access_at_index(input_buffer, 0) || (buffer_at_offset(input_buffer)[0] != '{')) {
        goto fail; /* not an object */
    }

    input_buffer->offset++;
    buffer_skip_whitespace(input_buffer);
    if (can_access_at_index(input_buffer, 0) && (buffer_at_offset(input_buffer)[0] == '}')) {
        goto success; /* empty object */
    }

    /* check if we skipped to the end of the buffer */
    if (cannot_access_at_index(input_buffer, 0)) {
        input_buffer->offset--;
        goto fail;
    }

    /* step back to character in front of the first element */
    input_buffer->offset--;
    /* loop through the comma separated array elements */
    do {
        /* allocate next item */
        J *new_item = JNew_Item();
        if (new_item == NULL) {
            goto fail; /* allocation failure */
        }

        /* attach next item to list */
        if (head == NULL) {
            /* start the linked list */
            current_item = head = new_item;
        } else {
            /* add to the end and advance */
            current_item->next = new_item;
            new_item->prev = current_item;
            current_item = new_item;
        }

        /* parse the name of the child */
        input_buffer->offset++;
        buffer_skip_whitespace(input_buffer);
        if (!parse_string(current_item, input_buffer)) {
            goto fail; /* faile to parse name */
        }
        buffer_skip_whitespace(input_buffer);

        /* swap valuestring and string, because we parsed the name */
        current_item->string = current_item->valuestring;
        current_item->valuestring = NULL;

        if (cannot_access_at_index(input_buffer, 0) || (buffer_at_offset(input_buffer)[0] != ':')) {
            goto fail; /* invalid object */
        }

        /* parse the value */
        input_buffer->offset++;
        buffer_skip_whitespace(input_buffer);
        if (!parse_value(current_item, input_buffer)) {
            goto fail; /* failed to parse value */
        }
        buffer_skip_whitespace(input_buffer);
    } while (can_access_at_index(input_buffer, 0) && (buffer_at_offset(input_buffer)[0] == ','));

    if (cannot_access_at_index(input_buffer, 0) || (buffer_at_offset(input_buffer)[0] != '}')) {
        goto fail; /* expected end of object */
    }

success:
    input_buffer->depth--;

    item->type = JObject;
    item->child = head;

    input_buffer->offset++;
    return true;

fail:
    if (head != NULL) {
        JDelete(head);
    }

    return false;
}

/* Render an object to text. */
static Jbool print_object(const J * const item, printbuffer * const output_buffer)
{
    unsigned char *output_pointer = NULL;
    size_t length = 0;
    J *current_item = item->child;

    if (output_buffer == NULL) {
        return false;
    }

    /* Compose the output: */
    length = (size_t) (output_buffer->format ? 2 : 1); /* fmt: {\n */
    output_pointer = ensure(output_buffer, length + 1);
    if (output_pointer == NULL) {
        return false;
    }

    *output_pointer++ = '{';
    output_buffer->depth++;
    if (output_buffer->format) {
        *output_pointer++ = '\n';
    }
    output_buffer->offset += length;

    while (current_item) {
        if (output_buffer->format) {
            size_t i;
            output_pointer = ensure(output_buffer, output_buffer->depth);
            if (output_pointer == NULL) {
                return false;
            }
            for (i = 0; i < output_buffer->depth; i++) {
                *output_pointer++ = '\t';
            }
            output_buffer->offset += output_buffer->depth;
        }

        /* print key */
        if (!print_string_ptr((unsigned char*)current_item->string, output_buffer)) {
            return false;
        }
        update_offset(output_buffer);

        length = (size_t) (output_buffer->format ? 2 : 1);
        output_pointer = ensure(output_buffer, length);
        if (output_pointer == NULL) {
            return false;
        }
        *output_pointer++ = ':';
        if (output_buffer->format) {
            *output_pointer++ = '\t';
        }
        output_buffer->offset += length;

        /* print value */
        if (!print_value(current_item, output_buffer)) {
            return false;
        }
        update_offset(output_buffer);

        /* print comma if not last */
        length = (size_t) ((output_buffer->format ? 1 : 0) + (current_item->next ? 1 : 0));
        output_pointer = ensure(output_buffer, length + 1);
        if (output_pointer == NULL) {
            return false;
        }
        if (current_item->next) {
            *output_pointer++ = ',';
        }

        if (output_buffer->format) {
            *output_pointer++ = '\n';
        }
        *output_pointer = '\0';
        output_buffer->offset += length;

        current_item = current_item->next;
    }

    output_pointer = ensure(output_buffer, output_buffer->format ? (output_buffer->depth + 1) : 2);
    if (output_pointer == NULL) {
        return false;
    }
    if (output_buffer->format) {
        size_t i;
        for (i = 0; i < (output_buffer->depth - 1); i++) {
            *output_pointer++ = '\t';
        }
    }
    *output_pointer++ = '}';
    *output_pointer = '\0';
    output_buffer->depth--;

    return true;
}

/* Get Array size/item / object item. */
N_CJSON_PUBLIC(int) JGetArraySize(const J *array)
{
    J *child = NULL;
    size_t size = 0;

    if (array == NULL) {
        return 0;
    }

    child = array->child;

    while(child != NULL) {
        size++;
        child = child->next;
    }

    /* FIXME: Can overflow here. Cannot be fixed without breaking the API */

    return (int)size;
}

static J* get_array_item(const J *array, size_t index)
{
    J *current_child = NULL;

    if (array == NULL) {
        return NULL;
    }

    current_child = array->child;
    while ((current_child != NULL) && (index > 0)) {
        index--;
        current_child = current_child->next;
    }

    return current_child;
}

N_CJSON_PUBLIC(J *) JGetArrayItem(const J *array, int index)
{
    if (array == NULL) {
        return NULL;
    }
    if (index < 0) {
        return NULL;
    }

    return get_array_item(array, (size_t)index);
}

static J *get_object_item(const J * const object, const char * const name, const Jbool case_sensitive)
{
    J *current_element = NULL;

    if ((object == NULL) || (name == NULL)) {
        return NULL;
    }

    current_element = object->child;
    if (case_sensitive) {
        while ((current_element != NULL) && (strcmp(name, current_element->string) != 0)) {
            current_element = current_element->next;
        }
    } else {
        while ((current_element != NULL) && (case_insensitive_strcmp((const unsigned char*)name, (const unsigned char*)(current_element->string)) != 0)) {
            current_element = current_element->next;
        }
    }

    return current_element;
}

N_CJSON_PUBLIC(J *) JGetObjectItem(const J * const object, const char * const string)
{
    if (object == NULL) {
        return NULL;
    }
    return get_object_item(object, string, false);
}

N_CJSON_PUBLIC(J *) JGetObjectItemCaseSensitive(const J * const object, const char * const string)
{
    if (object == NULL) {
        return NULL;
    }
    return get_object_item(object, string, true);
}

N_CJSON_PUBLIC(Jbool) JHasObjectItem(const J *object, const char *string)
{
    if (object == NULL) {
        return false;
    }
    return JGetObjectItem(object, string) ? 1 : 0;
}

/* Utility for array list handling. */
static void suffix_object(J *prev, J *item)
{
    prev->next = item;
    item->prev = prev;
}

/* Utility for handling references. */
static J *create_reference(const J *item)
{
    J *reference = NULL;
    if (item == NULL) {
        return NULL;
    }

    reference = JNew_Item();
    if (reference == NULL) {
        return NULL;
    }

    memcpy(reference, item, sizeof(J));
    reference->string = NULL;
    reference->type |= JIsReference;
    reference->next = reference->prev = NULL;
    return reference;
}

static Jbool add_item_to_array(J *array, J *item)
{
    J *child = NULL;

    if ((item == NULL) || (array == NULL)) {
        return false;
    }

    child = array->child;

    if (child == NULL) {
        /* list is empty, start new one */
        array->child = item;
    } else {
        /* append to the end */
        while (child->next) {
            child = child->next;
        }
        suffix_object(child, item);
    }

    return true;
}

/* Add item to array/object. */
N_CJSON_PUBLIC(void) JAddItemToArray(J *array, J *item)
{
    if (array == NULL || item == NULL) {
        return;
    }
    add_item_to_array(array, item);
}

#if defined(__clang__) || (defined(__GNUC__)  && ((__GNUC__ > 4) || ((__GNUC__ == 4) && (__GNUC_MINOR__ > 5))))
#pragma GCC diagnostic push
#endif
#ifdef __GNUC__
#pragma GCC diagnostic ignored "-Wcast-qual"
#endif
/* helper function to cast away const */
static void* cast_away_const(const void* string)
{
    return (void*)string;
}
#if defined(__clang__) || (defined(__GNUC__)  && ((__GNUC__ > 4) || ((__GNUC__ == 4) && (__GNUC_MINOR__ > 5))))
#pragma GCC diagnostic pop
#endif


static Jbool add_item_to_object(J * const object, const char * const string, J * const item, const Jbool constant_key)
{
    char *new_key = NULL;
    int new_type = JInvalid;

    if ((object == NULL) || (string == NULL) || (item == NULL)) {
        return false;
    }

    if (constant_key) {
        new_key = (char*)cast_away_const(string);
        new_type = item->type | JStringIsConst;
    } else {
        new_key = (char*)Jstrdup((const unsigned char*)string);
        if (new_key == NULL) {
            return false;
        }

        new_type = item->type & ~JStringIsConst;
    }

    if (!(item->type & JStringIsConst) && (item->string != NULL)) {
        _Free(item->string);
    }

    item->string = new_key;
    item->type = new_type;

    return add_item_to_array(object, item);
}

N_CJSON_PUBLIC(void) JAddItemToObject(J *object, const char *string, J *item)
{
    if (object == NULL || string == NULL || item == NULL) {
        return;
    }
    add_item_to_object(object, string, item, false);
}

/* Add an item to an object with constant string as key */
N_CJSON_PUBLIC(void) JAddItemToObjectCS(J *object, const char *string, J *item)
{
    if (object == NULL || string == NULL || item == NULL) {
        return;
    }
    add_item_to_object(object, string, item, true);
}

N_CJSON_PUBLIC(void) JAddItemReferenceToArray(J *array, J *item)
{
    if (array == NULL || item == NULL) {
        return;
    }
    add_item_to_array(array, create_reference(item));
}

N_CJSON_PUBLIC(void) JAddItemReferenceToObject(J *object, const char *string, J *item)
{
    if (object == NULL || string == NULL || item == NULL) {
        return;
    }
    add_item_to_object(object, string, create_reference(item), false);
}

N_CJSON_PUBLIC(J*) JAddNullToObject(J * const object, const char * const name)
{
    if (object == NULL) {
        return NULL;
    }
    J *null = JCreateNull();
    if (add_item_to_object(object, name, null, false)) {
        return null;
    }

    JDelete(null);
    return NULL;
}

N_CJSON_PUBLIC(J*) JAddTrueToObject(J * const object, const char * const name)
{
    if (object == NULL) {
        return NULL;
    }

    J *true_item = JCreateTrue();
    if (add_item_to_object(object, name, true_item, false)) {
        return true_item;
    }

    JDelete(true_item);
    return NULL;
}

N_CJSON_PUBLIC(J*) JAddFalseToObject(J * const object, const char * const name)
{
    if (object == NULL) {
        return NULL;
    }

    J *false_item = JCreateFalse();
    if (add_item_to_object(object, name, false_item, false)) {
        return false_item;
    }

    JDelete(false_item);
    return NULL;
}

N_CJSON_PUBLIC(J*) JAddBoolToObject(J * const object, const char * const name, const Jbool boolean)
{
    if (object == NULL) {
        return NULL;
    }

    J *bool_item = JCreateBool(boolean);
    if (add_item_to_object(object, name, bool_item, false)) {
        return bool_item;
    }

    JDelete(bool_item);
    return NULL;
}

N_CJSON_PUBLIC(J*) JAddNumberToObject(J * const object, const char * const name, const JNUMBER number)
{
    if (object == NULL) {
        return NULL;
    }

    J *number_item = JCreateNumber(number);
    if (add_item_to_object(object, name, number_item, false)) {
        return number_item;
    }

    JDelete(number_item);
    return NULL;
}

N_CJSON_PUBLIC(J*) JAddStringToObject(J * const object, const char * const name, const char * const string)
{
    if (object == NULL || string == NULL) {
        return NULL;
    }

    J *string_item = JCreateString(string);
    if (add_item_to_object(object, name, string_item, false)) {
        return string_item;
    }

    JDelete(string_item);
    return NULL;
}

N_CJSON_PUBLIC(J*) JAddRawToObject(J * const object, const char * const name, const char * const raw)
{
    if (object == NULL || raw == NULL) {
        return NULL;
    }

    J *raw_item = JCreateRaw(raw);
    if (add_item_to_object(object, name, raw_item, false)) {
        return raw_item;
    }

    JDelete(raw_item);
    return NULL;
}

N_CJSON_PUBLIC(J*) JAddObjectToObject(J * const object, const char * const name)
{
    if (object == NULL) {
        return NULL;
    }

    J *object_item = JCreateObject();
    if (add_item_to_object(object, name, object_item, false)) {
        return object_item;
    }

    JDelete(object_item);
    return NULL;
}

N_CJSON_PUBLIC(J*) JAddArrayToObject(J * const object, const char * const name)
{
    if (object == NULL) {
        return NULL;
    }

    J *array = JCreateArray();
    if (add_item_to_object(object, name, array, false)) {
        return array;
    }

    JDelete(array);
    return NULL;
}

N_CJSON_PUBLIC(J *) JDetachItemViaPointer(J *parent, J * const item)
{
    if (parent == NULL || item == NULL) {
        return NULL;
    }

    if (item->prev != NULL) {
        /* not the first element */
        item->prev->next = item->next;
    }
    if (item->next != NULL) {
        /* not the last element */
        item->next->prev = item->prev;
    }

    if (item == parent->child) {
        /* first element */
        parent->child = item->next;
    }
    /* make sure the detached item doesn't point anywhere anymore */
    item->prev = NULL;
    item->next = NULL;

    return item;
}

N_CJSON_PUBLIC(J *) JDetachItemFromArray(J *array, int which)
{
    if (array == NULL) {
        return NULL;
    }
    if (which < 0) {
        return NULL;
    }

    return JDetachItemViaPointer(array, get_array_item(array, (size_t)which));
}

N_CJSON_PUBLIC(void) JDeleteItemFromArray(J *array, int which)
{
    if (array == NULL) {
        return;
    }
    JDelete(JDetachItemFromArray(array, which));
}

N_CJSON_PUBLIC(J *) JDetachItemFromObject(J *object, const char *string)
{
    if (object == NULL) {
        return NULL;
    }

    J *to_detach = JGetObjectItem(object, string);

    return JDetachItemViaPointer(object, to_detach);
}

N_CJSON_PUBLIC(J *) JDetachItemFromObjectCaseSensitive(J *object, const char *string)
{
    if (object == NULL) {
        return NULL;
    }

    J *to_detach = JGetObjectItemCaseSensitive(object, string);

    return JDetachItemViaPointer(object, to_detach);
}

N_CJSON_PUBLIC(void) JDeleteItemFromObject(J *object, const char *string)
{
    if (object == NULL) {
        return;
    }
    JDelete(JDetachItemFromObject(object, string));
}

N_CJSON_PUBLIC(void) JDeleteItemFromObjectCaseSensitive(J *object, const char *string)
{
    if (object == NULL) {
        return;
    }
    JDelete(JDetachItemFromObjectCaseSensitive(object, string));
}

/* Replace array/object items with new ones. */
N_CJSON_PUBLIC(void) JInsertItemInArray(J *array, int which, J *newitem)
{
    if (array == NULL || newitem == NULL) {
        return;
    }

    J *after_inserted = NULL;

    if (which < 0) {
        return;
    }

    after_inserted = get_array_item(array, (size_t)which);
    if (after_inserted == NULL) {
        add_item_to_array(array, newitem);
        return;
    }

    newitem->next = after_inserted;
    newitem->prev = after_inserted->prev;
    after_inserted->prev = newitem;
    if (after_inserted == array->child) {
        array->child = newitem;
    } else {
        newitem->prev->next = newitem;
    }
}

N_CJSON_PUBLIC(Jbool) JReplaceItemViaPointer(J * const parent, J * const item, J * replacement)
{
    if (parent == NULL || replacement == NULL || item == NULL) {
        return false;
    }

    if (replacement == item) {
        return true;
    }

    replacement->next = item->next;
    replacement->prev = item->prev;

    if (replacement->next != NULL) {
        replacement->next->prev = replacement;
    }
    if (replacement->prev != NULL) {
        replacement->prev->next = replacement;
    }
    if (parent->child == item) {
        parent->child = replacement;
    }

    item->next = NULL;
    item->prev = NULL;
    JDelete(item);

    return true;
}

N_CJSON_PUBLIC(void) JReplaceItemInArray(J *array, int which, J *newitem)
{
    if (array == NULL || newitem == NULL) {
        return;
    }

    if (which < 0) {
        return;
    }

    JReplaceItemViaPointer(array, get_array_item(array, (size_t)which), newitem);
}

static Jbool replace_item_in_object(J *object, const char *string, J *replacement, Jbool case_sensitive)
{
    if (object == NULL || replacement == NULL || string == NULL) {
        return false;
    }

    /* replace the name in the replacement */
    if (!(replacement->type & JStringIsConst) && (replacement->string != NULL)) {
        _Free(replacement->string);
    }
    replacement->string = (char*)Jstrdup((const unsigned char*)string);
    replacement->type &= ~JStringIsConst;

    JReplaceItemViaPointer(object, get_object_item(object, string, case_sensitive), replacement);

    return true;
}

N_CJSON_PUBLIC(void) JReplaceItemInObject(J *object, const char *string, J *newitem)
{
    if (object == NULL || newitem == NULL) {
        return;
    }
    replace_item_in_object(object, string, newitem, false);
}

N_CJSON_PUBLIC(void) JReplaceItemInObjectCaseSensitive(J *object, const char *string, J *newitem)
{
    if (object == NULL || newitem == NULL) {
        return;
    }
    replace_item_in_object(object, string, newitem, true);
}

/* Create basic types: */
N_CJSON_PUBLIC(J *) JCreateNull(void)
{
    J *item = JNew_Item();
    if(item) {
        item->type = JNULL;
    }
    return item;
}

N_CJSON_PUBLIC(J *) JCreateTrue(void)
{
    J *item = JNew_Item();
    if(item) {
        item->type = JTrue;
    }
    return item;
}

N_CJSON_PUBLIC(J *) JCreateFalse(void)
{
    J *item = JNew_Item();
    if(item) {
        item->type = JFalse;
    }
    return item;
}

N_CJSON_PUBLIC(J *) JCreateBool(Jbool b)
{
    J *item = JNew_Item();
    if(item) {
        item->type = b ? JTrue : JFalse;
    }
    return item;
}

N_CJSON_PUBLIC(J *) JCreateNumber(JNUMBER num)
{
    J *item = JNew_Item();
    if(item) {
        item->type = JNumber;
        item->valuenumber = num;
        /* use saturation in case of overflow */
        if (num >= LONG_MAX) {
            item->valueint = LONG_MAX;
        } else if (num <= LONG_MIN) {
            item->valueint = LONG_MIN;
        } else {
            item->valueint = (long int)num;
        }
    }
    return item;
}

N_CJSON_PUBLIC(J *) JCreateString(const char *string)
{
    J *item = JNew_Item();
    if(item) {
        item->type = JString;
        item->valuestring = (char*)Jstrdup((const unsigned char*)string);
        if(!item->valuestring) {
            JDelete(item);
            return NULL;
        }
    }
    return item;
}

N_CJSON_PUBLIC(J *) JCreateStringValue(const char *string)
{
    J *item = JNew_Item();
    if (item != NULL) {
        item->type = JString;
        item->valuestring = (char*)cast_away_const(string);
    }
    return item;
}

N_CJSON_PUBLIC(J *) JCreateStringReference(const char *string)
{
    J *item = JNew_Item();
    if (item != NULL) {
        item->type = JString | JIsReference;
        item->valuestring = (char*)cast_away_const(string);
    }
    return item;
}

N_CJSON_PUBLIC(J *) JCreateObjectReference(const J *child)
{
    if (child == NULL) {
        return NULL;
    }
    J *item = JNew_Item();
    if (item != NULL) {
        item->type = JObject | JIsReference;
        item->child = (J*)cast_away_const(child);
    }
    return item;
}

N_CJSON_PUBLIC(J *) JCreateArrayReference(const J *child)
{
    if (child == NULL) {
        return NULL;
    }
    J *item = JNew_Item();
    if (item != NULL) {
        item->type = JArray | JIsReference;
        item->child = (J*)cast_away_const(child);
    }
    return item;
}

N_CJSON_PUBLIC(J *) JCreateRaw(const char *raw)
{
    J *item = JNew_Item();
    if(item) {
        item->type = JRaw;
        item->valuestring = (char*)Jstrdup((const unsigned char*)raw);
        if(!item->valuestring) {
            JDelete(item);
            return NULL;
        }
    }
    return item;
}

N_CJSON_PUBLIC(J *) JCreateArray(void)
{
    J *item = JNew_Item();
    if(item) {
        item->type=JArray;
    }
    return item;
}

N_CJSON_PUBLIC(J *) JCreateObject(void)
{
    J *item = JNew_Item();
    if (item) {
        item->type = JObject;
    }
    return item;
}

/* Create Arrays: */
N_CJSON_PUBLIC(J *) JCreateIntArray(const long int *numbers, int count)
{
    size_t i = 0;
    J *n = NULL;
    J *p = NULL;
    J *a = NULL;

    if ((count < 0) || (numbers == NULL)) {
        return NULL;
    }

    a = JCreateArray();
    for(i = 0; a && (i < (size_t)count); i++) {
        n = JCreateNumber(numbers[i]);
        if (!n) {
            JDelete(a);
            return NULL;
        }
        if(!i) {
            a->child = n;
        } else {
            suffix_object(p, n);
        }
        p = n;
    }

    return a;
}

N_CJSON_PUBLIC(J *) JCreateNumberArray(const JNUMBER *numbers, int count)
{
    size_t i = 0;
    J *n = NULL;
    J *p = NULL;
    J *a = NULL;

    if ((count < 0) || (numbers == NULL)) {
        return NULL;
    }

    a = JCreateArray();

    for(i = 0; a && (i < (size_t)count); i++) {
        n = JCreateNumber(numbers[i]);
        if(!n) {
            JDelete(a);
            return NULL;
        }
        if(!i) {
            a->child = n;
        } else {
            suffix_object(p, n);
        }
        p = n;
    }

    return a;
}

N_CJSON_PUBLIC(J *) JCreateStringArray(const char **strings, int count)
{
    size_t i = 0;
    J *n = NULL;
    J *p = NULL;
    J *a = NULL;

    if ((count < 0) || (strings == NULL)) {
        return NULL;
    }

    a = JCreateArray();

    for (i = 0; a && (i < (size_t)count); i++) {
        n = JCreateString(strings[i]);
        if(!n) {
            JDelete(a);
            return NULL;
        }
        if(!i) {
            a->child = n;
        } else {
            suffix_object(p,n);
        }
        p = n;
    }

    return a;
}

/* Duplication */
N_CJSON_PUBLIC(J *) JDuplicate(const J *item, Jbool recurse)
{
    J *newitem = NULL;
    J *child = NULL;
    J *next = NULL;
    J *newchild = NULL;

    /* Bail on bad ptr */
    if (!item) {
        goto fail;
    }
    /* Create new item */
    newitem = JNew_Item();
    if (!newitem) {
        goto fail;
    }
    /* Copy over all vars */
    newitem->type = item->type & (~JIsReference);
    newitem->valueint = item->valueint;
    newitem->valuenumber = item->valuenumber;
    if (item->valuestring) {
        newitem->valuestring = (char*)Jstrdup((unsigned char*)item->valuestring);
        if (!newitem->valuestring) {
            goto fail;
        }
    }
    if (item->string) {
        newitem->string = (item->type&JStringIsConst) ? item->string : (char*)Jstrdup((unsigned char*)item->string);
        if (!newitem->string) {
            goto fail;
        }
    }
    /* If non-recursive, then we're done! */
    if (!recurse) {
        return newitem;
    }
    /* Walk the ->next chain for the child. */
    child = item->child;
    while (child != NULL) {
        newchild = JDuplicate(child, true); /* Duplicate (with recurse) each item in the ->next chain */
        if (!newchild) {
            goto fail;
        }
        if (next != NULL) {
            /* If newitem->child already set, then crosswire ->prev and ->next and move on */
            next->next = newchild;
            newchild->prev = next;
            next = newchild;
        } else {
            /* Set newitem->child and move to it */
            newitem->child = newchild;
            next = newchild;
        }
        child = child->next;
    }

    return newitem;

fail:
    if (newitem != NULL) {
        JDelete(newitem);
    }

    return NULL;
}

N_CJSON_PUBLIC(void) JMinify(char *json)
{
    unsigned char *into = (unsigned char*)json;

    if (json == NULL) {
        return;
    }

    while (*json) {
        if (*json == ' ') {
            json++;
        } else if (*json == '\t') {
            /* Whitespace characters. */
            json++;
        } else if (*json == '\r') {
            json++;
        } else if (*json=='\n') {
            json++;
        } else if ((*json == '/') && (json[1] == '/')) {
            /* double-slash comments, to end of line. */
            while (*json && (*json != '\n')) {
                json++;
            }
        } else if ((*json == '/') && (json[1] == '*')) {
            /* multiline comments. */
            while (*json && !((*json == '*') && (json[1] == '/'))) {
                json++;
            }
            json += 2;
        } else if (*json == '\"') {
            /* string literals, which are \" sensitive. */
            *into++ = (unsigned char)*json++;
            while (*json && (*json != '\"')) {
                if (*json == '\\') {
                    *into++ = (unsigned char)*json++;
                }
                *into++ = (unsigned char)*json++;
            }
            *into++ = (unsigned char)*json++;
        } else {
            /* All other characters. */
            *into++ = (unsigned char)*json++;
        }
    }

    /* and null-terminate. */
    *into = '\0';
}

N_CJSON_PUBLIC(Jbool) JIsInvalid(const J * const item)
{
    if (item == NULL) {
        return false;
    }
    return (item->type & 0xFF) == JInvalid;
}

N_CJSON_PUBLIC(Jbool) JIsFalse(const J * const item)
{
    if (item == NULL) {
        return false;
    }
    return (item->type & 0xFF) == JFalse;
}

N_CJSON_PUBLIC(Jbool) JIsTrue(const J * const item)
{
    if (item == NULL) {
        return false;
    }
    return (item->type & 0xff) == JTrue;
}


N_CJSON_PUBLIC(Jbool) JIsBool(const J * const item)
{
    if (item == NULL) {
        return false;
    }
    return (item->type & (JTrue | JFalse)) != 0;
}
N_CJSON_PUBLIC(Jbool) JIsNull(const J * const item)
{
    if (item == NULL) {
        return false;
    }
    return (item->type & 0xFF) == JNULL;
}

N_CJSON_PUBLIC(Jbool) JIsNumber(const J * const item)
{
    if (item == NULL) {
        return false;
    }
    return (item->type & 0xFF) == JNumber;
}

N_CJSON_PUBLIC(Jbool) JIsString(const J * const item)
{
    if (item == NULL) {
        return false;
    }
    return (item->type & 0xFF) == JString;
}

N_CJSON_PUBLIC(Jbool) JIsArray(const J * const item)
{
    if (item == NULL) {
        return false;
    }
    return (item->type & 0xFF) == JArray;
}

N_CJSON_PUBLIC(Jbool) JIsObject(const J * const item)
{
    if (item == NULL) {
        return false;
    }
    return (item->type & 0xFF) == JObject;
}

N_CJSON_PUBLIC(Jbool) JIsRaw(const J * const item)
{
    if (item == NULL) {
        return false;
    }
    return (item->type & 0xFF) == JRaw;
}

N_CJSON_PUBLIC(Jbool) JCompare(const J * const a, const J * const b, const Jbool case_sensitive)
{
    if ((a == NULL) || (b == NULL) || ((a->type & 0xFF) != (b->type & 0xFF)) || JIsInvalid(a)) {
        return false;
    }

    /* check if type is valid */
    switch (a->type & 0xFF) {
    case JFalse:
    case JTrue:
    case JNULL:
    case JNumber:
    case JString:
    case JRaw:
    case JArray:
    case JObject:
        break;

    default:
        return false;
    }

    /* identical objects are equal */
    if (a == b) {
        return true;
    }

    switch (a->type & 0xFF) {
    /* in these cases and equal type is enough */
    case JFalse:
    case JTrue:
    case JNULL:
        return true;

    case JNumber:
        if (a->valuenumber == b->valuenumber) {
            return true;
        }
        return false;

    case JString:
    case JRaw:
        if ((a->valuestring == NULL) || (b->valuestring == NULL)) {
            return false;
        }
        if (strcmp(a->valuestring, b->valuestring) == 0) {
            return true;
        }

        return false;

    case JArray: {
        J *a_element = a->child;
        J *b_element = b->child;

        for (; (a_element != NULL) && (b_element != NULL);) {
            if (!JCompare(a_element, b_element, case_sensitive)) {
                return false;
            }

            a_element = a_element->next;
            b_element = b_element->next;
        }

        /* one of the arrays is longer than the other */
        if (a_element != b_element) {
            return false;
        }

        return true;
    }

    case JObject: {
        J *a_element = NULL;
        J *b_element = NULL;
        JArrayForEach(a_element, a) {
            /* TODO This has O(n^2) runtime, which is horrible! */
            b_element = get_object_item(b, a_element->string, case_sensitive);
            if (b_element == NULL) {
                return false;
            }

            if (!JCompare(a_element, b_element, case_sensitive)) {
                return false;
            }
        }

        /* doing this twice, once on a and b to prevent true comparison if a subset of b
         * TODO: Do this the proper way, this is just a fix for now */
        JArrayForEach(b_element, b) {
            a_element = get_object_item(a, b_element->string, case_sensitive);
            if (a_element == NULL) {
                return false;
            }

            if (!JCompare(b_element, a_element, case_sensitive)) {
                return false;
            }
        }

        return true;
    }

    default:
        return false;
    }
}
