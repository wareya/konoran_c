#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

typedef struct _Token {
    struct _Token * prev;
    struct _Token * next;
    char * text;
    size_t len;
    size_t line;
    size_t column;
    int form;
} Token;

uint8_t token_is_exact(Token * token, char * text)
{
    if (!token) return 0;
    size_t len = strlen(text);
    if (token->len != len) return 0;
    return strncmp(token->text, text, len) == 0;
}

Token * add_token(Token * prev)
{
    Token * newtoken = (Token *)malloc(sizeof(Token));
    memset(newtoken, 0, sizeof(Token));
    if (prev)
    {
        prev->next = newtoken;
        newtoken->prev = prev;
    }
    return newtoken;
}

uint8_t is_digit(char c)
{
    return (uint8_t)(c - '0') <= 9;
}

uint8_t is_hex_digit(char c)
{
    return (uint8_t)(c - '0') < 10
        || (uint8_t)(c - 'A') < 6
        || (uint8_t)(c - 'a') < 6;
}

uint8_t is_name_start_char(char c)
{
    return c == '_'
        || (uint8_t)(c - 'A') < 26
        || (uint8_t)(c - 'a') < 26;
}
uint8_t is_name_char(char c)
{
    return is_name_start_char(c) || is_digit(c);
}

size_t check_float_token(char * source)
{
    char * source_orig = source;
    size_t left_digits = 0;
    while (is_digit(*source))
        left_digits++, source++;
    if (*source != '.')
        return 0;
    source++;
    size_t right_digits = 0;
    while (is_digit(*source))
        right_digits++, source++;
    if ((right_digits == 0 && left_digits == 0) || *source == 0)
        return 0;
    
    if (*source == 'e' || *source == 'E')
    {
        // scientific suffix: e or E, + or - or neither, one or more digits
        source++;
        if (*source == '+' || *source == '-')
            source++;
        if (!is_digit(*source))
            return 0;
        while (is_digit(*source))
            source++;
    }
    
    if (*source++ != 'f')
        return 0;
    char c1 = *source++;
    if (!c1)
        return 0;
    char c2 = *source++;
    if ((c1 == '3' && c2 == '2') ||
        (c1 == '6' && c2 == '4'))
        return (size_t)(source - source_orig);
    return 0;
}

size_t check_int_token(char * source, uint8_t * is_untyped)
{
    char * source_orig = source;
    uint8_t has_minus = 0;
    if (*source == '-')
        source++, has_minus = 1;
    size_t digits = 0;
    while (is_digit(*source))
        digits++, source++;
    if (digits == 0 || *source == 0)
        return 0;
    
    char c0 = *source++;
    if (c0 != 'i' && c0 != 'u')
    {
        if (!has_minus)
        {
            *is_untyped = 1;
            return (size_t)(source - source_orig) - 1;
        }
        return 0;
    }
    char c1 = *source++;
    if (!c1)
        return 0;
    if (c1 == '8')
        return (size_t)(source - source_orig);
    char c2 = *source++;
    if ((c1 == '1' && c2 == '6') ||
        (c1 == '3' && c2 == '2') ||
        (c1 == '6' && c2 == '4'))
        return (size_t)(source - source_orig);
    return 0;
}

size_t check_hex_int_token(char * source, uint8_t * is_untyped)
{
    char * source_orig = source;
    uint8_t has_minus = 0;
    if (*source == '-')
        source++, has_minus = 1;
    if (*source++ != '0')
        return 0;
    if (*source++ != 'x')
        return 0;
    size_t digits = 0;
    while (is_hex_digit(*source))
        digits++, source++;
    if (digits == 0 || *source == 0)
        return 0;
    
    char c0 = *source++;
    if (c0 != 'i' && c0 != 'u')
    {
        if (!has_minus)
        {
            *is_untyped = 1;
            return (size_t)(source - source_orig) - 1;
        }
        return 0;
    }
    char c1 = *source++;
    if (!c1)
        return 0;
    if (c1 == '8')
        return (size_t)(source - source_orig);
    char c2 = *source++;
    if ((c1 == '1' && c2 == '6') ||
        (c1 == '3' && c2 == '2') ||
        (c1 == '6' && c2 == '4'))
        return (size_t)(source - source_orig);
    return 0;
}

size_t check_string(char * source)
{
    char * source_orig = source;
    if (*source++ != '"')
        return 0;
    while (*source && *source != '"' && *source != '\n' && *source != '\r')
    {
        if (*source == '\\')
        {
            source++; // step over backslash
            // backslash-null and backslash-newline are invalid
            // as in, LITERAL null or newline. not n or r character.
            if (*source == 0 || *source == '\n' || *source == '\r')
                return 0;
        }
        source++; // step over character
    }
    if (*source++ != '"')
        return 0;
    if (strncmp(source, "array_nonull", 12) == 0)
        return (size_t)(source - source_orig) + 12;
    if (strncmp(source, "array", 5) == 0)
        return (size_t)(source - source_orig) + 5;
    if (strncmp(source, "nonull", 6) == 0)
        return (size_t)(source - source_orig) + 6;
    return (size_t)(source - source_orig);
}

size_t check_char(char * source)
{
    char * source_orig = source;
    if (*source++ != '\'')
        return 0;
    char c = *source++;
    if (c == '\'' || c == '\r' || c == '\n' || c == 0)
        return 0;
    if (c == '\\')
    {
        char c = *source++;
        // only these four characters are allowed after a \ in a char
        if (c != '\\' && c != 'n' && c != 'r' && c != 't')
            return 0;
    }
    if ((uint8_t)c >= 0xF0)
    {
        if ((uint8_t)*source > 0xBF || (uint8_t)*source++ < 0x80) return 0;
        if ((uint8_t)*source > 0xBF || (uint8_t)*source++ < 0x80) return 0;
        if ((uint8_t)*source > 0xBF || (uint8_t)*source++ < 0x80) return 0;
    }
    else if ((uint8_t)c >= 0xE0)
    {
        if ((uint8_t)*source > 0xBF || (uint8_t)*source++ < 0x80) return 0;
        if ((uint8_t)*source > 0xBF || (uint8_t)*source++ < 0x80) return 0;
    }
    else if ((uint8_t)c >= 0xC0)
    {
        if ((uint8_t)*source > 0xBF || (uint8_t)*source++ < 0x80) return 0;
    }
    else if ((uint8_t)c >= 0x80)
        return 0;
    
    if (*source++ != '\'')
        return 0;
    if (strncmp(source, "u32", 3) == 0)
        return (size_t)(source - source_orig) + 3;
    return (size_t)(source - source_orig);
}

size_t check_name(char * source)
{
    char * source_orig = source;
    if (!is_name_start_char(*source++))
        return 0;
    while (is_name_char(*source))
        source++;
    return (size_t)(source - source_orig);
}

char * symbols[] = {
    "&&", "||", "==", "!=", ">=", "<=", "<<", ">>",
    "(", ")", ",", "?", ":", "[", "]", "{", "}",
    "!", "-", "+", "~", "*", "&", "@", "|", "^", ">", "<", "/", "%",
    ".", "=", ";",
};

size_t check_symbol(char * source)
{
    size_t count = sizeof(symbols) / sizeof(symbols[0]);
    for (size_t i = 0; i < count; i++)
    {
        size_t len = strlen(symbols[i]);
        if (strncmp(source, symbols[i], len) == 0)
            return len;
    }
    return 0;
}

enum {
    TOKEN_FORM_NONE = 0,
    TOKEN_FORM_FLOAT,
    TOKEN_FORM_INT,
    TOKEN_FORM_INTTOKEN,
    TOKEN_FORM_STRING,
    TOKEN_FORM_CHAR,
    TOKEN_FORM_NAME,
    TOKEN_FORM_SIGIL,
};

size_t check_token(char * source, int * form)
{
    size_t ret = 0;
    if ((ret = check_float_token(source)))
    {
        *form = TOKEN_FORM_FLOAT;
        return ret;
    }
    uint8_t is_suffixless = 0;
    if ((ret = check_hex_int_token(source, &is_suffixless)))
    {
        *form = TOKEN_FORM_INT + is_suffixless;
        return ret;
    }
    is_suffixless = 0;
    if ((ret = check_int_token(source, &is_suffixless)))
    {
        *form = TOKEN_FORM_INT + is_suffixless;
        return ret;
    }
    if ((ret = check_string(source)))
    {
        *form = TOKEN_FORM_STRING;
        return ret;
    }
    if ((ret = check_char(source)))
    {
        *form = TOKEN_FORM_CHAR;
        return ret;
    }
    if ((ret = check_name(source)))
    {
        *form = TOKEN_FORM_NAME;
        return ret;
    }
    if ((ret = check_symbol(source)))
    {
        *form = TOKEN_FORM_SIGIL;
        return ret;
    }
    return 0;
}

uint8_t is_whitespace(char c)
{
    return c == ' ' || c == '\n' || c == '\r' || c == '\t' || c == '\0';
}

void free_tokens_from_end(Token * token)
{
    while (token)
    {
        Token * tofree = token;
        token = token->prev;
        free(tofree);
    }
}
void free_tokens_from_front(Token * token)
{
    while (token)
    {
        Token * tofree = token;
        token = token->next;
        free(tofree);
    }
}

void print_token(Token * token)
{
    if (!token)
        return;
    for (size_t i = 0; i < token->len; i++)
        printf("%c", token->text[i]);
    puts("");
}

Token * tokenize(char * source, Token ** failed_last)
{
    size_t sourcelen = 0;
    char * src2 = source;
    while (*(src2++))
        sourcelen++;
    
    Token * last = 0;
    
    size_t lines = 1;
    size_t columns = 1;
    
    while (*source)
    {
        if (*source == '#'
         || strncmp(source, "//", 2) == 0)
        {
            while (*source != '\n' && *source != '\r' && *source != 0)
            {
                source++;
                columns++;
            }
            continue;
        }
        if (strncmp(source, "/*", 2) == 0)
        {
            source++;
            columns++;
            do
            {
                source++;
                columns++;
                
                if (*source == '\n')
                {
                    lines++;
                    columns = 1;
                }
            } while (strncmp(source, "*/", 2) != 0 && *source != 0);
            for (int i = 0; i < 2; i++)
            {
                source++;
                columns++;
                if (*source == '\n')
                {
                    lines++;
                    columns = 1;
                }
            }
            continue;
        }
        if (is_whitespace(*source))
        {
            columns++;
            if (*source == '\n')
            {
                lines++;
                columns = 1;
            }
            source++;
            continue;
        }
        int form = 0;
        size_t len = check_token(source, &form);
        if (len == 0)
        {
            *failed_last = last;
            break;
        }
        last = add_token(last);
        last->text = source;
        last->len = len;
        last->form = form;
        last->line = lines;
        last->column = columns;
        source += len;
        columns += len;
    }
    
    Token * first = last;
    while (first && first->prev)
        first = first->prev;
    return first;
}
