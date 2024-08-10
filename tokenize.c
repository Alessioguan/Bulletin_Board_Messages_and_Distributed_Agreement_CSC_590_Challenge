/*
 * In-place string tokenizer, by Stefan Bruda.  Read the comments in
 * the header before using it.
 */

#include "tokenize.h"

size_t str_tokenize(char* str, char** tokens, const size_t n) {
    size_t tok_size = 1;
    tokens[0] = str;

    size_t i = 0;
    while (i < n) {
        if (str[i] == ' ') {
            str[i] = '\0';
            i++;
            for (; i < n && str[i] == ' '; i++)
                /* NOP */;
            if (i < n) {
                tokens[tok_size] = str + i;
                tok_size++;
            }
        }
        else
            i++;
    }

    return tok_size;
}

size_t str_tokenize_slash(char* str, char** tokens, const size_t n) {
    size_t tok_size = 1;
    tokens[0] = str;
    int slash_identify = 0;

    size_t i = 0;
    while (i < n) {
        if (str[i] == '/') {
            slash_identify = 1;
            str[i] = '\0';
            i++;
            for (; i < n && str[i] == ' '; i++)
                /* NOP */;
            if (i < n) {
                tokens[tok_size] = str + i;
                tok_size++;
            }
        }
        else
            i++;
    }

    if(slash_identify)
        return tok_size;
    else
        return -1;
}