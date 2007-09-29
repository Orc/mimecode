/*
 * qp: encoder and decoder for quoted-printable items
 */

#include "config.h"

#include <stdlib.h>
#include <errno.h>

#include "mime_encoding.h"

static char
QPenc[] = {
	  0,  0,  0,  0,  0,  0,  0,  0,'\t', 0,'\n', 0,  0,  0,  0,  0,
	  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	' ' ,'!','"','#','$','%','&','\'','(',')','*','+',',','-','.','/',
	'0','1','2','3','4','5','6','7','8','9',':',';','<',  0,'>','?',
	'@','A','B','C','D','E','F','G','H','I','J','K','L','M','N','O',
	'P','Q','R','S','T','U','V','W','X','Y','Z','[','\\',']','^','_',
	'`','a','b','c','d','e','f','g','h','i','j','k','l','m','n','o',
	'p','q','r','s','t','u','v','w','x','y','z','{','|','}','~',  0,
};

#define Write(ctx,c)	if ((*write)(ctx,c) == EOF) return EOF


/* two entry points:
 *    qp->encode(readline(), writeline(), context);
 *    qp->decode(readline(), writeline(), context);
 */

/*
 * encode a document into quoted-printable form
 */
static int
encode( mimeread read, mimewrite write, void *context)
{
    char *q;
    char block[512];
    static char xchar[]	= "0123456789ABCDEF";
    int size;
    int xp = 0;		/* we keep track of the X position, because
			 * we can't let lines get longer than 76
			 * characters
			 */
    unsigned char *p;
    register c, i;

    while ( ( size = (*read)(context, block, sizeof block) ) > 0 ) {
	for (i = 0; i < size; i++) {
	    c = block[i];

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

	     if ( (c & 0x80) || ((xp == 0) && (c == '.')) || (QPenc[c] == 0) ) {
		Write(context, '=');
		Write(context, xchar[(c & 0xF0) >> 4]);
		Write(context, xchar[ c & 0x0F ]);
		xp += 3;
	     }
	     else {
		Write(context, c);
		if (c == '\t')
		    xp += (1+(xp/8))*8;
		else
		    xp ++;
	    }

	    if (c == '\n')
		xp = 0;
	    else if (xp > 70) {
		Write(context, '=');
		Write(context, '\n');
		xp = 0;
	    }
	}
    }
#if 0
    if (lastwasspace) {
	/* RFC1541 5.1, rule 3: don't allow whitespace at the
	 * end of an encoded line
	 */
	fputc('=', fd->stream);
	fputc('\n', fd->stream);
    }
    if (haseol)
	fputc('\n', fd->stream);
#endif
    return 0;
} /* encoder */


/*
 * decode a quoted-printable item.  In the grand scheme of things this
 * is pretty trivial;  we just replace CR/LF with LF and convert =XX
 * back into the character it used to be before it got beat up by the
 * evil daemons of creeping standardisation.
 */
static int
decode( mimeread read, mimewrite write, void *context)
{
    register c;
    char bfr[3];
    int lastwascr=0;
    char line[512];
    int val;
    register i, size;

    while ( (size = read(context,line,sizeof line)) > 0) {
	for (i=0; i < size; i++) {
	    c = line[i];
	    if (c == '=') {			/* an escape sequence? */
		if (i < size-2) {		/* enough room for an escape? */
		    bfr[0] = line[++i];
		    bfr[1] = line[++i];
		    bfr[2] = 0;

		    sscanf(bfr, "%x", &val);
		    Write(context,val);
		}
		/* else we just silently drop it */

	    }
	    else
		Write(context, c);
	}
    }
    return 0;
} /* decoder */



struct mime_encoding quoted_printable = {
    "quoted-printable",
    encode,
    decode,
};


#ifdef TEST

int
qpread(context *ctx, char *block, int size)
{
    return fread(block, 1, size, ctx->input);
}


int
qpwrite(context *ctx, char ch)
{
    return fputc(ch, ctx->output);
}



main()
{
    char line[80];
    void *p;
    context context = { stdin, stdout };

    decode(qpread, qpwrite, (void*)&context);
}
#endif
