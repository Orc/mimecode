/*
 * ravel: builds a mime file from a list of input files
 */
#include "config.h"

#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include "mime_encoding.h"
#include <basis/options.h>

#include <sys/utsname.h>
#include <time.h>

#include <paths.h>

extern Encoder base64, uuencode, quoted_printable;

struct x_option uuencopts[] = {
    { 'm', 'm', "base64", 0, "use base64 encoding" },
    { 'o', 'o', "output", "FILE", "write encoded output to FILE" },
    { 'V', 'V', "version", 0, "Show the current version, then exit" },
} ;


struct x_option ravelopts[] = {
    { '6', '6', "base64", 0, "encode all attachments in base64" },
    { 's', 's', "subject", "SUBJECT", "Set the subject for this message" },
    { 'm', 'm', "message-id", "MESSAGE-ID", "Use this message-id instead of\n"
						 "a randomly generated one." },
    { 'b', 'b', "boundary", "BOUNDARY", "Set the boundary between parts of the mime\n"
					     "message" },
    { 't', 't', "mail-to", "ADDRESS", "Mail the output directly to address\n" },
    { 'f', 'f', "from",    "ADDRESS", "if -t, say the mail is from address\n" },
    { 'p', 'p', "preamble", "TEXT", "Use this text as the preamble to the message\n"
				    "instead of the conventional `this is a MIME\n"
				    "file...' one." },
    { 'o', 'o', "output", "FILE", "Write output to FILE" },
    { 'h', 'h', "help", 0, "print this help message" },
    { 'v', 'v', "verbose", 0, "be somewhat chatty" },
    { 'V', 'V', "version", 0, "Show the current version, then exit" },
} ;

#define SIZ(a)	(sizeof a / sizeof a[0])


extern char version[];

char *pgm = "unravel";
struct x_option *opts;
int nropts;

void
die(int code)
{
    fprintf(stderr, "\nusage: %s [options] file [file...]\n\n", pgm);
    showopts(stderr, nropts, opts);
    exit (code);
}


static int
readblock(void *ctx, char *block, int size)
{
    context *io = ctx;
    
    return fread(block, 1, size, io->input);
}


static int
writechar(void *ctx, char ch)
{
    context *io = ctx;
    
    return fputc(ch, io->output);
}


void
douuencode(context *io, int base64please, int argc, char **argv)
{
    struct stat sb;
    int mode = 644;
    char *name = (argc > 1) ? argv[1] : argv[0];
    Encoder *code = base64please ? &base64 : &uuencode;

    if ((io->input = fopen(argv[0], "r")) != 0) {
	if (fstat(fileno(io->input), &sb) != -1)
	    mode = sb.st_mode & 0777;

	fprintf(io->output, "begin%s %o %s\n",
		    base64please ? "-base64" : "", mode, name);
	(*code->encode)((mimeread)readblock, (mimewrite)writechar, io);
	fprintf(io->output, base64please ? "====\n" : "end\n");
	fclose(io->output);
	exit(0);
    }
    perror(argv[0]);
    exit(1);
}


main(int argc, char **argv)
{
    int ix;
    char block0[512];
    int j, sz;
    int istextIhope;
    context io = { NULL, NULL };
    int opt;
    struct utsname sys;
    time_t now;
    char *messageid = 0,
	 *boundary = 0,
	 *subject = 0,
	 *preamble = 0,
	 *from = 0;
    char **to = (char**)malloc(1);
    int  nrto = 0;
    int  tomail = 0;
    int  all64 = 0;
    char *oflag = 0;
    int  verbose = 0;
    int  uue = 0;
    int  base64please = 0;

#if HAVE_BASENAME
    pgm = basename(argv[0]);
#else
    if (pgm = strrchr(argv[0], '/'))
	++pgm;
    else
	pgm = argv[0];
#endif

    if (strcasecmp(pgm, "uuencode") == 0) {
	uue = 1;
	opts = uuencopts;
	nropts = SIZ(uuencopts);
    }
    else {
	opts = ravelopts;
	nropts = SIZ(ravelopts);
    }

    x_opterr = 1;
    while ((opt = x_getopt(argc, argv, nropts, opts)) != EOF) {
	switch (opt) {
	case '6':
	    all64 = 1;
	    break;
	case 'f':
	    from = x_optarg;
	    break;
	case 's':
	    subject = x_optarg;
	    break;
	case 'm':
	    if (uue)
		base64please = 1;
	    else
		messageid = x_optarg;
	    break;
	case 'b':
	    boundary = x_optarg;
	    break;
	case 'p':
	    preamble = x_optarg;
	    break;
	case 'o':
	    if (oflag) {
		fprintf(stderr, "%s: it's meaningless to use two -o's\n", pgm);
		exit(1);
	    }
	    oflag = strdup(x_optarg);
	    break;
	case 't':
	    to = realloc(to, (1+nrto)*sizeof to[0]);
	    to[nrto] = strdup(x_optarg);
	    nrto++;
	    break;
	case 'v':
	    verbose = 1;
	    break;
	case 'V':
	    printf("%s %s\n", pgm, version);
	    exit(0);
	default:
	case 'h':
	    die( opt == 'h' ? 0 : 1);
	}
    }
    if (argc == x_optind)
	die(1);

    if ( oflag && (nrto > 0) ) {
	fprintf(stderr, "%s: can't specify both -o and -t\n", pgm);
	exit(1);
    }

    io.output = stdout;
    if (oflag && ((io.output = fopen(oflag, "w")) == 0) ) {
	perror(oflag);
	exit(1);
    }

    if (uue) douuencode(&io, base64please, argc-x_optind, argv+x_optind);

    if (boundary == 0)
	boundary = "Hello, Sailor";
    if (subject == 0)
	subject = "MIME-encoded files";
    if (messageid == 0) {
	static char template[200];

	time(&now);
	uname(&sys);
	sprintf(template, "%u.%u/%u/%s.", getuid(), getgid(), now, sys.nodename);
#if HAS_DOMAINNAME
	if (strlen(sys.domainname) > 0 && strcmp(sys.domainname, "(none)") != 0)
	    strcat(template, sys.domainname);
#endif

	messageid = template;
    }

    if (nrto > 0) {
	if ((io.output = popen(_PATH_SENDMAIL " -U -t", "w")) == 0) {
	    perror("sendmail");
	    exit(1);
	}
	tomail = 1;
    }
    else
	io.output = stdout;

    fprintf(io.output,
	    "Message-ID: <%s>\n"
	   "Subject: %s\n"
           "Mime-Version: 1.0\n"
           "Content-Type: multipart/mixed; boundary=\"%s\"\n",
           messageid, subject, boundary);
    if (from && *from)
	fprintf(io.output, "From: %s\n", from);

    if (to)
	for (ix = 0; ix < nrto; ix++)
	    fprintf(io.output, "To: %s\n", to[ix]);
    writechar(&io,'\n');

    if (preamble)
	fprintf(io.output, "%s\n", preamble);
    else
	fprintf(io.output,
	       "This is a MIME-encoded message.  Decode it with `unravel' or\n"
	       "any other MIME-unpacking software.  Unravel is available at\n"
	       "http://www.pell.chi.il.us/~orc/Code/mimecode\n");

    for (ix = x_optind; ix < argc; ix++) {
	if (verbose)
	    fprintf(stderr, "packaging %s\n", argv[ix]);
	if ((io.input = fopen(argv[ix], "r")) != 0) {
	    fprintf(io.output,
		    "--%s\n"
	           "Content-Disposition: inline; filename=\"%s\"\n",
			boundary, argv[ix]);

	    if (all64)
		istextIhope=0;
	    else {
		sz = fread(block0, 1, sizeof block0, io.input);
		for (istextIhope=1,j=0; j < sz; j++)
		    if (!isprint(block0[j]) && block0[j] != '\n'
					    && block0[j] != '\t') {
			istextIhope=0;
			break;
		    }
	    }
	    rewind(io.input);
	    
	    if (istextIhope) {
		if (verbose)
		    fprintf(stderr, "\t\tquoted-printable\n");
		fprintf(io.output,
			"Content-type: text/plain; name=\"%s\"\n"
			"Content-transfer-encoding: quoted-printable\n\n",
			argv[ix]);
		(*quoted_printable.encode)(readblock,writechar,&io);
	    }
	    else {
		if (verbose)
		    fprintf(stderr, "\t\tbase64\n");
		fprintf(io.output,
			"Content-type: application/octet-stream; name=\"%s\"\n"
		        "Content-transfer-encoding: base64\n\n", argv[ix]);
		(*base64.encode)(readblock,writechar,&io);
	    }
	    fclose(io.input);
	}
	else
	    perror(argv[ix]);
    }
    fprintf(io.output, "--%s--\n", boundary);
    fflush(io.output);
    if (tomail) {
	sleep(1);
	pclose(io.output);
    }
    exit(0);
}
