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

static const char copyright[] = "@(#) Copyright (c) 1983, 1993\nThe Regents of the University of California.All rights reserved. \n";

static const char sccsid[] = "@(#)uudecode.c	8.2 (Berkeley) 4/2/94";

/*
 * mime-encoding for `content-transfer-encoding: X-uue' files.
 */

#include "config.h"

#include <stdio.h>
#include <errno.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>

#include "mime_encoding.h"


#define Write(ctx,ch)	if ( (*write)(ctx,ch) == -1) return -1

/* ENC is the basic 1 character encoding function to make a char printing */
#define	ENC(c) ((c) ? ((c) & 077) + ' ': '`')

static int
encode(mimeread read, mimewrite write, void *ctx)
{
    register int ch, n;
    register char *p;
    char buf[80];

    while ( (n = (*read)(ctx, buf, 45)) > 0 ) {
	ch = ENC(n);
	Write(ctx,ch);
	for (p = buf; n > 0; n -= 3, p += 3) {
	    ch = *p >> 2;
	    Write(ctx, ENC(ch));
	    ch = (*p << 4) & 060 | (p[1] >> 4) & 017;
	    Write(ctx, ENC(ch));
	    ch = (p[1] << 2) & 074 | (p[2] >> 6) & 03;
	    Write(ctx, ENC(ch));
	    ch = p[2] & 077;
	    Write(ctx, ENC(ch));
	}
	Write(ctx,'\n');
    }
    Write(ctx, ENC('\0'));
    Write(ctx, '\n');
}


static int
decode(mimeread read, mimewrite write, void *ctx)
{
#define	DEC(c)	(((c) - ' ') & 077)	/* single character decode */

    register int n;
    register char ch, *p;
    char text[512];
    int size;
    int count = 0;

    while ( (size = (*read)(ctx, text, sizeof text)) > 0 ) {

	if (strcasecmp(text, "end\n") == 0 || strcasecmp(text, "end\r\n") == 0)
	    return 1;

	/*
	 * `n' is used to avoid writing out all the characters
	 * at the end of the file.
	 */
	if ((n = DEC(*text)) <= 0)
	    continue;
	for (p = 1+text; n > 0; p += 4, n -= 3) {
	    if (n >= 3) {
		ch = DEC(p[0]) << 2 | DEC(p[1]) >> 4;
		Write(ctx,ch);
		ch = DEC(p[1]) << 4 | DEC(p[2]) >> 2;
		Write(ctx,ch);
		ch = DEC(p[2]) << 6 | DEC(p[3]);
		Write(ctx,ch);
	    }
	    else {
		if (n >= 1) {
		    ch = DEC(p[0]) << 2 | DEC(p[1]) >> 4;
		    Write(ctx,ch);
		}
		if (n >= 2) {
		    ch = DEC(p[1]) << 4 | DEC(p[2]) >> 2;
		    Write(ctx,ch);
		}
		if (n >= 3) {
		    ch = DEC(p[2]) << 6 | DEC(p[3]);
		    Write(ctx,ch);
		}
	    }
	}
    }
    return 0;
} /* decode */

struct mime_encoding uuencode = { "text/uuencode", encode, decode };
