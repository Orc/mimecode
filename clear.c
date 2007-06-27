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
 * cleartext `translation' of mimed objects.
 */

#include "config.h"

#include <errno.h>
#ifdef OS_FREEBSD
#   include <stdlib.h>
#else
#   include <malloc.h>
#endif

#include "mime_encoding.h"

#define C_MAGIC 0x002C44C2

struct crec {
    long magic;
    struct mime_encoding* functions;
    mime_open_mode mode;
    FILE *stream;
};

extern struct mime_encoding clear;

/*
 * copen() returns a magic token saying we've opened this encoder.
 */
static struct crec*
copen(FILE* stream, mime_open_mode mode)
{
    struct crec* tmp;

    if (mode != MIME_ENCODE && mode != MIME_DECODE) {
	errno = EINVAL;
	return 0;
    }

    if ((tmp = malloc(sizeof tmp[0])) == 0)
	return 0;

    tmp->magic = C_MAGIC;
    tmp->stream = stream;
    tmp->mode = mode;
    tmp->functions = &clear;

    return tmp;
} /* copen */


/*
 * cput() writes a string out to the output
 */
static int
cput(char* line, int size, struct crec* fd)
{
    if (fd == 0 || fd->magic != C_MAGIC) {
	errno = EINVAL;
	return;
    }
    fputs(line, fd->stream);
    fputc('\n', fd->stream);
}


/*
 * cclose() closes up shop
 */
static int
cclose(struct crec* fd)
{
    fflush(fd->stream);
    fd->magic = ~1;
    free(fd);
    return 0;
}


struct mime_encoding clear = {
    "8bit",
    copen,
    cput,
    cclose,
    0
};
