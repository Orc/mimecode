/*
 * unravel: picks apart a mimed article.
 */
#include "config.h"

#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <unistd.h>
#if HAVE_LIBGEN_H
#include <libgen.h>
#endif
#include <errno.h>
#include <stdlib.h>
#include <stdarg.h>

#include <string.h>
#include <basis/options.h>
#include <sys/types.h>
#include <sys/stat.h>

extern int overwrite;
FILE* openfile(char*, char*, char*);

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
    char *mark;
    int size;
} ;

static struct boundary_stack *stack;

static int nrbound = 0;
static int sp = 0;

static int verbose = 0;		/* spit out needed debugging */
static int traceonly = 0;	/* describe the message, but don't save */
static int save_them_all = 0;	/* write all valid fragments */
static int sevenbit = 0;	/* force filenames to usascii */
static int clip = 0;		/* strip paths off output filenames */

static char *outputfile = 0;	/* fixed output file for uudecode */
static char *prefix = "part";	/* name for -a files */

char *pgm;
extern char version[];

#define Match(s,cst_p)	(strncasecmp(s, cst_p, sizeof(cst_p)-1) == 0)
#define Cstrdup(s, cst_p) (Match(s, cst_p) ? strdup(skipwhite(s+sizeof(cst_p)-1)) : 0)


static void
error(char *fmt, ...)
{
    va_list ptr;

    va_start(ptr, fmt);
    fprintf(stderr, "%s: ", pgm);
    vfprintf(stderr, fmt, ptr);
    fputc('\n', stdout);
    va_end(ptr);
    exit(1);
}


#if !HAVE_BASENAME
char *
basename(char *p)
{
    char *r = strrchr(p, '/');

    return r ? (1+r) : p;
}
#endif


/*
 * xfgetline() gets a line and strips carriage control
 */
char *
xfgetline(char *bfr, int len, FILE *fd)
{
    char *ret;
    int size;

    if (( ret = fgets(bfr, len, fd) )) {
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
pushboundary(char* boundary)
{
    sp++;
    if (sp == nrbound) {
	nrbound = (nrbound+1) * 2;
	stack = realloc(stack, nrbound * sizeof stack[0]);
    }
    stack[sp].mark = strdup(boundary);
    stack[sp].size  = strlen(boundary);
} /* pushboundary */


/*
 * popboundary() pops the top element off the boundary stack
 */
static void
popboundary()
{
    if (sp) {
	free(stack[sp].mark);
	--sp;
    }
} /* popboundary */


/*
 * checkbound() checks to see if a line can be found in the boundary stack
 */
static int
checkbound(char* line)
{
    if (sp <= 0 || line[0] != '-' || line[1] != '-')
	return 0;

    if (strncmp(line+2, stack[sp].mark, stack[sp].size) == 0) {
	register int sz = stack[sp].size;
	
	/* check for --boundary--, which mean the end of this level.
	 */

	if (line[2+sz] == '-' && line[3+sz] == '-' && line[4+sz] == 0)
	    popboundary();
	
	return 1;
    }
    return 0;
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
    register int c;
    char* p;

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
    long pos;
    int gotheaders=0;
    char *p;
    char line[1024];

    pos = ftell(input);
    while (get_header(line, sizeof line, input)) {

	/* If we run into a line that doesn't have a :, we stop right
	 * here and back off to the beginning of it.
	 */
	if (strchr(line, ':') == 0) {
	    fseek(input, pos, SEEK_SET);
	    break;
	}
	else
	    pos = ftell(input);
	gotheaders++;

	if (checkbound(line))
	    return 0;

	if (( p = Cstrdup(line, "Mime-version:") ))
	    info->mime_version = p;
	else if (( p = Cstrdup(line, "Content-Type:") ))
	    info->content_type = p;
	else if (( p = Cstrdup(line, "Content-Transfer-Encoding:") ))
	    info->content_transfer_encoding = p;
	else if (( p = Cstrdup(line, "Content-Disposition:") ))
	    info->content_disposition = p;
    }
    if (gotheaders)
	info->headers_are_valid = 1;
    return 1;
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



static int
readline(context *io, char *text, int size)
{
    if (xfgetline(text, size, io->input) != 0) {
	if (checkbound(text))
	    return 0;
	strcat(text, "\n");
	return strlen(text);
    }
    return 0;
}


static int
writechar(context *io, char ch)
{
    if (ch == '\n')
	io->linecount++;
    io->charcount++;
    return io->output ? fputc(ch,io->output) : 1;
}


static void
namefile(context *io, char *wanted, char *actual)
{
    if (wanted && !traceonly)
	fprintf(stderr, "%s ", wanted);

    if (io->output && actual && (wanted == 0 || strcmp(wanted, actual) != 0) ) {
	if ( !traceonly ) {
	    if (wanted)
		fprintf(stderr, "-> ");
	    fprintf(stderr,"%s ", actual);
	}
    }
}


/*
 * read_section() reads lines until we find a boundary or EOF
 */
static void
read_section(FILE* input, Encoder *code, char* filename, int mode)
{
    context io = { NULL, NULL };
    char *actualname = 0;

    actualname = alloca((filename ? strlen(filename) : strlen(prefix)) + 40);
    actualname[0] = 0;

    io.input = input;
    io.output = traceonly ? 0 : openfile(filename, prefix, actualname);

    namefile(&io,filename,actualname);

    if ( (*code->decode)((mimeread)readline, (mimewrite)writechar, &io) < 0 ) {
	perror("decode");
	return;
    }
    
    if (io.output) {
	if (mode) fchmod(fileno(io.output), mode);
	fclose(io.output);
	fprintf(stderr, "[%d line%s]\n", io.linecount, (io.linecount==1)?"":"s");
    }
    else if ( traceonly )
	printf("; %d line%s, %d char%s\n",
		 io.linecount, (io.linecount==1)?"":"s",
		 io.charcount, (io.charcount==1)?"":"s");
} /* read_section */


/*
 * eat_section() gobbles up and throws away the junk at the end of a mimed message
 */
static void
eat_section(FILE* input)
{
    char text[512];

    while (xfgetline(text, sizeof text, input) != 0 && !checkbound(text))
	;
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
 * fixfilename() tweaks filenames to optionally strip to 7bit ascii (-7)
 *               and to chop the path off (-c).
 */
char *
fixfilename(char *path)
{
    char *from, *to;

    for (to = path, from = clip ? basename(path) : path; *from; ++from) {
	if (sevenbit)
	    *from &= 0x7f;
	if (to != from)
	    *to = *from;
	++to;
    }
    if (to != from)
	*to = 0;
    return path;
} /* fixfilename */


/*
 * uud: picks apart a single-part uuencoded message, ignoring headers
 *      and separators and everything.  This is backwards compatability
 *      to replace the uudecode program.
 */
void
uud(FILE *input)
{
    char line[1024];
    char *p, *fi, *filename;
    unsigned int mode = 0644;
    Encoder *code;

#define LIKE(l,p)	( strncasecmp(l,p,(sizeof p) - 1) == 0 )

    while (xfgetline(line, sizeof line, input))
	if (LIKE(line, "begin ") || LIKE(line, "begin-base64 ")) {
	    code = (line[5] == '-') ? &base64
				    : &uuencode;

	    for (fi = line; *fi && !isspace(*fi); ++fi)
		;
	    mode = (unsigned int)strtol(fi, &p, 8);

	    if (p == fi) error("badly formed uuencode ``begin'' line");

	    mode &= 0777;	/* mask off unwanted mode bits */

	    while (isspace(*p)) ++p;
	    if (*p == '"') {
		filename = ++p;
		if (( p = strchr(p, '"') ))
		    *p = 0;
		else  error("badly formed uuencode ``begin'' line");
	    }
	    else filename = p;

	    if (*filename == 0) error("badly formed uuencode ``begin'' line");

	    if ( traceonly ) {
		printf("%*suuencoded", sp, "");
		if ( outputfile )
		    printf("; \"%s\"", outputfile);
		/*putchar('\n');*/
	    }

	    read_section(input, code, outputfile ? outputfile
						 : fixfilename(filename), mode);
	}
} /* uud */


/*
 * read_mime() pulls apart a mimed letter
 */
static void
read_mime(FILE* input)
{
    struct interesting_headers headers;
    char *p;
    char *boundary = 0;
    char *filename = 0;
    Encoder* ofn = &clear;

    memset(&headers, 0, sizeof headers);

    if (read_headers(input, &headers) == 0) return;

    ofn = get_translator(headers.content_transfer_encoding);

    if ((save_them_all || sp || headers.mime_version) && headers.content_type) {
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

	if (verbose) {
	    fprintf(stderr, "filename is     [%s]\n"
			    "boundary is     [%s]\n"
			    "content_type is [%s]\n",
			    filename ? filename : "-undefined-",
			    boundary ? boundary : "-undefined-",
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


	    if ( traceonly )
		printf("%*s%s\n", sp, "", headers.content_type);
		
	    if (boundary) {
		int level;

		pushboundary(boundary);
		level = sp;

		eat_section(input);
		while (sp == level)
		    read_mime(input);

		if (sp != level)
		    eat_section(input);
		return;
	    }
	}
	else if (Match(headers.content_type, "message/rfc822"))
	    return read_mime(input);

	/* other types that we might be interested in:
	 *	image/	-- we should simply ignore these.
	 *	text/	-- possibly uuencoded.
	 */
    }
    if (ofn == &clear && sp == 0 && filename == 0) {
	/* Not mime.  Maybe it's uuencoded? */
	uud(input);
	return;
    }

    if ( traceonly ) {
	printf("%*s%s", sp, "", headers.content_type);
	if ( filename )
	    printf("; \"%s\"", filename);
	/*putchar('\n');*/
    }

    return (filename||save_them_all) ? read_section(input, ofn, filename, 0)
				     : eat_section(input);
} /* read_mime */


struct x_option ropts[] = {
    { '7', '7', "7bit",     0, "Force filenames to 7 bit ascii" },
    { 'a', 'a', "all",      0, "Write all document fragments to files" },
    { 'c', 'c', "current",  0, "extract attachments to the current directory"},
    { 'f', 'f', "overwrite",0, "Overwrite existing files" },
    { 'h', 'h', "help",     0, "Show this message" },
    { 'p', 'p', "prefix","PFX","set document prefix to PFX" },
    { 't', 't', "traceonly",0, "show the document types, but don't save" },
    { 'v', 'v', "verbose",  0, "Display progress messages" },
    { 'V', 'V', "version",  0, "Show the version number, then exit" }
};

struct x_option uopts[] = {
    { '7', '7', "7bit",     0, "Force filenames to 7 bit ascii" },
    { 'c', 'c', "current",  0, "write the decoded file to the current directory"},
    { 'f', 'f', "overwrite",0, "Overwrite existing files" },
    { 'o', 'o', "output","FILE","Write output to FILE" },
    { 'v', 'v', "verbose",  0, "Display progress messages" },
    { 'V', 'V', "version",  0, "Show the version number, then exit" }
};
#define SIZE(opt) (sizeof opt/sizeof opt[0])


static void
die(int exitcode, int nropts, struct x_option *opts)
{
#if HAVE_SETBUFFER
    char iob[1024];
    setbuffer(stderr, iob, sizeof iob);
#endif
    fprintf(stderr, "\nusage: %s [options] [file]\n\n", pgm);
    showopts(stderr, nropts, opts);
#if HAVE_SETBUFFER
    fflush(stderr);
    setbuf(stderr, 0);
#endif
    exit(exitcode);
}

void
main(int argc, char **argv)
{
    int opt, uudecode = 0;
    struct x_option *opts;
    int nropts;

    x_opterr = 1;

    pgm = basename(argv[0]);

    if (strcasecmp(pgm, "uudecode") == 0)
	uudecode = 1;

    if (uudecode) {
	opts = uopts;
	nropts = SIZE(uopts);
    }
    else {
	opts = ropts;
	nropts = SIZE(ropts);
    }

    while ((opt = x_getopt(argc, argv, nropts, opts)) != EOF) {
	switch (opt) {
	case '7':
		sevenbit++;
		break;
	case 'c':
		clip++;
		break;
	case 'f':
		overwrite++;
		break;
	case 'v':
		verbose++;
		break;
	case 'u':
		uudecode++;
		break;
	case 'a':
		save_them_all++;
		break;
	case 'o':
		outputfile = x_optarg;
		break;
	case 't':
		save_them_all++;
		traceonly++;
		break;
	case 'p':
		prefix = x_optarg;
		break;
	case 'V':
		printf("%s %s\n", pgm, version);
		exit(0);
	default:
		die( (opt == 'h') ? 0 : 1, nropts, opts );
	}
    }

    if (argc <= x_optind) {
	if (isatty(0)) {
	    error("cannot read input from a tty\n");
	    exit(1);
	}
	setlinebuf(stdin);
    }
    else if (freopen(argv[x_optind], "r", stdin) == 0) {
	perror(argv[x_optind]);
	exit(1);
    }
    if (uudecode)
	uud(stdin);
    else {
	stack = malloc(1);
	read_mime(stdin);
    }
    exit(0);
}
