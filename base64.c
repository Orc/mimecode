/*
 * x64: base64 document encoder and decoder.
 */

#include "config.h"
#include "mime_encoding.h"
#include <errno.h>

/*
 * We run the encoder and decoder off this pair of tables (plus a little
 * bit of glue to clock sextets/bytes on and off the streams that we're
 * processing.)
 */
static char
table64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static char
X64dec[] = {
	-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
	-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,
	52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-1,-1,-1,-1, 0, 1, 2, 3, 4, 5, 6,
	 7, 8, 9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
	-1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,
	49,50,51,-1,-1,-1,-1,
};

#define Write(ctx,c)	if ((*write)(ctx,c) == -1) return -1;

/*
 * encoder() encodes a document into base64
 */
static int
encode( mimeread read, mimewrite write, void *context )
{
    int size,
	i,
	xp = 0,
	res = 0;
    char text[512];

    while ( (size = (*read)(context, text+res, sizeof text-res)) > 0 ) {
	size += res;
	i = 0;
	while (size > 2) {
	    Write(context, table64[63 &  (text[i] >> 2)                    ]);
	    Write(context, table64[63 & ((text[i] << 4) | (text[i+1] >> 4)) ]);
	    Write(context, table64[63 & ((text[i+1] << 2) | (text[i+2] >> 6)) ]);
	    Write(context, table64[63 &   text[i+2]                          ]);
	    i += 3;
	    size -= 3;
	    if (++xp > 18) {
		Write(context, '\n');
		xp = 0;
	    }
	}
	for (res=0; res < size; res++)
	    text[res] = text[i++];
    }
    if (res) {
	if (xp > 18)
	    Write(context, '\n');
	Write(context,table64[63 &  (text[0] >> 2)                    ]);
	Write(context,table64[63 & ((text[0] << 4) | (text[1] >> 4)) ]);
	Write(context,(res > 1 ) ? table64[63 & (text[1] << 2)] : '=');
	Write(context,'=');
	xp = 1;
    }
    if (xp) Write(context, '\n');
    return 0;
} /* encode */


/*
 * decode() decodes a base64 document
 */
static int
decode( mimeread read, mimewrite write, void *context)
{
    unsigned char line[512];
    register unsigned char *code;
    register int size;
    unsigned char c[4];

    while ( (size = (*read)(context, (void*)line, sizeof line)) > 0 ) {
	code = line;

	while ( (size >= 4) && (code[0] != '=') ) {
	    c[0] = X64dec[code[0]];
	    c[1] = X64dec[code[1]];
	    c[2] = X64dec[code[2]];
	    c[3] = X64dec[code[3]];

	    Write(context, (c[0]<<2) | (c[1]>>4) );
	    if (code[2] == '=')
		break;
	    Write(context, (c[1]<<4) | (c[2]>>2) );
	    if (code[3] == '=')
		break;

	    Write(context, (c[2]<<6) | (c[3]) );

	    code += 4;
	    size -= 4;
	}
    }
    return 1;
} /* decode */



/* decoder/encoder table entry for base64
 */
struct mime_encoding base64 = {
    "base64",
    encode,
    decode
} ;


#ifdef TEST

int
b64fread(context *ctx, char *block, int size)
{
    return fread(block, 1, size, ctx->input);
}

int
b64putc(context *ctx, char ch)
{
    return fputc(ch, ctx->output);
}

int
b64gets(context *ctx, char *line, int size)
{
    return fgets(line,size,ctx->input) ? strlen(line) : -1;
}


main(argc, argv)
char **argv;
{
    context context = { stdin, stdout };


    if ( (argc > 1) && (strcasecmp(argv[1], "encode") == 0) )
	encode(b64fread,b64putc,&context);
    else
	decode(b64gets,b64putc,&context);
}
#endif
