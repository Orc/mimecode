/*
 *   Copyright (c) 1998 David Parsons. All rights reserved.
 *   
 *   Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following conditions
 *   are met:
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in
 *     the documentation and/or other materials provided with the
 *     distribution.
 *  3. All advertising materials mentioning features or use of this
 *     software must display the following acknowledgement:
 *     
 *   This product includes software developed by David Parsons
 *   (orc@pell.chi.il.us)
 *
 *  4. My name may not be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *     
 *  THIS SOFTWARE IS PROVIDED BY DAVID PARSONS ``AS IS'' AND ANY
 *  EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 *  THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 *  PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL DAVID
 *  PARSONS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 *  EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 *  TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 *  ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 *  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 *  IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 *  THE POSSIBILITY OF SUCH DAMAGE.
 */
static const char rcsid[] = "$Id$";
/*
 * x64: base64 document encoder and decoder.
 */

#include "config.h"

#include <errno.h>
#ifdef OS_FREEBSD
#   include <stdlib.h>
#else
#   include <malloc.h>
#endif

#include "mime_encoding.h"

/*
 * We run the encoder and decoder off this pair of tables (plus a little
 * bit of glue to clock sextets/bytes on and off the streams that we're
 * processing.)
 */
static char
X64enc[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static char
X64dec[] = {
	-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
	-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,
	52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-1,-1,-1,-1, 0, 1, 2, 3, 4, 5, 6,
	 7, 8, 9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
	-1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,
	49,50,51,-1,-1,-1,-1,
};

extern struct mime_encoding base64;

#define X64_MAGIC	0x1F2E3D4C

struct base64rec {
    long int magic;	/* must be X64_MAGIC */
    struct mime_encoding* functions;
			/* open, put, close, though we don't care about open */
    mime_open_mode mode;/* are we encoding or decoding? */
    FILE *stream;	/* where to write fd->stream/decoded information */

    unsigned long bfr;	/* encode/decode buffer */
    int queued;		/* how many characters have been pushed */
			/* into the buffer? */
    int xp;		/* ENCODE: line fitting */
    int pad;		/* DECODE: = characters encountered */
};

/*
 * encoder() encodes a document into base64
 */
static int
encoder(unsigned char* line, int size, struct base64rec* fd)
{
    /* nibble up the input in 3-byte chunks, spitting out 4 sextets
     * and linefeeds as required
     */
    while (size-- > 0) {
	fd->bfr <<= 8;
	fd->bfr |= *line++;
	if ((fd->queued)++ >= 2) {
	    /* filled the queue;  spit the fd->stream text to the
	     * outside world
	     */
	    if (fd->xp > 70) {
		fputc('\n', fd->stream);
		fd->xp = 0;
	    }
	    fputc(X64enc[(unsigned int)((fd->bfr>>18)&63)], fd->stream);
	    fputc(X64enc[(unsigned int)((fd->bfr>>12)&63)], fd->stream);
	    fputc(X64enc[(unsigned int)((fd->bfr>>6)&63)], fd->stream);
	    fputc(X64enc[(unsigned int)((fd->bfr)&63)], fd->stream);
	    fd->xp += 4;
	    fd->bfr = 0;
	    fd->queued = 0;
	}
    }
    return 0;
} /* encoder */


/*
 * decoder() decodes a base64 document
 */
static int
decoder(unsigned char* line, int size, struct base64rec* fd)
{
    int val;
    unsigned char c;

    while (*line) {
	c = 0x7f & *line++;
	if (c == '=') {
	    fd->queued++;
	    fd->pad++;
	}
	else if ((val = X64dec[c]) < 0)
	    continue;
	else
	    fd->queued++;

	fd->bfr <<= 6;
	fd->bfr |= (63 & (unsigned int)val);

	if (fd->queued >= 4) {
	    switch (fd->pad) {
	    case 0:
		    fputc(((fd->bfr)>>16) & 0xff, fd->stream);
		    fputc(((fd->bfr)>>8) & 0xff, fd->stream);
		    fputc((fd->bfr) & 0xff, fd->stream);
		    break;
	    case 1:
		    fputc(((fd->bfr)>>8) & 0xff, fd->stream);
		    fputc((fd->bfr) & 0xff, fd->stream);
		    break;
	    case 2:
		    fputc((fd->bfr) & 0xff, fd->stream);
		    break;
	    }
	    fd->bfr = fd->queued = fd->pad = 0;
	}
    }
    return 0;
} /* decoder */



/*
 * x64open() builds a base64rec to pass back to the caller and sets
 * up for either encoding or decoding
 */
static struct base64rec*
x64open(FILE* destination, mime_open_mode mode)
{
    struct base64rec *tmp;

    if (mode != MIME_ENCODE && mode != MIME_DECODE) {
	errno = ENOSYS;
	return 0;
    }
    if ((tmp = malloc(sizeof tmp[0])) == 0)
	return tmp;
    memset(tmp, 0, sizeof tmp[0]);

    tmp->magic = X64_MAGIC;
    tmp->stream = destination;
    tmp->mode = mode;
    tmp->functions = &base64;

    return tmp;
} /* x64open */


/*
 * x64put() is the wrapper that calls the encoder or decoder on a line of
 * text.
 */
static int
x64put(char* line, int size, struct base64rec* fd)
{
    if (fd == 0 || fd->magic != X64_MAGIC) {
	errno = EINVAL;
	return EOF;
    }
    switch (fd->mode) {
    case MIME_ENCODE:
	    return encoder(line,size,fd);
    case MIME_DECODE:
	    return decoder(line,size,fd);
    default:
	    errno = ENOSYS;
	    return EOF;
    }
} /* put */


/*
 * x64close() flushes buffers, syncs the stream, and disposes of
 * all the resources that were allocated for this bas64struct
 */
static int
x64close(struct base64rec *fd)
{
    if (fd == 0 || fd->magic != X64_MAGIC)
	return 0;

    /* first flush the stream */

    if (fd->mode == MIME_ENCODE) {
	switch (fd->queued) {
	case 0:
	    if (fd->xp > 0)
		fputc('\n', fd->stream);
	    break;
	case 1:
	    if (fd->xp > 70)
		fputc('\n', fd->stream);
	    fputc(X64enc[(unsigned int)(((fd->bfr)>6)&63)], fd->stream);
	    fputc(X64enc[(unsigned int)((fd->bfr)&63)], fd->stream);
	    fputc('=', fd->stream);
	    fputc('=', fd->stream);
	    fputc('\n', fd->stream);
	    break;
	case 2:
	    if (fd->xp > 70)
		fputc('\n', fd->stream);
	    fputc(X64enc[(unsigned int)(((fd->bfr)>>12)&63)], fd->stream);
	    fputc(X64enc[(unsigned int)(((fd->bfr)>6)&63)], fd->stream);
	    fputc(X64enc[(unsigned int)((fd->bfr)&63)], fd->stream);
	    fputc('=', fd->stream);
	    fputc('\n', fd->stream);
	}
    }

    fflush(fd->stream);
    fd->magic = ~1;
    free(fd);

    return 0;
} /* close */


/* decoder/encoder table entry for base64
 */
struct mime_encoding base64 = {
    "base64",
    x64open,
    x64put,
    x64close,
    0
} ;


#ifdef TEST
main()
{
    void *p;
    char line[200];


    p = x64open(stdout, MIME_DECODE);

    if (p == 0)
	perror("one");

    while (fgets(line, sizeof line, stdin) != 0)
	if (x64put(line, sizeof line, p) != 0)
	    perror("two");

    x64close(p);
}
#endif
