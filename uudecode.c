/*-
 * Copyright (c) 1983, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

static const char copyright[] = "@(#) Copyright (c) 1983, 1993\n
The Regents of the University of California.All rights reserved. \n";

static const char sccsid[] = "@(#)uudecode.c	8.2 (Berkeley) 4/2/94";
static const char rcsid[] = "$Id$";

/*
 * mime-encoding for `content-transfer-encoding: X-uue' files.
 */

#include "config.h"

#include <stdio.h>
#include <errno.h>
#include <ctype.h>
#include <string.h>
#ifdef OS_FREEBSD
#   include <stdlib.h>
#else
#   include <malloc.h>
#endif

#include "mime_encoding.h"

struct mime_encoding uuencode;


#define U_MAGIC	0x0DDBA11

struct urec {
    long magic;
    struct mime_encoding* functions;
    enum {NEEDBEGIN, DECODING, FINISHED} state;
    FILE* stream;
    int privateopen;
    int filemode;		/* for privateopen */
    char *filename;		/* for privateopen */
} ;

/*
 * uudecode() handles a line from a uuencoded file.  It only decodes one thing per
 * open/close session -- if we want to handle multiple uuencoded items in a file,
 * we need to have a way to pass this information up to the calling process.
 */
static int
uudecode(unsigned char* line, struct urec* fd)
{
#define	DEC(c)	(((c) - ' ') & 077)	/* single character decode */

    register int n;
    register char ch, *p;

    switch (fd->state) {
    case NEEDBEGIN:
	    if (strncmp(line, "begin ", 6) == 0) {
		int mode = 0644;
		

		/* if a stream wasn't specified in the open, we need
		 * to initialize it when we encounter the begin line.
		 */
		if (fd->privateopen) {
		    if (sscanf(line, "begin %o", &mode) < 1) {
			errno = EINVAL;
			return EOF;
		    }
		    p = line+6/*sizeof "begin "*/;
		    while (*p && !isspace(*p))
			++p;
		    while (*p && isspace(*p))
			++p;

		    strtok(p, "\r\n");
		    fd->filename = malloc(strlen(p) + 20);
		    if (strlen(p) == 0 || (fd->stream = openfile(p, fd->filename)) == 0)
			return EOF;
		}
		fd->state = DECODING;
		fd->filemode = mode;
	    }
	    break;
    case DECODING:
	/* trim \r\n off the end of the string */
	for (p=line; *p != '\r' && *p != '\n' && *p; ++p)
	    ;
	*p = 0;

	if (strcmp(line, "end") == 0) {
	    fd->state = FINISHED;
	    break;
	}
	/*
	 * `n' is used to avoid writing out all the characters
	 * at the end of the file.
	 */
	if ((n = DEC(*line)) <= 0)
	    break;
	for (++line; n > 0; line += 4, n -= 3)
	    if (n >= 3) {
		ch = DEC(line[0]) << 2 | DEC(line[1]) >> 4;
		fputc(ch, fd->stream);
		ch = DEC(line[1]) << 4 | DEC(line[2]) >> 2;
		fputc(ch, fd->stream);
		ch = DEC(line[2]) << 6 | DEC(line[3]);
		fputc(ch, fd->stream);
	    }
	    else {
		if (n >= 1) {
		    ch = DEC(line[0]) << 2 | DEC(line[1]) >> 4;
		    fputc(ch, fd->stream);
		}
		if (n >= 2) {
		    ch = DEC(line[1]) << 4 | DEC(line[2]) >> 2;
		    fputc(ch, fd->stream);
		}
		if (n >= 3) {
		    ch = DEC(line[2]) << 6 | DEC(line[3]);
		    fputc(ch, fd->stream);
		}
	    }
	    break;
    default:
	    break;
    }
    return 1;
} /* uudecode */


/*
 * uopen() opens a uucoder stream
 */
static struct urec*
uopen(FILE* stream, mime_open_mode mode)
{
    struct urec* tmp;

    if (mode != MIME_DECODE) {
	errno = ENOSYS;
	return 0;
    }

    if ((tmp = malloc(sizeof *tmp)) == 0)
	return 0;

    tmp->magic = U_MAGIC;
    tmp->stream = stream;
    tmp->state = NEEDBEGIN;
    tmp->privateopen = stream ? 0 : 1;
    tmp->filemode = 0644;	/* ignored if not privateopen */
    tmp->filename = 0;

    return tmp;
} /* uopen */


/*
 * uput() writes a string to the coder
 */
static int
uput(unsigned char* line, int linesize, struct urec* fd)
{
    return uudecode(line, fd);
} /* uput */


/*
 * uclose() shuts down the coder
 */
static int
uclose(struct urec* fd)
{
    if (fd) {
	fflush(fd->stream);
	if (fd->privateopen) {
	    fchmod(fileno(fd->stream), fd->filemode);
	    fclose(fd->stream);
	    if (fd->filename)
		free(fd->filename);
	}
	
	fd->magic = ~1;
	free(fd);
	return 0;
    }
    else {
	errno = EINVAL;
	return EOF;
    }
} /* uclose */


/*
 * ufilename() returns the file associated with this uuencoded object
 */
static char *
ufilename(struct urec* fd)
{
    return fd->filename;
}


/*
 * coder operations block-- the only thing that's exported from this
 * module
 */
struct mime_encoding uuencode = {
    "X-uue",		/* Content-Transfer-Encoding: */
    uopen,		/* open the coder */
    uput,		/* write a block to the coder */
    uclose,		/* close the coder */
    ufilename		/* print the name of the file we're spitting out */
} ;
