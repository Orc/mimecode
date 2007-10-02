/*
 * mime_encoding: public table for encoder/decoder functions for
 * mime encodings
 */
#ifndef _MIME_ENCODING_D
#define _MIME_ENCODING_D

#include <stdio.h>

typedef int (*mimeread)(void*,char*,int);
typedef int (*mimewrite)(void*,char);
typedef int (*mimeclosehook)(void*,char*);


typedef struct {
    FILE *input;
    FILE *output;
    int linecount;
} context;

typedef struct mime_encoding {
    char *encoding;			/* encoding type: "base64", etc */
    int (*encode)(mimeread, mimewrite, void*);
    int (*decode)(mimeread, mimewrite, void*);
} Encoder;

#endif/*_MIME_ENCODING_D*/
