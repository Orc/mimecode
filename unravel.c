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
/*
 * unravel: picks apart a mimed article.
 */
static char sccsid[] = "%Z%%M% %I% %E%";

#include "config.h"

#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#ifdef OS_FREEBSD
#   include <stdlib.h>
#else
#   include <malloc.h>
#endif

#include <string.h>
#include <basis/options.h>

FILE* openfile(char*, char*);

#include "mime_encoding.h"

/* various encoder/decoder objects, written in C */
Encoder base64,
	quoted_printable,
	uuencode,
	clear;

static Encoder *coders[] = {
    &base64,
    &quoted_printable,
    &uuencode
};
#define NR_CODERS	(sizeof coders/sizeof coders[0])


struct interesting_headers {
    int headers_are_valid;
    char *mime_version;
    char *content_type;
    char *content_transfer_encoding;
    char *content_disposition;
} ;


struct boundary_stack {
    char *boundary;
    int size;
    int level;
} ;

static struct boundary_stack *stack;

static int nrbound = 0;
static int topbound = 0;

static char line[10240];

static int verbose = 0;		/* spit out needed debugging */
static int writeoutput = 0;	/* write all valid fragments */
static int sevenbit = 0;	/* force filenames to usascii */

#define Match(s,cst_p)	(strncasecmp(s, cst_p, sizeof(cst_p)-1) == 0)
#define Cstrdup(s, cst_p) (Match(s, cst_p) ? strdup(skipwhite(s+sizeof(cst_p)-1)) : 0)

/*
 * xfgetline() gets a line and strips carriage control
 */
char *
xfgetline(char *bfr, int len, FILE *fd)
{
    char *ret = fgets(bfr, len, fd);
    int size;

    if (ret) {
	size = strlen(bfr);
	if (size > 0 && bfr[size-1] == '\n') {
	    --size;
	    if (size > 0 && bfr[size-1] == '\r')
		--size;
	}
	bfr[size] = 0;
    }
    return ret;
} /* xfgetline */


/*
 * pushboundary() pushes a boundary onto the boundary stack
 */
static void
pushboundary(char* boundary, int level)
{
    if (topbound == nrbound) {
	nrbound = (nrbound+1) * 2;
	stack = realloc(stack, nrbound * sizeof stack[0]);
    }
    stack[topbound].boundary = strdup(boundary);
    stack[topbound].size  = strlen(boundary);
    stack[topbound].level = level;
    topbound++;
} /* pushboundary */


/*
 * popboundary() pops the top element off the boundary stack
 */
static void
popboundary()
{
    if (topbound > 0) {
	--topbound;
	free(stack[topbound].boundary);
    }
} /* popboundary */


/*
 * checkbound() checks to see if a line can be found in the boundary stack
 */
static int
checkbound(char* candidate)
{
    int sz;

    if (topbound <= 0 || candidate[0] != '-' || candidate[1] != '-')
	return EOF;

    sz = stack[topbound-1].size;

    if (strncmp(candidate+2, stack[topbound-1].boundary, sz) == 0) {
	/* check for --boundary--, which mean the end of this level.
	 */
	if (candidate[2+sz] == '-' && candidate[3+sz] == '-' && candidate[4+sz] == 0)
	    popboundary();
    
	return (topbound>0) ? stack[topbound-1].level : 0;
    }
    return EOF;
} /* checkbound */


/*
 * skipwhite() slips over leading whitespace in a string
 */
static char *
skipwhite(char *str)
{
    while (isspace(*str))
	++str;
    return str;
} /* skipwhite */


/*
 * get_header() reads a single header line
 */
static int
get_header(char* bfr, int bfrlen, FILE* input)
{
    char* current;
    int sizeleft;
    register c;
    char* p;
    int checkedforsep=0;

    current = bfr;
    sizeleft = bfrlen;

    while (xfgetline(current, sizeleft, input) != 0) {

	for (p=current; *p; ++p)
	    sizeleft--;
	*p = 0;
	current = p;

	c = fgetc(input);

	if (c != ' ' && c != '\t') {
	    ungetc(c, input);
	    break;
	}
    }
    return (current == bfr) ? 0 : 1;
} /* get_header */


/*
 * read_headers() reads header lines, returning a pointer to an array of
 * interesting headers (Mime-version, Content-Type, Content-Transfer-Encoding)
 */
static int
read_headers(FILE* input, struct interesting_headers* info)
{
    int rc;
    register c;
    long pos;
    int gotheaders=0;
    char *p;

    pos = ftell(input);
    while (get_header(line, sizeof line, input)) {

	/* If we run into a line that doesn't have a :, we stop right
	 * here and back off to the beginning of it.
	 */
	if (strchr(line, ':') == 0) {
	    fflush(input);
	    fseek(input, pos, SEEK_SET);
	    fflush(input);
	    break;
	}
	else
	    pos = ftell(input);
	gotheaders++;

	if ((rc = checkbound(line)) >= 0)
	    return rc;

	if (p = Cstrdup(line, "Mime-version:"))
	    info->mime_version = p;
	else if (p = Cstrdup(line, "Content-Type:"))
	    info->content_type = p;
	else if (p = Cstrdup(line, "Content-Transfer-Encoding:"))
	    info->content_transfer_encoding = p;
	else if (p = Cstrdup(line, "Content-Disposition:"))
	    info->content_disposition = p;
    }
    if (gotheaders)
	info->headers_are_valid = 1;
    return EOF;
} /* read_headers */


/*
 * getstring() picks up a string or a white-space delimited token.
 */
static char*
getstring(char* line)
{
    static char boundary[200];
    char *p, *q;

    if ((p = strchr(line, '=')) == 0)
	return 0;
    p++;
    q = boundary;

    if (*p == '"') {
	while (*++p != '"')
	    *q++ = *p;
    }
    else while (*p && !isspace(*p))
	*q++ = *p++;
    *q = 0;
    return boundary;
} /* getstring */


/*
 * fragment() picks out ;-delimited fragments from a mime header line.
 * this is basically strtok(), except that we ignore ; inside strings.
 */
static char*
fragment(char* string)
{
    static char* ptr = 0;
    char* q;
    char* retval;

    if (string)
	ptr = string;

    if (ptr == 0 || *ptr == 0)
	return 0;

    for (q = ptr; *q; ++q)
	if (*q == ';') {	/* break on all `;'s */
	    *q++ = 0;
	    retval = ptr;
	    ptr = q;
	    return retval;
	}
	else if (*q == '"') {	/* that aren't in a string */
	    ++q;
	    while (*q && *q != '"')
		++q;
	}

    /* walked off the end of the string; return the entire string
     * and set the string pointer to null
     */
    retval = ptr;
    ptr = 0;
    return retval;
} /* fragment */


/*
 * read_section() reads lines until we find a boundary or EOF
 */
static int
read_section(FILE* input, Encoder *translator, char* filename)
{
    int rc = EOF;
    int linecount = 0;
    void *f;

    FILE *output = filename ? openfile(filename, line) : 0;

    if (filename)
	fprintf(stderr, "%s ", filename);
    if (output && (filename ==0 || strcmp(filename, line) != 0)) {
	if (filename)
	    fprintf(stderr, "-> ");
	fprintf(stderr,"%s ", line);
    }

    f = (*translator->open) (output, MIME_DECODE);

    while (xfgetline(line, sizeof line, input) != 0) {
	rc = checkbound(line);

	if (rc != EOF)
	    break;
	linecount++;
	(*translator->put) (line, sizeof line, f);
    }
    (*translator->close) (f);
    if (output) {
	fclose(output);
	fprintf(stderr, "[%d line%s]\n", linecount, (linecount==1)?"":"s");
    }
    return rc;
} /* read_section */


/*
 * eat_section() gobbles up and throws away the junk at the end of a mimed message
 */
static int
eat_section(FILE* input)
{
    int rc = EOF;

    while (xfgetline(line, sizeof line, input) != 0) {
	if ((rc = checkbound(line)) != EOF)
	    break;
    }
    return rc;
} /* eat_section */


/*
 * get_translator() checks our known translators to see if any of them
 * match the content-transfer-encoding of this item.
 */
static Encoder *
get_translator(char* content_transfer_encoding)
{
    char *p;
    int ix;

    if (content_transfer_encoding) {
	p = skipwhite(content_transfer_encoding);

	for (ix = 0; ix < NR_CODERS; ix++)
	    if (strcasecmp(p, coders[ix]->encoding) == 0)
		return coders[ix];
    }

    return &clear;
} /* get_translator */


/*
 * fixfilename() rewrites a filename so that it doesn't include directory
 *               separators, backslashes, or ``:''
 */
char *
fixfilename(char *fn)
{
    char *from, *to;

    for (to = from = fn; *from; ++from) {
	if (sevenbit)
	    *from &= 0x7f;
	if (*from == ':' || *from == '/' || *from == '\\')
	    continue;
	if (to != from)
	    *to = *from;
	++to;
    }
    if (to != from)
	*to = 0;
    return fn;
} /* fixfilename */


/*
 * read_mime() pulls apart a mimed letter
 */
static int
read_mime(FILE* input, int level)
{
    struct interesting_headers headers;
    int rc;
    char *p;
    char *boundary = 0;
    char *filename = 0;
    Encoder* ofn = &clear;

    memset(&headers, 0, sizeof headers);
    rc = read_headers(input, &headers);

    if (rc != EOF)		/* found a boundary while we were reading the header */
	return rc;

    if (feof(input))
	return EOF;

    ofn = get_translator(headers.content_transfer_encoding);


    if ((level || headers.mime_version) && headers.content_type) {

	for (p = fragment(headers.content_type); p; p = fragment(0)) {
	    p = skipwhite(p);
	    /* possible bug:  getstring() returns a string from a static array,
	     * and subsequent calls to it will overwrite the original value.
	     * If we have a multipart message that's got both a name and a
	     * value, One Of Them Will Fail(tm) and unravel will screw up
	     * the unpacking in a most horrible fashion.
	     */
	    if (Match(p, "name=") || Match(p, "filename="))
		filename = fixfilename(getstring(p));
	    else if (Match(p, "boundary="))
		boundary = getstring(p);
	}
	if (filename == 0)
	    for (p = fragment(headers.content_disposition); p; p = fragment(0)){
		p = skipwhite(p);
		if (Match(p, "name=") || Match(p, "filename="))
		    filename = fixfilename(getstring(p));
	    }

	if ((filename == 0) && writeoutput)  {
	    char template[20];
	    strcpy(template, "part.XXXXXX");
	    if ( (filename = mktemp(template)) == 0)
		fprintf(stderr, "can't create output file: %s\n", strerror(errno));
	}

	if (verbose) {
	    fprintf(stderr, "filename is     [%s]\n"
			    "boundary is     [%s]\n"
			    "content_type is [%s]\n",
			    filename, boundary ? boundary : "-undefined-",
			    headers.content_type);
	}

	/* If it's a multipart message we'll pick it apart,
	 * otherwise we'll just fling it through and scan it appropriately
	 *
	 * multipart/mixed is supposed to be different than multipart/digest.
	 * (the default for /mixed is text/plain, while /digest defaults to
	 * message/rfc822.)   However, in practice it looks like all /mixed
	 * attachments from common generators will have content-types, so
	 * we'll treat all the parts, no matter what the source, as 
	 * message/rfc822, and let god sort out the just.
	 */

	if (Match(headers.content_type, "multipart/")) {
	    /* multipart message:  we eat the header section, then
	     * process --boundary separated segments until we either
	     * run out of file or hit a --boundary--.  If we hit a
	     * --boundary--, we discard the next section we find (the
	     * boundary will have already been popped off the boundary
	     * stack by checkbound()
	     */

	    if (boundary) {
		pushboundary(boundary, level);

		for (rc = eat_section(input); rc == level; rc = read_mime(input, 1+level))
		    ;
		if (rc != EOF)
		    rc = eat_section(input);

		return rc;
	    }
	}
	else if (Match(headers.content_type, "message/rfc822"))
	    return read_mime(input, level);

	/* other types that we might be interested in:
	 *	image/	-- we should simply ignore these.
	 *	text/	-- possibly uuencoded.
	 */
    }
    if (ofn == &clear && level == 1 && filename == 0) {
	/* Not mime.  Maybe it's uuencoded? */
	uud(input);
	return EOF;
    }

    return read_section(input, ofn, filename);
} /* read_mime */


/*
 * uud: picks apart a single-part uuencoded message, ignoring headers
 *      and separators and everything.  This is backwards compatability
 *      to replace the uudecode program.
 */
void
uud(FILE *input)
{
    int rc = EOF;
    int linecount = 0;
    MFILE f;
    char *file;

    if ( (f = (uuencode.open) (isatty(1) ? 0 : stdout, MIME_DECODE)) == 0)
	return;
	
    while (xfgetline(line, sizeof line, input) != 0) {
	if ( (rc = (uuencode.put)(line, strlen(line), f)) < 0)
	    break;
	else if (rc == 1)
	    linecount++;
    }
    if (isatty(1)) {
	if ( (file = (uuencode.filename)(f)) != 0)
	    fprintf(stderr, "%s ", file);
	fprintf(stderr, "[%d line%s]\n", linecount, (linecount==1)?"":"s");
    }
    (uuencode.close) (f);
} /* uud */


struct x_option options[] = {
    { '7', '7', "7bit",    0, "Force filenames to 7 bit ascii" },
    { 'w', 'w', "write",   0, "Always write document fragments to files" },
    { 'h', 'h', "help",    0, "Show this message" },
    { 'v', 'v', "verbose", 0, "Display progress messages" },
    { 'V', 'V', "version", 0, "Show the version number, then exit" }
};
#define NROPTIONS (sizeof options/sizeof options[0])


main(int argc, char **argv)
{
    int opt;
    char *pgm;

    x_opterr = 1;

    while ((opt = x_getopt(argc, argv, NROPTIONS, options)) != EOF) {
	switch (opt) {
	case '7':
		sevenbit++;
		break;
	case 'v':
		verbose++;
		break;
	case 'w':
		writeoutput++;
		break;
	case 'V':
		puts("unravel " VERSION);
		exit(0);
	default:
		fprintf(stderr, "\nusage: unravel [options] [file]\n\n");
		showopts(stderr, NROPTIONS, options);
		exit (opt == 'h' ? 0 : 1);
	}
    }

    if ( (pgm = strrchr(argv[0], '/')) == 0)
	pgm = argv[0];
    else
	pgm++;

    if (argc <= x_optind) {
	if (isatty(0)) {
	    fprintf(stderr, "unravel: cannot read input from a tty\n");
	    exit(1);
	}
	setlinebuf(stdin);
    }
    else if (freopen(argv[x_optind], "r", stdin) == 0) {
	perror(argv[x_optind]);
	exit(1);
    }
    if (strcasecmp(pgm, "uudecode") == 0)
	uud(stdin);
    else {
	stack = malloc(1);
	read_mime(stdin, 1);
    }
    exit(0);
}
