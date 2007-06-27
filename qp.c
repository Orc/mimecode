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
 * qp: encoder and decoder for quoted-printable items
 */

#include "config.h"

#ifdef OS_FREEBSD
#   include <stdlib.h>
#else
#   include <malloc.h>
#endif
#include <errno.h>

#include "mime_encoding.h"

static char
QPenc[] = {
	  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	  0,'!','"','#','$','%','&','\'','(',')','*','+',',','-','.','/',
	'0','1','2','3','4','5','6','7','8','9',':',';','<',  0,'>','?',
	'@','A','B','C','D','E','F','G','H','I','J','K','L','M','N','O',
	'P','Q','R','S','T','U','V','W','X','Y','Z','[','\\',']','^','_',
	'`','a','b','c','d','e','f','g','h','i','j','k','l','m','n','o',
	'p','q','r','s','t','u','v','w','x','y','z','{','|','}','~',  0,
};

#define QP_MAGIC	0xDEADBEEF

struct qprec {
    int magic;		/* must be QP_MAGIC */
    struct mime_encoding* functions;
    mime_open_mode mode;/* open for encoding or decoding? */
    FILE* stream;	/* where the output lives */
} ;


/*
 * qphex() prints a byte out in a qp-format UPPERCASE hexidecimal
 * format
 */
static void
qphex(FILE* output, unsigned char c)
{
    static char xchar[]	= "0123456789ABCDEF";

    fputc('=', output);
    fputc(xchar[(c & 0xF0) >> 4], output);
    fputc(xchar[ c & 0x0F], output);
} /* qphex */


/*
 * encode a document into quoted-printable form
 */
static int
encoder(unsigned char* line, int size, struct qprec* fd)
{
    int xp = 0;		/* we keep track of the X position, because
			 * we can't let lines get longer than 76
			 * characters
			 */
    unsigned char *p;
    int val;
    register c;
    int lastwasspace = 0;
    int haseol = 0;

    p = line+strlen(line);

    /* take off any trailing \r\n */
    if (p > line && p[-1] == '\n') {
	haseol = 1;
	*--p = 0;
	if (p > line && p[-1] == '\r')
	    *--p = 0;
    }

    while (*line) {
	c = *line++;

	/* if the character is <= ' ', > '~', or '=', we want to
	 * print it out in =XX format, unless it's a \n, where
	 * we dump a RFC822 \r\n, or unless it's a tab or space,
	 * where we display it as is, except if it's at end of
	 * line we need to dump a soft linebreak to keep from
	 * having a bare whitespace at the end of the line
	 * (RFC1541 5.1, rule 3).  If the line ever gets over
	 * 70 characters long, we need to drop a soft linebreak
	 * and start a new line (RFC1541 5.1, rule 5).  If none
	 * of the above is true, dump the ascii value of the
	 * character and continue on our way.
	 */

	if (c & 0x80 || (xp == 0 && c == '.') ) {
	    qphex(fd->stream, c);
	    xp += 3;
	}
	else if (c == '\n') {
	    xp = 0;
	}
	else if ((val = QPenc[c]) == 0 && c != '\t' && c != ' ') {
	    qphex(fd->stream, c);
	    xp += 3;
	}
	else {
	    fputc(c, fd->stream);
	    if (c == '\t')
		xp = (1+(xp/8))*8;
	    else
		xp++;
	}
	if (xp > 70) {			/* split long lines */
	    fputc('=', fd->stream);
	    fputc('\n', fd->stream);
	    xp = 0;
	    lastwasspace = 0;
	}
	else
	    lastwasspace = (c == '\t' || c == ' ');
    } /* while (*line) */

    if (lastwasspace) {
	/* RFC1541 5.1, rule 3: don't allow whitespace at the
	 * end of an encoded line
	 */
	fputc('=', fd->stream);
	fputc('\n', fd->stream);
    }
    if (haseol)
	fputc('\n', fd->stream);
    return 0;
} /* encoder */


/*
 * decode a quoted-printable item.  In the grand scheme of things this
 * is pretty trivial;  we just replace CR/LF with LF and convert =XX
 * back into the character it used to be before it got beat up by the
 * evil daemons of creeping standardisation.
 */
static int
decoder(unsigned char* line, int size, struct qprec* fd)
{
    register c;
    int val;
    char bfr[3];
    int lastwascr=0;

    while ((c = *line++) != 0) {

	if (c == '=') {				/* an escape sequence? */
	    if ((c = *line++) == 0)		/* soft linebreak? */
		return 0;
	    else {				/* or some qp encoded thing */
		bfr[0] = c;
		if (*line) {
		    bfr[1] = *line++;
		    bfr[2] = 0;

		    sscanf(bfr, "%x", &val);
		    fputc(val, fd->stream);
		}
		/* else we simply drop it one the floor with a plop */
	    }
	}
	else if ((c & 0x80) == 0 && (QPenc[c] == c || c == ' ' || c == '\t'))
	    fputc(c, fd->stream);
    }
    fputc('\n', fd->stream);
    return 0;
} /* decoder */



/*
 * qpopen() a quoted_printable encoder or decoder
 */
struct qprec *
qpopen(FILE* stream, mime_open_mode mode)
{
    struct qprec* tmp;

    if (stream == 0) {
	errno = EINVAL;
	return 0;
    }
    if (mode != MIME_ENCODE && mode != MIME_DECODE) {
	errno = ENOSYS;
	return 0;
    }
    if ((tmp = malloc(sizeof tmp[0])) == 0)
	return 0;

    tmp->mode = mode;
    tmp->stream = stream;
    tmp->magic = QP_MAGIC;
} /* qpopen */


/*
 * qpput() writes a line into the encryption engine
 */
static int
qpput(unsigned char* line, int size, struct qprec* fd)
{
    if (fd == 0 || fd->magic != QP_MAGIC) {
	errno = EINVAL;
	return EOF;
    }
    switch (fd->mode) {
    case MIME_ENCODE:	return encoder(line, size, fd);
    case MIME_DECODE:	return decoder(line, size, fd);
    default:		errno = ENOSYS;
			return EOF;
    }
} /* qpput */


/*
 * qpclose() does what you'd expect
 */
static int
qpclose(struct qprec* fd)
{
    if (fd == 0 || fd->magic != QP_MAGIC) {
	errno = EINVAL;
	return EOF;
    }
    fflush(fd->stream);
    fd->magic = ~1;
    free(fd);
    return 0;
} /* qpclose */


struct mime_encoding quoted_printable = {
    "quoted-printable",
    qpopen,
    qpput,
    qpclose
};


#ifdef TEST
main()
{
    char line[80];
    void *p;

    p = qpopen(stdout, MIME_DECODE);

    while (gets(line)) {
	qpput(line, p);
    }
    qpclose(p);
}
#endif
